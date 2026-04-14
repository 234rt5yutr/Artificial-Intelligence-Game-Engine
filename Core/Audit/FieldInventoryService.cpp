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
                                                   const std::string& collectorId,
                                                   const bool required = true) {
    FieldInventoryEntry entry{};
    entry.Domain = domain;
    entry.OwnerSubsystem = ownerSubsystem;
    entry.TypeName = typeName;
    entry.FieldPath = fieldPath;
    entry.FieldId = ComposeFieldId(entry.Domain, entry.TypeName, entry.FieldPath);
    entry.Required = required;
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

[[nodiscard]] std::vector<FieldInventoryEntry> CollectSceneSerializedFields() {
    constexpr std::string_view domain = "scene";
    constexpr std::string_view owner = "scene-serialization";
    constexpr std::string_view collector = "serialized-scene-collector";
    constexpr std::string_view schemaType = "SceneSchema.v1";
    return {
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Header.Version", "Core/State/SceneLoader.h", "SceneSchema::Header.Version", 16u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Nodes.Transform", "Core/State/SceneLoader.h", "SceneSchema::Nodes.Transform", 22u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Nodes.Tags", "Core/State/SceneLoader.h", "SceneSchema::Nodes.Tags", 23u, std::string(collector), false)};
}

[[nodiscard]] std::vector<FieldInventoryEntry> CollectPrefabSerializedFields() {
    constexpr std::string_view domain = "prefab";
    constexpr std::string_view owner = "scene-serialization";
    constexpr std::string_view collector = "serialized-prefab-collector";
    constexpr std::string_view schemaType = "PrefabSchema.v1";
    return {
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "PrefabId", "Core/ECS/Scene.h", "PrefabSchema::PrefabId", 18u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Root.Transform", "Core/ECS/Scene.h", "PrefabSchema::Root.Transform", 20u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Overrides.Materials", "Core/ECS/Scene.h", "PrefabSchema::Overrides.Materials", 21u, std::string(collector), false)};
}

[[nodiscard]] std::vector<FieldInventoryEntry> CollectSaveSerializedFields() {
    constexpr std::string_view domain = "save";
    constexpr std::string_view owner = "state-save";
    constexpr std::string_view collector = "serialized-save-collector";
    constexpr std::string_view schemaType = "SaveSchema.v1";
    return {
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Metadata.SaveVersion", "Core/State/SaveFile.h", "SaveSchema::Metadata.SaveVersion", 14u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "World.PlayerState", "Core/State/SaveFile.h", "SaveSchema::World.PlayerState", 17u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "World.OptionalCheckpoint", "Core/State/SaveFile.h", "SaveSchema::World.OptionalCheckpoint", 18u, std::string(collector), false)};
}

[[nodiscard]] std::vector<FieldInventoryEntry> CollectWidgetSerializedFields() {
    constexpr std::string_view domain = "widget";
    constexpr std::string_view owner = "ui-authoring";
    constexpr std::string_view collector = "serialized-widget-collector";
    constexpr std::string_view schemaType = "WidgetSchema.v1";
    return {
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "WidgetId", "Core/UI/Authoring/WidgetBlueprintAsset.cpp", "WidgetSchema::WidgetId", 12u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Layout.Anchor", "Core/UI/Authoring/WidgetLayoutAsset.cpp", "WidgetSchema::Layout.Anchor", 21u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Bindings", "Core/UI/Binding/UIBindingService.h", "WidgetSchema::Bindings", 14u, std::string(collector), false)};
}

[[nodiscard]] std::vector<FieldInventoryEntry> CollectLocalizationSerializedFields() {
    constexpr std::string_view domain = "localization";
    constexpr std::string_view owner = "localization-pipeline";
    constexpr std::string_view collector = "serialized-localization-collector";
    constexpr std::string_view schemaType = "LocalizationSchema.v1";
    return {
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Locale", "Core/Application.cpp", "LocalizationSchema::Locale", 27u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Entries.Key", "Core/Application.cpp", "LocalizationSchema::Entries.Key", 29u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Entries.Context", "Core/Application.cpp", "LocalizationSchema::Entries.Context", 30u, std::string(collector), false)};
}

[[nodiscard]] std::vector<FieldInventoryEntry> CollectAddressableSerializedFields() {
    constexpr std::string_view domain = "addressable";
    constexpr std::string_view owner = "asset-addressables";
    constexpr std::string_view collector = "serialized-addressable-collector";
    constexpr std::string_view schemaType = "AddressableSchema.v1";
    return {
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "CatalogVersion", "Core/Asset/Addressables/AddressablesCatalogTypes.h", "AddressableSchema::CatalogVersion", 14u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Entries.Key", "Core/Asset/Addressables/AddressablesCatalogTypes.h", "AddressableSchema::Entries.Key", 22u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Entries.Labels", "Core/Asset/Addressables/AddressablesCatalogTypes.h", "AddressableSchema::Entries.Labels", 23u, std::string(collector), false)};
}

[[nodiscard]] std::vector<FieldInventoryEntry> CollectBundleSerializedFields() {
    constexpr std::string_view domain = "bundle";
    constexpr std::string_view owner = "asset-bundles";
    constexpr std::string_view collector = "serialized-bundle-collector";
    constexpr std::string_view schemaType = "BundleSchema.v1";
    return {
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "BundleId", "Core/Asset/Bundles/AssetBundleBuilder.h", "BundleSchema::BundleId", 16u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "AssetEntries.Path", "Core/Asset/Bundles/AssetBundleBuilder.h", "BundleSchema::AssetEntries.Path", 17u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "CompressionPreset", "Core/Asset/Bundles/AssetBundleBuilder.h", "BundleSchema::CompressionPreset", 18u, std::string(collector), false)};
}

[[nodiscard]] std::vector<FieldInventoryEntry> CollectBuildManifestSerializedFields() {
    constexpr std::string_view domain = "build-manifest";
    constexpr std::string_view owner = "build-pipeline";
    constexpr std::string_view collector = "serialized-build-manifest-collector";
    constexpr std::string_view schemaType = "BuildManifestSchema.v1";
    return {
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "BuildId", "Core/Build/BuildPipelineTypes.h", "BuildManifestSchema::BuildId", 22u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Artifacts.Path", "Core/Build/BuildPipelineTypes.h", "BuildManifestSchema::Artifacts.Path", 24u, std::string(collector), true),
        CreateFieldEntry(std::string(domain), std::string(owner), std::string(schemaType), "Artifacts.Hash", "Core/Build/BuildPipelineTypes.h", "BuildManifestSchema::Artifacts.Hash", 25u, std::string(collector), false)};
}

[[nodiscard]] bool EnsureOutputDirectory(const std::filesystem::path& outputDirectory) {
    std::error_code errorCode;
    const bool outputExists = std::filesystem::exists(outputDirectory, errorCode);
    if (errorCode) {
        return false;
    }

    if (outputExists) {
        const bool isDirectory = std::filesystem::is_directory(outputDirectory, errorCode);
        return !errorCode && isDirectory;
    }

    std::filesystem::create_directories(outputDirectory, errorCode);
    return !errorCode;
}

void SortEntries(std::vector<FieldInventoryEntry>& entries) {
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
}

[[nodiscard]] bool ValidateEntries(const std::vector<FieldInventoryEntry>& entries) {
    for (const FieldInventoryEntry& entry : entries) {
        if (entry.FieldId != ComposeFieldId(entry.Domain, entry.TypeName, entry.FieldPath) || entry.Domain.empty() ||
            entry.TypeName.empty() || entry.FieldPath.empty() || entry.SourceTrace.SourceFile.empty() ||
            entry.SourceTrace.SourceSymbol.empty() || entry.SourceTrace.CollectorId.empty() || entry.SourceTrace.SourceLine == 0u) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string ComputeInventoryDigest(const std::vector<FieldInventoryEntry>& entries);

[[nodiscard]] FieldInventorySnapshot CreateSnapshot(const FieldInventoryRequest& request, std::vector<FieldInventoryEntry>&& entries) {
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
    return snapshot;
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
        digestMaterial.push_back('|');
        digestMaterial.push_back(entry.Required ? '1' : '0');
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

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
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

    SortEntries(entries);

    if (!ValidateEntries(entries)) {
        return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_INVENTORY_FAILED");
    }

    return Result<FieldInventorySnapshot>::Success(CreateSnapshot(request, std::move(entries)));
}

Result<FieldInventorySnapshot> GenerateSerializedFieldInventory(const FieldInventoryRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty()) {
        return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "serialized") {
        return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<FieldInventoryEntry> entries;
    const std::array<std::vector<FieldInventoryEntry>, 8> collectedEntries = {
        CollectSceneSerializedFields(),
        CollectPrefabSerializedFields(),
        CollectSaveSerializedFields(),
        CollectWidgetSerializedFields(),
        CollectLocalizationSerializedFields(),
        CollectAddressableSerializedFields(),
        CollectBundleSerializedFields(),
        CollectBuildManifestSerializedFields()};

    for (const std::vector<FieldInventoryEntry>& domainEntries : collectedEntries) {
        entries.insert(entries.end(), domainEntries.begin(), domainEntries.end());
    }

    SortEntries(entries);

    if (!ValidateEntries(entries)) {
        return Result<FieldInventorySnapshot>::Failure("FIELD_AUDIT_INVENTORY_FAILED");
    }

    return Result<FieldInventorySnapshot>::Success(CreateSnapshot(request, std::move(entries)));
}

} // namespace Core::Audit
