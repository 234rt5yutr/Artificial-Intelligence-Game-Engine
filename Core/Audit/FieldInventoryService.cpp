#include "Core/Audit/FieldInventoryService.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Core::Audit {
namespace {

[[nodiscard]] uint64_t HashString(const std::string_view value) {
    constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;

    uint64_t hash = kFnvOffset;
    for (const unsigned char symbol : value) {
        hash ^= static_cast<uint64_t>(symbol);
        hash *= kFnvPrime;
    }
    return hash;
}

[[nodiscard]] std::string HashToHex(const uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

[[nodiscard]] std::string ComposeFieldId(const std::string& domain, const std::string& typeName, const std::string& fieldPath) {
    return domain + "::" + typeName + "::" + fieldPath;
}

[[nodiscard]] FieldInventoryEntry CreateFieldEntry(const std::string& domain,
                                                   const std::string& ownerSubsystem,
                                                   const std::string& typeName,
                                                   const std::string& fieldPath,
                                                   const std::string& sourceFile,
                                                   const std::string& sourceSymbol,
                                                   const uint32_t sourceLine,
                                                   const std::string& collectorId) {
    FieldInventoryEntry entry{};
    entry.Domain = domain;
    entry.OwnerSubsystem = ownerSubsystem;
    entry.TypeName = typeName;
    entry.FieldPath = fieldPath;
    entry.FieldId = ComposeFieldId(entry.Domain, entry.TypeName, entry.FieldPath);
    entry.SourceTrace.SourceFile = sourceFile;
    entry.SourceTrace.SourceSymbol = sourceSymbol;
    entry.SourceTrace.SourceLine = sourceLine;
    entry.SourceTrace.CollectorId = collectorId;
    return entry;
}

[[nodiscard]] std::vector<FieldInventoryEntry> CollectEcsRuntimeFields() {
    constexpr std::string_view domain = "ecs";
    constexpr std::string_view owner = "ecs-runtime";
    constexpr std::string_view collector = "runtime-ecs-collector";

    return {
        CreateFieldEntry(std::string(domain), std::string(owner), "TransformComponent", "Position", "Core/ECS/Components/TransformComponent.h", "TransformComponent::Position", 9u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "TransformComponent", "Rotation", "Core/ECS/Components/TransformComponent.h", "TransformComponent::Rotation", 10u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "TransformComponent", "Scale", "Core/ECS/Components/TransformComponent.h", "TransformComponent::Scale", 11u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "TransformComponent", "IsDirty", "Core/ECS/Components/TransformComponent.h", "TransformComponent::IsDirty", 17u, std::string(collector))
    };
}

[[nodiscard]] std::vector<FieldInventoryEntry> CollectUiRuntimeFields() {
    constexpr std::string_view domain = "ui";
    constexpr std::string_view owner = "ui-runtime";
    constexpr std::string_view collector = "runtime-ui-collector";

    return {
        CreateFieldEntry(std::string(domain), std::string(owner), "UIComponent", "WidgetId", "Core/ECS/Components/UIComponent.h", "UIComponent::WidgetId", 47u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "UIComponent", "Text", "Core/ECS/Components/UIComponent.h", "UIComponent::Text", 48u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "UIComponent", "Visible", "Core/ECS/Components/UIComponent.h", "UIComponent::Visible", 63u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "UIComponent", "Interactive", "Core/ECS/Components/UIComponent.h", "UIComponent::Interactive", 64u, std::string(collector))
    };
}

[[nodiscard]] std::vector<FieldInventoryEntry> CollectNetworkRuntimeFields() {
    constexpr std::string_view domain = "network";
    constexpr std::string_view owner = "network-runtime";
    constexpr std::string_view collector = "runtime-network-collector";

    return {
        CreateFieldEntry(std::string(domain), std::string(owner), "SessionInstanceRecord", "TickRate", "Core/Network/Session/SessionTypes.h", "SessionInstanceRecord::TickRate", 42u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "SessionInstanceRecord", "ConnectedClients", "Core/Network/Session/SessionTypes.h", "SessionInstanceRecord::ConnectedClients", 41u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "DedicatedServerStartRequest", "FeatureFlags.EnableReplay", "Core/Network/Session/SessionTypes.h", "DedicatedServerStartRequest::FeatureFlags.EnableReplay", 76u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "SessionJoinResult", "UsedCompatibilityDowngrade", "Core/Network/Session/SessionTypes.h", "SessionJoinResult::UsedCompatibilityDowngrade", 101u, std::string(collector))
    };
}

[[nodiscard]] std::vector<FieldInventoryEntry> CollectDiagnosticsRuntimeFields() {
    constexpr std::string_view domain = "diagnostics";
    constexpr std::string_view owner = "diagnostics-runtime";
    constexpr std::string_view collector = "runtime-diagnostics-collector";

    return {
        CreateFieldEntry(std::string(domain), std::string(owner), "ProfilerCaptureRequest", "DurationMs", "Core/Diagnostics/ProfilerCaptureTypes.h", "ProfilerCaptureRequest::DurationMs", 32u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "ProfilerCaptureRequest", "IncludeCpu", "Core/Diagnostics/ProfilerCaptureTypes.h", "ProfilerCaptureRequest::IncludeCpu", 33u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "ProfilerCaptureSession", "Completed", "Core/Diagnostics/ProfilerCaptureTypes.h", "ProfilerCaptureSession::Completed", 73u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "TraceExportResult", "Checksum", "Core/Diagnostics/ProfilerCaptureTypes.h", "TraceExportResult::Checksum", 84u, std::string(collector))
    };
}

[[nodiscard]] std::vector<FieldInventoryEntry> CollectBuildRuntimeFields() {
    constexpr std::string_view domain = "build";
    constexpr std::string_view owner = "build-services";
    constexpr std::string_view collector = "runtime-build-collector";

    return {
        CreateFieldEntry(std::string(domain), std::string(owner), "PlatformBuildRequest", "Platform", "Core/Build/BuildPipelineTypes.h", "PlatformBuildRequest::Platform", 32u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "PlatformBuildRequest", "OutputDirectory", "Core/Build/BuildPipelineTypes.h", "PlatformBuildRequest::OutputDirectory", 37u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "DedicatedServerBuildRequest", "BuildProfile", "Core/Build/BuildPipelineTypes.h", "DedicatedServerBuildRequest::BuildProfile", 106u, std::string(collector)),
        CreateFieldEntry(std::string(domain), std::string(owner), "DedicatedServerBuildResult", "DeterministicDigest", "Core/Build/BuildPipelineTypes.h", "DedicatedServerBuildResult::DeterministicDigest", 135u, std::string(collector))
    };
}

[[nodiscard]] std::string ComputeInventoryDigest(const std::vector<FieldInventoryEntry>& entries) {
    std::string digestMaterial;
    digestMaterial.reserve(entries.size() * 96u);
    for (const FieldInventoryEntry& entry : entries) {
        digestMaterial.append(entry.FieldId);
        digestMaterial.push_back('|');
        digestMaterial.append(entry.OwnerSubsystem);
        digestMaterial.push_back('|');
        digestMaterial.append(entry.SourceTrace.SourceFile);
        digestMaterial.push_back('|');
        digestMaterial.append(entry.SourceTrace.SourceSymbol);
        digestMaterial.push_back('|');
        digestMaterial.append(entry.SourceTrace.CollectorId);
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(entry.SourceTrace.SourceLine));
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

} // namespace

Result<FieldInventorySnapshot> GenerateRuntimeFieldInventory(const FieldInventoryRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty()) {
        return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "runtime") {
        return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    std::error_code errorCode;
    const bool outputExists = std::filesystem::exists(request.OutputDirectory, errorCode);
    if (errorCode) {
        return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    if (outputExists) {
        const bool isDirectory = std::filesystem::is_directory(request.OutputDirectory, errorCode);
        if (errorCode || !isDirectory) {
            return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
        }
    } else {
        std::filesystem::create_directories(request.OutputDirectory, errorCode);
        if (errorCode) {
            return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
        }
    }

    std::vector<FieldInventoryEntry> entries;
    const std::array<std::vector<FieldInventoryEntry>, 5> collectedEntries = {
        CollectEcsRuntimeFields(),
        CollectUiRuntimeFields(),
        CollectNetworkRuntimeFields(),
        CollectDiagnosticsRuntimeFields(),
        CollectBuildRuntimeFields()
    };
    for (const std::vector<FieldInventoryEntry>& domainEntries : collectedEntries) {
        entries.insert(entries.end(), domainEntries.begin(), domainEntries.end());
    }

    std::sort(entries.begin(), entries.end(), [](const FieldInventoryEntry& left, const FieldInventoryEntry& right) {
        if (left.FieldId != right.FieldId) {
            return left.FieldId < right.FieldId;
        }
        if (left.Domain != right.Domain) {
            return left.Domain < right.Domain;
        }
        if (left.TypeName != right.TypeName) {
            return left.TypeName < right.TypeName;
        }
        return left.FieldPath < right.FieldPath;
    });

    for (const FieldInventoryEntry& entry : entries) {
        if (entry.FieldId != ComposeFieldId(entry.Domain, entry.TypeName, entry.FieldPath) || entry.Domain.empty() ||
            entry.TypeName.empty() || entry.FieldPath.empty() || entry.SourceTrace.SourceFile.empty() ||
            entry.SourceTrace.CollectorId.empty()) {
            return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_INVENTORY_FAILED");
        }
    }

    std::set<std::string> domainSet;
    for (const FieldInventoryEntry& entry : entries) {
        domainSet.insert(entry.Domain);
    }

    FieldInventorySnapshot snapshot{};
    snapshot.Scope = request.Scope;
    snapshot.OutputDirectory = request.OutputDirectory;
    snapshot.Entries = std::move(entries);
    snapshot.Domains.assign(domainSet.begin(), domainSet.end());
    snapshot.DeterministicDigest = ComputeInventoryDigest(snapshot.Entries);
    return Result<FieldInventorySnapshot>::Success(std::move(snapshot));
}

} // namespace Core::Audit
