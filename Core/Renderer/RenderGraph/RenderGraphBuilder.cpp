#include "Core/Renderer/RenderGraph/RenderGraphBuilder.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Core::Renderer {
namespace {

class RenderGraphBuilderServiceImpl {
public:
    Result<RenderGraphPassHandle> RegisterPass(const RenderGraphPassRegistration& registration) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (registration.PassName.empty()) {
            return Result<RenderGraphPassHandle>::Failure("RENDER_GRAPH_INVALID_PASS_NAME");
        }

        if (!registration.Callback) {
            return Result<RenderGraphPassHandle>::Failure("RENDER_GRAPH_INVALID_PASS_CALLBACK");
        }

        RenderGraphPassRegistration normalized = registration;
        if (normalized.PassId.empty()) {
            normalized.PassId = normalized.PassName;
        }

        if (m_PassesById.find(normalized.PassId) != m_PassesById.end()) {
            return Result<RenderGraphPassHandle>::Failure("RENDER_GRAPH_DUPLICATE_PASS_ID");
        }

        if (m_PassIdByName.find(normalized.PassName) != m_PassIdByName.end()) {
            return Result<RenderGraphPassHandle>::Failure("RENDER_GRAPH_DUPLICATE_PASS_NAME");
        }

        const Result<void> declarationValidation = ValidateDeclarations(normalized);
        if (!declarationValidation.Ok) {
            return Result<RenderGraphPassHandle>::Failure(declarationValidation.Error);
        }

        const Result<void> dependencyValidation = ValidateDependencies(normalized);
        if (!dependencyValidation.Ok) {
            return Result<RenderGraphPassHandle>::Failure(dependencyValidation.Error);
        }

        RenderGraphPassHandle handle{};
        handle.PassId = normalized.PassId;
        handle.Value = HashPassId(normalized.PassId);

        const auto existing = m_PassIdByHandle.find(handle.Value);
        if (existing != m_PassIdByHandle.end() && existing->second != handle.PassId) {
            return Result<RenderGraphPassHandle>::Failure("RENDER_GRAPH_HANDLE_COLLISION");
        }

        RegisteredRenderGraphPass stored{};
        stored.Handle = handle;
        stored.Registration = std::move(normalized);

        m_PassesById.emplace(stored.Handle.PassId, stored);
        m_PassIdByName.emplace(stored.Registration.PassName, stored.Handle.PassId);
        m_PassIdByHandle.emplace(stored.Handle.Value, stored.Handle.PassId);
        m_RegistrationOrder.push_back(stored.Handle.PassId);
        return Result<RenderGraphPassHandle>::Success(handle);
    }

    std::vector<RegisteredRenderGraphPass> GetRegisteredPasses() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::vector<RegisteredRenderGraphPass> snapshot;
        snapshot.reserve(m_RegistrationOrder.size());
        for (const std::string& passId : m_RegistrationOrder) {
            const auto found = m_PassesById.find(passId);
            if (found != m_PassesById.end()) {
                snapshot.push_back(found->second);
            }
        }
        return snapshot;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_PassesById.clear();
        m_PassIdByName.clear();
        m_PassIdByHandle.clear();
        m_RegistrationOrder.clear();
    }

private:
    static bool IsReadCompatibleState(const RenderGraphResourceState state) {
        switch (state) {
            case RenderGraphResourceState::ShaderRead:
            case RenderGraphResourceState::ReadWrite:
            case RenderGraphResourceState::DepthRead:
            case RenderGraphResourceState::TransferSrc:
            case RenderGraphResourceState::Present:
                return true;
            default:
                return false;
        }
    }

    static bool IsWriteCompatibleState(const RenderGraphResourceState state) {
        switch (state) {
            case RenderGraphResourceState::ShaderWrite:
            case RenderGraphResourceState::ReadWrite:
            case RenderGraphResourceState::ColorAttachment:
            case RenderGraphResourceState::DepthWrite:
            case RenderGraphResourceState::TransferDst:
                return true;
            default:
                return false;
        }
    }

    static uint32_t HashPassId(const std::string& passId) {
        constexpr uint32_t fnvOffsetBasis = 2166136261u;
        constexpr uint32_t fnvPrime = 16777619u;
        uint32_t hash = fnvOffsetBasis;
        for (const unsigned char ch : passId) {
            hash ^= static_cast<uint32_t>(ch);
            hash *= fnvPrime;
        }
        return hash;
    }

    static std::optional<RenderGraphResourceState> FindResourceState(
        const std::vector<RenderGraphResourceAccessDeclaration>& declarations,
        const std::string& resourceName) {
        const auto found = std::find_if(
            declarations.begin(),
            declarations.end(),
            [&resourceName](const RenderGraphResourceAccessDeclaration& declaration) {
                return declaration.Resource == resourceName;
            });

        if (found == declarations.end()) {
            return std::nullopt;
        }
        return found->State;
    }

    static Result<void> ValidateDeclarations(const RenderGraphPassRegistration& registration) {
        std::unordered_set<std::string> readSet;
        std::unordered_set<std::string> writeSet;
        std::unordered_set<std::string> importedSet;

        for (const RenderGraphResourceAccessDeclaration& read : registration.Reads) {
            if (read.Resource.empty()) {
                return Result<void>::Failure("RENDER_GRAPH_MISSING_READ_RESOURCE");
            }
            if (readSet.find(read.Resource) != readSet.end()) {
                return Result<void>::Failure("RENDER_GRAPH_DUPLICATE_READ_RESOURCE");
            }
            if (!IsReadCompatibleState(read.State)) {
                return Result<void>::Failure("RENDER_GRAPH_INVALID_READ_ACCESS_STATE");
            }
            readSet.insert(read.Resource);
        }

        for (const RenderGraphResourceAccessDeclaration& write : registration.Writes) {
            if (write.Resource.empty()) {
                return Result<void>::Failure("RENDER_GRAPH_MISSING_WRITE_RESOURCE");
            }
            if (writeSet.find(write.Resource) != writeSet.end()) {
                return Result<void>::Failure("RENDER_GRAPH_DUPLICATE_WRITE_RESOURCE");
            }
            if (!IsWriteCompatibleState(write.State)) {
                return Result<void>::Failure("RENDER_GRAPH_INVALID_WRITE_ACCESS_STATE");
            }
            writeSet.insert(write.Resource);
        }

        for (const std::string& importedResource : registration.ImportedResources) {
            if (importedResource.empty()) {
                return Result<void>::Failure("RENDER_GRAPH_EMPTY_IMPORTED_RESOURCE");
            }
            if (importedSet.find(importedResource) != importedSet.end()) {
                return Result<void>::Failure("RENDER_GRAPH_DUPLICATE_IMPORTED_RESOURCE");
            }
            importedSet.insert(importedResource);
            const bool declaredInReadSet = readSet.find(importedResource) != readSet.end();
            const bool declaredInWriteSet = writeSet.find(importedResource) != writeSet.end();
            if (!declaredInReadSet && !declaredInWriteSet) {
                return Result<void>::Failure("RENDER_GRAPH_IMPORTED_RESOURCE_UNDECLARED_ACCESS");
            }
        }

        for (const std::string& resource : readSet) {
            const auto writeState = FindResourceState(registration.Writes, resource);
            if (!writeState.has_value()) {
                continue;
            }
            const auto readState = FindResourceState(registration.Reads, resource);
            if (!readState.has_value()) {
                continue;
            }
            if (*readState != RenderGraphResourceState::ReadWrite &&
                *writeState != RenderGraphResourceState::ReadWrite) {
                return Result<void>::Failure("RENDER_GRAPH_INVALID_READ_WRITE_COMBINATION");
            }
        }

        const bool hasResourceDeclarations = !registration.Reads.empty() || !registration.Writes.empty();
        if (!hasResourceDeclarations && !registration.HasSideEffects) {
            return Result<void>::Failure("RENDER_GRAPH_MISSING_RESOURCE_DECLARATIONS");
        }

        return Result<void>::Success();
    }

    Result<void> ValidateDependencies(const RenderGraphPassRegistration& registration) const {
        std::unordered_set<std::string> dependencySet;
        for (const std::string& dependency : registration.DependsOn) {
            if (dependency.empty()) {
                return Result<void>::Failure("RENDER_GRAPH_EMPTY_DEPENDENCY");
            }
            if (dependency == registration.PassId) {
                return Result<void>::Failure("RENDER_GRAPH_DIRECT_CYCLE_CANDIDATE");
            }
            if (dependencySet.find(dependency) != dependencySet.end()) {
                return Result<void>::Failure("RENDER_GRAPH_DUPLICATE_DEPENDENCY");
            }
            dependencySet.insert(dependency);

            const auto existing = m_PassesById.find(dependency);
            if (existing != m_PassesById.end()) {
                const auto reverseDependencyFound = std::find(
                    existing->second.Registration.DependsOn.begin(),
                    existing->second.Registration.DependsOn.end(),
                    registration.PassId);
                if (reverseDependencyFound != existing->second.Registration.DependsOn.end()) {
                    return Result<void>::Failure("RENDER_GRAPH_DIRECT_CYCLE_CANDIDATE");
                }
            }
        }
        return Result<void>::Success();
    }

    mutable std::mutex m_Mutex;
    std::unordered_map<std::string, RegisteredRenderGraphPass> m_PassesById;
    std::unordered_map<std::string, std::string> m_PassIdByName;
    std::unordered_map<uint32_t, std::string> m_PassIdByHandle;
    std::vector<std::string> m_RegistrationOrder;
};

RenderGraphBuilderServiceImpl& GetImpl() {
    static RenderGraphBuilderServiceImpl impl;
    return impl;
}

} // namespace

Result<RenderGraphPassHandle> RenderGraphBuilderService::RegisterPass(const RenderGraphPassRegistration& registration) {
    return GetImpl().RegisterPass(registration);
}

std::vector<RegisteredRenderGraphPass> RenderGraphBuilderService::GetRegisteredPasses() const {
    return GetImpl().GetRegisteredPasses();
}

void RenderGraphBuilderService::Clear() {
    GetImpl().Clear();
}

RenderGraphBuilderService& GetRenderGraphBuilderService() {
    static RenderGraphBuilderService service;
    return service;
}

Result<RenderGraphPassHandle> RegisterRenderGraphPass(const RenderGraphPassRegistration& registration) {
    return GetRenderGraphBuilderService().RegisterPass(registration);
}

} // namespace Core::Renderer

