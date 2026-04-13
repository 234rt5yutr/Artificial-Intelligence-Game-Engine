#include "Core/Renderer/RenderGraph/RenderGraphExecutor.h"

#include "Core/Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Core/Renderer/RenderGraph/TransientResourceAllocator.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace Core::Renderer {
namespace {

struct PassAccessRecord {
    uint32_t PassOrderIndex = 0;
    RenderGraphResourceState State = RenderGraphResourceState::Unknown;
    RenderGraphQueue Queue = RenderGraphQueue::Graphics;
    bool IsWrite = false;
};

struct RenderGraphEdge {
    std::string SourcePass;
    std::string DestinationPass;
    std::string Reason;
};

struct DigestBuilder {
    uint64_t Value = 1469598103934665603ull;

    void Append(const std::string_view token) {
        constexpr uint64_t prime = 1099511628211ull;
        for (const unsigned char ch : token) {
            Value ^= static_cast<uint64_t>(ch);
            Value *= prime;
        }
    }

    std::string FinalizeHex() const {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0') << std::setw(16) << Value;
        return stream.str();
    }
};

std::string QueueToString(const RenderGraphQueue queue) {
    switch (queue) {
        case RenderGraphQueue::Graphics:
            return "graphics";
        case RenderGraphQueue::Compute:
            return "compute";
        case RenderGraphQueue::Transfer:
            return "transfer";
        default:
            return "unknown";
    }
}

std::string StateToString(const RenderGraphResourceState state) {
    switch (state) {
        case RenderGraphResourceState::Unknown:
            return "unknown";
        case RenderGraphResourceState::ShaderRead:
            return "shader_read";
        case RenderGraphResourceState::ShaderWrite:
            return "shader_write";
        case RenderGraphResourceState::ReadWrite:
            return "read_write";
        case RenderGraphResourceState::ColorAttachment:
            return "color_attachment";
        case RenderGraphResourceState::DepthRead:
            return "depth_read";
        case RenderGraphResourceState::DepthWrite:
            return "depth_write";
        case RenderGraphResourceState::TransferSrc:
            return "transfer_src";
        case RenderGraphResourceState::TransferDst:
            return "transfer_dst";
        case RenderGraphResourceState::Present:
            return "present";
        default:
            return "unknown";
    }
}

bool IsUndeclaredSideEffectCandidate(const RenderGraphPassRegistration& registration) {
    const bool hasResourceDeclarations = !registration.Reads.empty() || !registration.Writes.empty();
    const bool hasExplicitDependencies = !registration.DependsOn.empty();
    return !hasResourceDeclarations && !hasExplicitDependencies && !registration.HasSideEffects;
}

void AddEdge(
    const std::string& sourcePass,
    const std::string& destinationPass,
    const std::string& reason,
    std::set<std::pair<std::string, std::string>>& edgeSet,
    std::vector<RenderGraphEdge>& edges) {
    if (sourcePass == destinationPass) {
        return;
    }
    const std::pair<std::string, std::string> edge = std::make_pair(sourcePass, destinationPass);
    if (edgeSet.find(edge) != edgeSet.end()) {
        return;
    }
    edgeSet.insert(edge);
    RenderGraphEdge record{};
    record.SourcePass = sourcePass;
    record.DestinationPass = destinationPass;
    record.Reason = reason;
    edges.push_back(std::move(record));
}

} // namespace

Result<RenderGraphExecutionReport> RenderGraphExecutorService::Execute(const RenderGraphExecutionContext& context) const {
    using Clock = std::chrono::high_resolution_clock;
    const auto compileStart = Clock::now();

    const std::vector<RegisteredRenderGraphPass> passes = GetRenderGraphBuilderService().GetRegisteredPasses();
    if (passes.empty()) {
        return Result<RenderGraphExecutionReport>::Failure("RENDER_GRAPH_NO_PASSES_REGISTERED");
    }

    std::unordered_map<std::string, RenderGraphPassRegistration> registrationsById;
    std::unordered_map<std::string, RenderGraphPassHandle> handlesById;
    registrationsById.reserve(passes.size());
    handlesById.reserve(passes.size());

    for (const RegisteredRenderGraphPass& pass : passes) {
        registrationsById.emplace(pass.Handle.PassId, pass.Registration);
        handlesById.emplace(pass.Handle.PassId, pass.Handle);
    }

    std::set<std::pair<std::string, std::string>> edgeSet;
    std::vector<RenderGraphEdge> edges;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    std::unordered_map<std::string, uint32_t> indegree;
    std::unordered_map<std::string, std::string> lastWriterByResource;
    std::unordered_map<std::string, std::vector<std::string>> readersByResource;

    for (const RegisteredRenderGraphPass& pass : passes) {
        indegree[pass.Handle.PassId] = 0;
    }

    for (const RegisteredRenderGraphPass& pass : passes) {
        const std::string passId = pass.Handle.PassId;
        const RenderGraphPassRegistration& registration = pass.Registration;

        if (context.StrictMode && IsUndeclaredSideEffectCandidate(registration)) {
            return Result<RenderGraphExecutionReport>::Failure("RENDER_GRAPH_STRICT_UNDECLARED_SIDE_EFFECTS");
        }

        for (const std::string& dependency : registration.DependsOn) {
            const auto found = handlesById.find(dependency);
            if (found == handlesById.end()) {
                return Result<RenderGraphExecutionReport>::Failure("RENDER_GRAPH_MISSING_DEPENDENCY_PASS");
            }
            AddEdge(dependency, passId, "explicit_dependency", edgeSet, edges);
        }

        for (const RenderGraphResourceAccessDeclaration& read : registration.Reads) {
            const auto writer = lastWriterByResource.find(read.Resource);
            if (writer != lastWriterByResource.end()) {
                AddEdge(writer->second, passId, "resource_read_after_write", edgeSet, edges);
            }
            readersByResource[read.Resource].push_back(passId);
        }

        for (const RenderGraphResourceAccessDeclaration& write : registration.Writes) {
            const auto writer = lastWriterByResource.find(write.Resource);
            if (writer != lastWriterByResource.end()) {
                AddEdge(writer->second, passId, "resource_write_after_write", edgeSet, edges);
            }

            const auto readers = readersByResource.find(write.Resource);
            if (readers != readersByResource.end()) {
                for (const std::string& readerPassId : readers->second) {
                    AddEdge(readerPassId, passId, "resource_write_after_read", edgeSet, edges);
                }
            }

            lastWriterByResource[write.Resource] = passId;
            readersByResource[write.Resource].clear();
        }
    }

    for (const RenderGraphEdge& edge : edges) {
        adjacency[edge.SourcePass].push_back(edge.DestinationPass);
        ++indegree[edge.DestinationPass];
    }

    std::set<std::string> ready;
    for (const auto& [passId, degree] : indegree) {
        if (degree == 0) {
            ready.insert(passId);
        }
    }

    std::vector<std::string> sortedPasses;
    sortedPasses.reserve(passes.size());
    while (!ready.empty()) {
        if (context.StrictMode && ready.size() > 1) {
            return Result<RenderGraphExecutionReport>::Failure("RENDER_GRAPH_STRICT_AMBIGUOUS_ORDERING");
        }

        const std::string current = *ready.begin();
        ready.erase(ready.begin());
        sortedPasses.push_back(current);

        const auto children = adjacency.find(current);
        if (children == adjacency.end()) {
            continue;
        }

        for (const std::string& child : children->second) {
            auto foundInDegree = indegree.find(child);
            if (foundInDegree == indegree.end() || foundInDegree->second == 0) {
                continue;
            }
            --foundInDegree->second;
            if (foundInDegree->second == 0) {
                ready.insert(child);
            }
        }
    }

    if (sortedPasses.size() != passes.size()) {
        return Result<RenderGraphExecutionReport>::Failure("RENDER_GRAPH_CYCLE_DETECTED");
    }

    std::unordered_map<std::string, uint32_t> orderIndexByPassId;
    orderIndexByPassId.reserve(sortedPasses.size());
    for (uint32_t orderIndex = 0; orderIndex < static_cast<uint32_t>(sortedPasses.size()); ++orderIndex) {
        orderIndexByPassId.emplace(sortedPasses[orderIndex], orderIndex);
    }

    std::map<std::string, RenderGraphResourceLifetime> lifetimesByResource;
    std::map<std::string, std::vector<PassAccessRecord>> accessRecordsByResource;
    std::unordered_set<std::string> importedResources;

    for (const RegisteredRenderGraphPass& pass : passes) {
        const RenderGraphPassRegistration& registration = pass.Registration;
        const uint32_t passOrderIndex = orderIndexByPassId[pass.Handle.PassId];

        for (const std::string& imported : registration.ImportedResources) {
            importedResources.insert(imported);
        }

        for (const RenderGraphResourceAccessDeclaration& read : registration.Reads) {
            auto& lifetime = lifetimesByResource[read.Resource];
            if (lifetime.Resource.empty()) {
                lifetime.Resource = read.Resource;
                lifetime.FirstPassIndex = passOrderIndex;
                lifetime.LastPassIndex = passOrderIndex;
            } else {
                lifetime.FirstPassIndex = std::min(lifetime.FirstPassIndex, passOrderIndex);
                lifetime.LastPassIndex = std::max(lifetime.LastPassIndex, passOrderIndex);
            }
            lifetime.EstimatedSizeBytes = std::max(lifetime.EstimatedSizeBytes, read.EstimatedSizeBytes);

            PassAccessRecord access{};
            access.PassOrderIndex = passOrderIndex;
            access.State = read.State;
            access.Queue = registration.Queue;
            access.IsWrite = false;
            accessRecordsByResource[read.Resource].push_back(access);
        }

        for (const RenderGraphResourceAccessDeclaration& write : registration.Writes) {
            auto& lifetime = lifetimesByResource[write.Resource];
            if (lifetime.Resource.empty()) {
                lifetime.Resource = write.Resource;
                lifetime.FirstPassIndex = passOrderIndex;
                lifetime.LastPassIndex = passOrderIndex;
            } else {
                lifetime.FirstPassIndex = std::min(lifetime.FirstPassIndex, passOrderIndex);
                lifetime.LastPassIndex = std::max(lifetime.LastPassIndex, passOrderIndex);
            }
            lifetime.EstimatedSizeBytes = std::max(lifetime.EstimatedSizeBytes, write.EstimatedSizeBytes);

            PassAccessRecord access{};
            access.PassOrderIndex = passOrderIndex;
            access.State = write.State;
            access.Queue = registration.Queue;
            access.IsWrite = true;
            accessRecordsByResource[write.Resource].push_back(access);
        }
    }

    uint32_t barrierCount = 0;
    uint32_t queueSynchronizationCount = 0;
    for (auto& [resource, accesses] : accessRecordsByResource) {
        std::sort(
            accesses.begin(),
            accesses.end(),
            [](const PassAccessRecord& lhs, const PassAccessRecord& rhs) {
                return lhs.PassOrderIndex < rhs.PassOrderIndex;
            });

        for (size_t index = 1; index < accesses.size(); ++index) {
            const PassAccessRecord& previous = accesses[index - 1];
            const PassAccessRecord& current = accesses[index];
            const bool stateTransition = previous.State != current.State;
            const bool hazardTransition = previous.IsWrite || current.IsWrite;
            if (stateTransition || hazardTransition) {
                ++barrierCount;
            }
            if (previous.Queue != current.Queue) {
                ++queueSynchronizationCount;
            }
        }
    }

    std::vector<TransientResourceRequest> transientRequests;
    transientRequests.reserve(lifetimesByResource.size());
    for (auto& [resourceName, lifetime] : lifetimesByResource) {
        lifetime.Imported = importedResources.find(resourceName) != importedResources.end();
        TransientResourceRequest request{};
        request.Resource = resourceName;
        request.FirstPassIndex = lifetime.FirstPassIndex;
        request.LastPassIndex = lifetime.LastPassIndex;
        request.EstimatedSizeBytes = lifetime.EstimatedSizeBytes;
        request.Imported = lifetime.Imported;
        transientRequests.push_back(std::move(request));
    }

    TransientResourceAllocator allocator;
    const TransientResourceAllocationReport allocationReport =
        allocator.Allocate(transientRequests, context.AllowTransientAliasing);

    for (const TransientResourceAliasAssignment& assignment : allocationReport.Assignments) {
        auto foundLifetime = lifetimesByResource.find(assignment.Resource);
        if (foundLifetime != lifetimesByResource.end()) {
            foundLifetime->second.AliasGroup = assignment.AliasGroup;
        }
    }

    RenderGraphExecutionReport report{};
    report.PassOrder = sortedPasses;
    report.BarrierCount = barrierCount;
    report.QueueSynchronizationCount = queueSynchronizationCount;
    report.AliasSavingsBytes = allocationReport.AliasSavingsBytes;
    report.ResourceLifetimes.reserve(lifetimesByResource.size());

    DigestBuilder digestBuilder;
    digestBuilder.Append(context.FrameTag);
    for (const std::string& passId : sortedPasses) {
        digestBuilder.Append(passId);
    }
    for (const RenderGraphEdge& edge : edges) {
        digestBuilder.Append(edge.SourcePass);
        digestBuilder.Append(edge.DestinationPass);
        digestBuilder.Append(edge.Reason);
    }

    for (const auto& [resourceName, lifetime] : lifetimesByResource) {
        report.ResourceLifetimes.push_back(lifetime);
        digestBuilder.Append(resourceName);
        digestBuilder.Append(std::to_string(lifetime.FirstPassIndex));
        digestBuilder.Append(std::to_string(lifetime.LastPassIndex));
        digestBuilder.Append(std::to_string(lifetime.AliasGroup));
        digestBuilder.Append(std::to_string(lifetime.EstimatedSizeBytes));
    }

    for (const std::string& passId : sortedPasses) {
        const auto foundRegistration = registrationsById.find(passId);
        if (foundRegistration == registrationsById.end()) {
            return Result<RenderGraphExecutionReport>::Failure("RENDER_GRAPH_INTERNAL_REGISTRATION_LOST");
        }

        const auto passStart = Clock::now();
        try {
            foundRegistration->second.Callback();
        } catch (const std::exception&) {
            return Result<RenderGraphExecutionReport>::Failure("RENDER_GRAPH_PASS_CALLBACK_EXCEPTION");
        } catch (...) {
            return Result<RenderGraphExecutionReport>::Failure("RENDER_GRAPH_PASS_CALLBACK_UNKNOWN_EXCEPTION");
        }
        const auto passEnd = Clock::now();
        const auto duration = std::chrono::duration<double, std::milli>(passEnd - passStart).count();

        RenderGraphPassTiming timing{};
        timing.PassId = passId;
        timing.DurationMs = duration;
        report.PassTimings.push_back(timing);
    }

    const auto compileEnd = Clock::now();
    const double compileDurationMs = std::chrono::duration<double, std::milli>(compileEnd - compileStart).count();
    if (context.MaxCompileBudgetMs > 0.0 && compileDurationMs > context.MaxCompileBudgetMs) {
        report.ValidationDiagnostics.push_back("RENDER_GRAPH_COMPILE_BUDGET_EXCEEDED");
    }

    report.CompileDigest = digestBuilder.FinalizeHex();

    if (context.EmitDiagnostics) {
        nlohmann::json diagnostics = nlohmann::json::object();
        diagnostics["frameTag"] = context.FrameTag;
        diagnostics["compileDigest"] = report.CompileDigest;
        diagnostics["passOrder"] = report.PassOrder;
        diagnostics["barrierCount"] = report.BarrierCount;
        diagnostics["queueSynchronizationCount"] = report.QueueSynchronizationCount;
        diagnostics["aliasSavingsBytes"] = report.AliasSavingsBytes;
        diagnostics["strictMode"] = context.StrictMode;

        nlohmann::json passItems = nlohmann::json::array();
        for (const std::string& passId : sortedPasses) {
            const RenderGraphPassRegistration& registration = registrationsById[passId];
            nlohmann::json passItem = nlohmann::json::object();
            passItem["passId"] = passId;
            passItem["passName"] = registration.PassName;
            passItem["debugLabel"] = registration.DebugLabel;
            passItem["queue"] = QueueToString(registration.Queue);
            passItem["dependsOn"] = registration.DependsOn;
            passItem["hasSideEffects"] = registration.HasSideEffects;

            nlohmann::json reads = nlohmann::json::array();
            for (const RenderGraphResourceAccessDeclaration& read : registration.Reads) {
                nlohmann::json readItem = nlohmann::json::object();
                readItem["resource"] = read.Resource;
                readItem["state"] = StateToString(read.State);
                readItem["estimatedSizeBytes"] = read.EstimatedSizeBytes;
                reads.push_back(std::move(readItem));
            }
            passItem["reads"] = std::move(reads);

            nlohmann::json writes = nlohmann::json::array();
            for (const RenderGraphResourceAccessDeclaration& write : registration.Writes) {
                nlohmann::json writeItem = nlohmann::json::object();
                writeItem["resource"] = write.Resource;
                writeItem["state"] = StateToString(write.State);
                writeItem["estimatedSizeBytes"] = write.EstimatedSizeBytes;
                writes.push_back(std::move(writeItem));
            }
            passItem["writes"] = std::move(writes);
            passItem["importedResources"] = registration.ImportedResources;

            passItems.push_back(std::move(passItem));
        }
        diagnostics["passes"] = std::move(passItems);

        nlohmann::json edgeItems = nlohmann::json::array();
        for (const RenderGraphEdge& edge : edges) {
            nlohmann::json edgeItem = nlohmann::json::object();
            edgeItem["source"] = edge.SourcePass;
            edgeItem["destination"] = edge.DestinationPass;
            edgeItem["reason"] = edge.Reason;
            edgeItems.push_back(std::move(edgeItem));
        }
        diagnostics["edges"] = std::move(edgeItems);

        nlohmann::json resourceItems = nlohmann::json::array();
        for (const RenderGraphResourceLifetime& lifetime : report.ResourceLifetimes) {
            nlohmann::json resourceItem = nlohmann::json::object();
            resourceItem["resource"] = lifetime.Resource;
            resourceItem["firstPassIndex"] = lifetime.FirstPassIndex;
            resourceItem["lastPassIndex"] = lifetime.LastPassIndex;
            resourceItem["aliasGroup"] = lifetime.AliasGroup;
            resourceItem["estimatedSizeBytes"] = lifetime.EstimatedSizeBytes;
            resourceItem["imported"] = lifetime.Imported;
            resourceItems.push_back(std::move(resourceItem));
        }
        diagnostics["resources"] = std::move(resourceItems);

        nlohmann::json timingItems = nlohmann::json::array();
        for (const RenderGraphPassTiming& timing : report.PassTimings) {
            nlohmann::json timingItem = nlohmann::json::object();
            timingItem["passId"] = timing.PassId;
            timingItem["durationMs"] = timing.DurationMs;
            timingItems.push_back(std::move(timingItem));
        }
        diagnostics["passTimings"] = std::move(timingItems);
        diagnostics["validationDiagnostics"] = report.ValidationDiagnostics;

        report.DiagnosticsJson = diagnostics.dump();
    }

    return Result<RenderGraphExecutionReport>::Success(std::move(report));
}

RenderGraphExecutorService& GetRenderGraphExecutorService() {
    static RenderGraphExecutorService service;
    return service;
}

Result<RenderGraphExecutionReport> ExecuteRenderGraph(const RenderGraphExecutionContext& context) {
    return GetRenderGraphExecutorService().Execute(context);
}

} // namespace Core::Renderer

