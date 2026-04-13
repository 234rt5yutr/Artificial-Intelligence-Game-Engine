#include "Core/UI/Authoring/UIAssetAuthoringService.h"

#include "Core/Security/PathValidator.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <future>
#include <unordered_set>

namespace Core {
namespace UI {
namespace Authoring {
namespace {

    std::filesystem::path ResolveCookedMetadataPath(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& explicitPath) {
        if (!explicitPath.empty()) {
            return explicitPath;
        }
        return sourcePath.parent_path() /
               (sourcePath.stem().string() + ".widgetbp.meta.json");
    }

    std::filesystem::path ResolveManifestPath(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& explicitPath) {
        if (!explicitPath.empty()) {
            return explicitPath;
        }
        return sourcePath.parent_path() / "widget_blueprint_manifest.json";
    }

    bool WriteJsonDocument(const std::filesystem::path& path, const Json& document, std::string* errorMessage) {
        std::error_code ec;
        const std::filesystem::path parentPath = path.parent_path();
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath, ec);
            if (ec) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Failed to create directory for " + path.string();
                }
                return false;
            }
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to open file for writing: " + path.string();
            }
            return false;
        }

        output << document.dump(2);
        if (!output.good()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to write JSON document: " + path.string();
            }
            return false;
        }

        return true;
    }

    bool ReadJsonDocument(const std::filesystem::path& path, Json& document, std::string* errorMessage) {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to open JSON file: " + path.string();
            }
            return false;
        }

        try {
            input >> document;
        } catch (const nlohmann::json::parse_error&) {
            if (errorMessage != nullptr) {
                *errorMessage = "JSON parse error in " + path.string();
            }
            return false;
        }

        return true;
    }

    bool LoadManifest(const std::filesystem::path& path, Json& manifest, std::string* errorMessage) {
        manifest = Json::object();
        manifest["schemaVersion"] = WIDGET_BLUEPRINT_SCHEMA_VERSION;
        manifest["assetType"] = "widget_blueprint_manifest";
        manifest["entries"] = Json::array();

        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) {
            return true;
        }

        if (!ReadJsonDocument(path, manifest, errorMessage)) {
            return false;
        }

        if (!manifest.is_object()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Manifest root must be an object: " + path.string();
            }
            return false;
        }

        if (!manifest.contains("entries")) {
            manifest["entries"] = Json::array();
            return true;
        }

        if (!manifest["entries"].is_array()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Manifest entries must be an array: " + path.string();
            }
            return false;
        }

        return true;
    }

    Json BuildManifestEntry(
        const WidgetBlueprintAsset& blueprint,
        uint64_t sourceHash,
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& cookedMetadataPath) {

        const auto now = std::chrono::system_clock::now();
        const uint64_t authoredTimestampSeconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());

        return {
            {"blueprintId", blueprint.BlueprintId},
            {"schemaVersion", blueprint.SchemaVersion},
            {"parentBlueprintId", blueprint.ParentBlueprintId},
            {"widgetType", blueprint.WidgetType},
            {"sourcePath", sourcePath.string()},
            {"cookedMetadataPath", cookedMetadataPath.string()},
            {"sourceHash", sourceHash},
            {"authoredTimestamp", authoredTimestampSeconds}
        };
    }

    bool StyleDependencyExists(const std::string& styleDependencyId) {
        if (styleDependencyId.empty()) {
            return true;
        }

        const std::optional<std::filesystem::path> validatedPath =
            Security::PathValidator::ValidateAssetPath(std::filesystem::path(styleDependencyId));
        if (!validatedPath.has_value()) {
            return false;
        }

        std::error_code ec;
        return std::filesystem::exists(validatedPath.value(), ec) && !ec;
    }

    std::string BuildLayoutPoolKey(const std::string& layoutId, const std::string& nodeId) {
        return layoutId + ":" + nodeId;
    }

    const WidgetLayoutNode* FindNodeById(const WidgetLayoutAsset& layout, const std::string& nodeId) {
        auto nodeIt = std::find_if(
            layout.Nodes.begin(),
            layout.Nodes.end(),
            [&nodeId](const WidgetLayoutNode& node) { return node.NodeId == nodeId; });
        if (nodeIt == layout.Nodes.end()) {
            return nullptr;
        }
        return &(*nodeIt);
    }

} // namespace

    UIAssetAuthoringService& UIAssetAuthoringService::Get() {
        static UIAssetAuthoringService instance;
        return instance;
    }

    CreateWidgetBlueprintAssetResult UIAssetAuthoringService::CreateWidgetBlueprintAsset(
        const CreateWidgetBlueprintAssetRequest& request) {
        CreateWidgetBlueprintAssetResult result;
        result.Success = false;

        if (request.SourceBlueprintPath.empty()) {
            result.ErrorCode = UI_ASSET_AUTHORING_INVALID_REQUEST;
            result.Message = "SourceBlueprintPath cannot be empty.";
            return result;
        }

        WidgetBlueprintValidationResult validation = ValidateWidgetBlueprintAsset(request.Blueprint);
        if (!validation.Valid) {
            result.ErrorCode = validation.ErrorCode;
            result.Message = validation.Message;
            result.Diagnostics = validation.Diagnostics;
            return result;
        }

        const std::optional<std::filesystem::path> validatedSourcePath =
            Security::PathValidator::ValidateAssetPath(request.SourceBlueprintPath);
        if (!validatedSourcePath.has_value()) {
            result.ErrorCode = UI_ASSET_AUTHORING_PATH_VALIDATION_FAILED;
            result.Message = "Source blueprint path validation failed.";
            return result;
        }
        result.SourceBlueprintPath = validatedSourcePath.value();

        const std::filesystem::path requestedCookedPath = ResolveCookedMetadataPath(
            result.SourceBlueprintPath,
            request.CookedMetadataPath);
        const std::optional<std::filesystem::path> validatedCookedPath =
            Security::PathValidator::ValidateCookedPath(requestedCookedPath);
        if (!validatedCookedPath.has_value()) {
            result.ErrorCode = UI_ASSET_AUTHORING_PATH_VALIDATION_FAILED;
            result.Message = "Cooked metadata path validation failed.";
            return result;
        }
        result.CookedMetadataPath = validatedCookedPath.value();

        const std::filesystem::path requestedManifestPath = ResolveManifestPath(
            result.SourceBlueprintPath,
            request.ManifestPath);
        const std::optional<std::filesystem::path> validatedManifestPath =
            Security::PathValidator::ValidateAssetPath(requestedManifestPath);
        if (!validatedManifestPath.has_value()) {
            result.ErrorCode = UI_ASSET_AUTHORING_PATH_VALIDATION_FAILED;
            result.Message = "Manifest path validation failed.";
            return result;
        }
        result.ManifestPath = validatedManifestPath.value();

        std::error_code ec;
        if (!request.OverwriteExisting && std::filesystem::exists(result.SourceBlueprintPath, ec) && !ec) {
            result.ErrorCode = UI_ASSET_AUTHORING_SOURCE_EXISTS;
            result.Message = "Widget blueprint source already exists and overwrite is disabled.";
            return result;
        }

        std::string loopBlueprintId;
        if (DetectWidgetBlueprintInheritanceLoop(request.Blueprint, m_KnownBlueprints, &loopBlueprintId)) {
            result.ErrorCode = UI_ASSET_AUTHORING_INHERITANCE_LOOP;
            result.Message = "Widget blueprint inheritance loop detected at '" + loopBlueprintId + "'.";
            return result;
        }

        if (!request.Blueprint.ParentBlueprintId.empty() &&
            m_KnownBlueprints.find(request.Blueprint.ParentBlueprintId) == m_KnownBlueprints.end()) {
            result.Diagnostics.push_back(
                "Unresolved parentBlueprintId '" + request.Blueprint.ParentBlueprintId + "'.");
        }

        const Json sourcePayload = request.SourcePayload.has_value()
            ? request.SourcePayload.value()
            : SerializeWidgetBlueprintAsset(request.Blueprint);
        std::vector<std::string> deprecatedFields = CollectDeprecatedWidgetBlueprintFields(sourcePayload);
        for (const std::string& deprecatedField : deprecatedFields) {
            result.Diagnostics.push_back("Deprecated blueprint field: " + deprecatedField);
        }

        const Json canonicalBlueprint = SerializeWidgetBlueprintAsset(request.Blueprint);
        result.DeterministicSourceHash = ComputeWidgetBlueprintDeterministicHash(request.Blueprint);

        const WidgetBlueprintCookedMetadata metadata = BuildWidgetBlueprintCookedMetadata(
            request.Blueprint,
            result.DeterministicSourceHash);
        const Json cookedMetadataPayload = SerializeWidgetBlueprintCookedMetadata(metadata);

        std::string writeError;
        if (!WriteJsonDocument(result.SourceBlueprintPath, canonicalBlueprint, &writeError)) {
            result.ErrorCode = UI_ASSET_AUTHORING_WRITE_FAILED;
            result.Message = writeError;
            return result;
        }

        if (!WriteJsonDocument(result.CookedMetadataPath, cookedMetadataPayload, &writeError)) {
            result.ErrorCode = UI_ASSET_AUTHORING_WRITE_FAILED;
            result.Message = writeError;
            return result;
        }

        Json manifest;
        if (!LoadManifest(result.ManifestPath, manifest, &writeError)) {
            result.ErrorCode = UI_ASSET_AUTHORING_MANIFEST_INVALID;
            result.Message = writeError;
            return result;
        }

        Json& entries = manifest["entries"];
        for (auto entryIt = entries.begin(); entryIt != entries.end();) {
            if (entryIt->is_object() && entryIt->value("blueprintId", "") == request.Blueprint.BlueprintId) {
                entryIt = entries.erase(entryIt);
            } else {
                ++entryIt;
            }
        }

        result.ManifestEntry = BuildManifestEntry(
            request.Blueprint,
            result.DeterministicSourceHash,
            result.SourceBlueprintPath,
            result.CookedMetadataPath);
        entries.push_back(result.ManifestEntry);

        if (!WriteJsonDocument(result.ManifestPath, manifest, &writeError)) {
            result.ErrorCode = UI_ASSET_AUTHORING_WRITE_FAILED;
            result.Message = writeError;
            return result;
        }

        m_KnownBlueprints[request.Blueprint.BlueprintId] = request.Blueprint;

        result.Success = true;
        return result;
    }

    LoadWidgetLayoutAssetResult UIAssetAuthoringService::LoadWidgetLayoutAsset(
        const LoadWidgetLayoutAssetRequest& request) {
        LoadWidgetLayoutAssetResult result;
        result.Success = false;

        auto complete = [&request](const LoadWidgetLayoutAssetResult& callbackResult) {
            if (request.CompletionCallback) {
                request.CompletionCallback(callbackResult);
            }
        };

        if (request.LayoutSourcePath.empty()) {
            result.ErrorCode = UI_ASSET_AUTHORING_INVALID_REQUEST;
            result.Message = "LayoutSourcePath cannot be empty.";
            complete(result);
            return result;
        }

        const std::optional<std::filesystem::path> validatedLayoutPath =
            Security::PathValidator::ValidateAssetPath(request.LayoutSourcePath);
        if (!validatedLayoutPath.has_value()) {
            result.ErrorCode = UI_ASSET_AUTHORING_PATH_VALIDATION_FAILED;
            result.Message = "Layout source path validation failed.";
            complete(result);
            return result;
        }

        Json layoutPayload;
        std::string readError;
        if (!ReadJsonDocument(validatedLayoutPath.value(), layoutPayload, &readError)) {
            result.ErrorCode = UI_ASSET_AUTHORING_LAYOUT_PARSE_FAILED;
            result.Message = readError;
            complete(result);
            return result;
        }

        WidgetLayoutValidationResult validation;
        std::optional<WidgetLayoutAsset> parsedLayout =
            DeserializeWidgetLayoutAsset(layoutPayload, &validation);
        if (!parsedLayout.has_value()) {
            result.ErrorCode = validation.ErrorCode.empty()
                ? UI_ASSET_AUTHORING_LAYOUT_PARSE_FAILED
                : validation.ErrorCode;
            result.Message = validation.Message.empty()
                ? "Failed to deserialize widget layout."
                : validation.Message;
            result.Diagnostics = validation.Diagnostics;
            complete(result);
            return result;
        }

        result.Layout = std::move(parsedLayout.value());
        WidgetLayoutDependencySet dependencies = CollectWidgetLayoutDependencies(result.Layout);

        std::vector<std::pair<std::string, std::future<bool>>> dependencyChecks;
        dependencyChecks.reserve(
            dependencies.BlueprintDependencies.size() + dependencies.StyleDependencies.size());

        for (const std::string& blueprintId : dependencies.BlueprintDependencies) {
            dependencyChecks.emplace_back(
                blueprintId,
                std::async(std::launch::async, [this, blueprintId]() {
                    return m_KnownBlueprints.find(blueprintId) != m_KnownBlueprints.end();
                }));
        }

        for (const std::string& styleDependency : dependencies.StyleDependencies) {
            dependencyChecks.emplace_back(
                styleDependency,
                std::async(std::launch::async, [styleDependency]() {
                    return StyleDependencyExists(styleDependency);
                }));
        }

        for (auto& dependencyCheck : dependencyChecks) {
            if (!dependencyCheck.second.get()) {
                result.MissingDependencies.push_back(dependencyCheck.first);
            }
        }

        if (!result.MissingDependencies.empty()) {
            result.ErrorCode = UI_ASSET_AUTHORING_LAYOUT_DEPENDENCY_MISSING;
            result.Message = "Layout dependencies could not be resolved.";
            complete(result);
            return result;
        }

        for (const WidgetLayoutNode& node : result.Layout.Nodes) {
            std::optional<WidgetBlueprintAsset> resolvedBlueprint =
                ResolveBlueprintForLayoutNode(node, &result.Diagnostics);
            if (!resolvedBlueprint.has_value()) {
                result.ErrorCode = UI_ASSET_AUTHORING_LAYOUT_DEPENDENCY_MISSING;
                result.Message = "Failed to resolve blueprint inheritance chain for layout node.";
                complete(result);
                return result;
            }
            if (std::find(
                    result.ResolvedBlueprintIds.begin(),
                    result.ResolvedBlueprintIds.end(),
                    resolvedBlueprint->BlueprintId) == result.ResolvedBlueprintIds.end()) {
                result.ResolvedBlueprintIds.push_back(resolvedBlueprint->BlueprintId);
            }
        }

        m_KnownLayouts[result.Layout.LayoutId] = result.Layout;

        if (request.BuildPoolOnLoad && !BuildLayoutPool(result.Layout, &result.Diagnostics)) {
            result.ErrorCode = UI_ASSET_AUTHORING_LAYOUT_POOL_EMPTY;
            result.Message = "Failed to prewarm layout pool.";
            complete(result);
            return result;
        }

        result.Success = true;
        complete(result);
        return result;
    }

    std::optional<WidgetBlueprintAsset> UIAssetAuthoringService::FindBlueprintById(
        const std::string& blueprintId) const {
        const auto blueprintIt = m_KnownBlueprints.find(blueprintId);
        if (blueprintIt == m_KnownBlueprints.end()) {
            return std::nullopt;
        }
        return blueprintIt->second;
    }

    std::optional<WidgetLayoutAsset> UIAssetAuthoringService::FindLayoutById(
        const std::string& layoutId) const {
        const auto layoutIt = m_KnownLayouts.find(layoutId);
        if (layoutIt == m_KnownLayouts.end()) {
            return std::nullopt;
        }
        return layoutIt->second;
    }

    std::optional<LayoutWidgetInstanceHandle> UIAssetAuthoringService::CheckoutLayoutInstance(
        const std::string& layoutId,
        const std::string& nodeId) {
        const std::string poolKey = BuildLayoutPoolKey(layoutId, nodeId);
        auto poolIt = m_LayoutPools.find(poolKey);
        if (poolIt == m_LayoutPools.end()) {
            return std::nullopt;
        }

        for (LayoutWidgetInstance& instance : poolIt->second) {
            if (!instance.InUse) {
                instance.InUse = true;
                return LayoutWidgetInstanceHandle{layoutId, nodeId, instance.InstanceId};
            }
        }

        auto layoutIt = m_KnownLayouts.find(layoutId);
        if (layoutIt == m_KnownLayouts.end()) {
            return std::nullopt;
        }

        const WidgetLayoutNode* node = FindNodeById(layoutIt->second, nodeId);
        if (node == nullptr) {
            return std::nullopt;
        }

        LayoutWidgetInstance newInstance;
        newInstance.InstanceId = m_NextInstanceId++;
        newInstance.LayoutId = layoutId;
        newInstance.NodeId = nodeId;
        newInstance.RuntimeProperties = node->PropertyOverrides;
        newInstance.ResetPolicy = node->Pooling.ResetPolicy;
        newInstance.InUse = true;
        poolIt->second.push_back(newInstance);
        return LayoutWidgetInstanceHandle{layoutId, nodeId, newInstance.InstanceId};
    }

    bool UIAssetAuthoringService::CheckinLayoutInstance(const LayoutWidgetInstanceHandle& handle) {
        if (handle.LayoutId.empty() || handle.NodeId.empty() || handle.InstanceId == 0) {
            return false;
        }

        const std::string poolKey = BuildLayoutPoolKey(handle.LayoutId, handle.NodeId);
        auto poolIt = m_LayoutPools.find(poolKey);
        if (poolIt == m_LayoutPools.end()) {
            return false;
        }

        auto instanceIt = std::find_if(
            poolIt->second.begin(),
            poolIt->second.end(),
            [&handle](const LayoutWidgetInstance& instance) {
                return instance.InstanceId == handle.InstanceId;
            });
        if (instanceIt == poolIt->second.end()) {
            return false;
        }

        instanceIt->InUse = false;

        if (instanceIt->ResetPolicy == WidgetInstanceResetPolicy::ResetToLayoutDefaults) {
            auto layoutIt = m_KnownLayouts.find(handle.LayoutId);
            if (layoutIt == m_KnownLayouts.end()) {
                return false;
            }
            const WidgetLayoutNode* node = FindNodeById(layoutIt->second, handle.NodeId);
            if (node == nullptr) {
                return false;
            }
            instanceIt->RuntimeProperties = node->PropertyOverrides;
        }

        return true;
    }

    bool UIAssetAuthoringService::BuildLayoutPool(
        const WidgetLayoutAsset& layout,
        std::vector<std::string>* diagnostics) {

        const std::string layoutPrefix = layout.LayoutId + ":";
        for (auto poolIt = m_LayoutPools.begin(); poolIt != m_LayoutPools.end();) {
            if (poolIt->first.rfind(layoutPrefix, 0) == 0) {
                poolIt = m_LayoutPools.erase(poolIt);
            } else {
                ++poolIt;
            }
        }

        for (const WidgetLayoutNode& node : layout.Nodes) {
            const std::string poolKey = BuildLayoutPoolKey(layout.LayoutId, node.NodeId);
            std::vector<LayoutWidgetInstance>& pool = m_LayoutPools[poolKey];
            pool.reserve(node.Pooling.PrewarmCount);

            for (uint32_t index = 0; index < node.Pooling.PrewarmCount; ++index) {
                LayoutWidgetInstance instance;
                instance.InstanceId = m_NextInstanceId++;
                instance.LayoutId = layout.LayoutId;
                instance.NodeId = node.NodeId;
                instance.RuntimeProperties = node.PropertyOverrides;
                instance.ResetPolicy = node.Pooling.ResetPolicy;
                instance.InUse = false;
                pool.push_back(std::move(instance));
            }

            if (diagnostics != nullptr && node.Pooling.PrewarmCount > 0) {
                diagnostics->push_back(
                    "Prewarmed " + std::to_string(node.Pooling.PrewarmCount) +
                    " instance(s) for layout node '" + node.NodeId + "'.");
            }
        }

        return true;
    }

    std::optional<WidgetBlueprintAsset> UIAssetAuthoringService::ResolveBlueprintForLayoutNode(
        const WidgetLayoutNode& node,
        std::vector<std::string>* diagnostics) const {
        const auto startBlueprintIt = m_KnownBlueprints.find(node.BlueprintId);
        if (startBlueprintIt == m_KnownBlueprints.end()) {
            if (diagnostics != nullptr) {
                diagnostics->push_back(
                    "Layout node '" + node.NodeId +
                    "' references unknown blueprint '" + node.BlueprintId + "'.");
            }
            return std::nullopt;
        }

        std::vector<const WidgetBlueprintAsset*> chain;
        std::unordered_set<std::string> visited;
        std::string currentBlueprintId = node.BlueprintId;

        while (!currentBlueprintId.empty()) {
            if (!visited.insert(currentBlueprintId).second) {
                if (diagnostics != nullptr) {
                    diagnostics->push_back(
                        "Inheritance loop while resolving blueprint '" + currentBlueprintId + "'.");
                }
                return std::nullopt;
            }

            const auto blueprintIt = m_KnownBlueprints.find(currentBlueprintId);
            if (blueprintIt == m_KnownBlueprints.end()) {
                if (diagnostics != nullptr) {
                    diagnostics->push_back(
                        "Missing parent blueprint '" + currentBlueprintId +
                        "' for layout node '" + node.NodeId + "'.");
                }
                return std::nullopt;
            }

            chain.push_back(&blueprintIt->second);
            currentBlueprintId = blueprintIt->second.ParentBlueprintId;
        }

        std::reverse(chain.begin(), chain.end());

        WidgetBlueprintAsset resolved = *chain.back();
        resolved.StyleOverrides = Json::object();
        resolved.BindableProperties.clear();
        resolved.DefaultTransitions.clear();

        std::unordered_set<std::string> propertySet;
        std::unordered_map<std::string, WidgetBlueprintTransition> transitionMap;
        std::vector<std::string> transitionOrder;

        for (const WidgetBlueprintAsset* blueprint : chain) {
            resolved.StyleOverrides.update(blueprint->StyleOverrides, true);

            for (const std::string& bindableProperty : blueprint->BindableProperties) {
                if (propertySet.insert(bindableProperty).second) {
                    resolved.BindableProperties.push_back(bindableProperty);
                }
            }

            for (const WidgetBlueprintTransition& transition : blueprint->DefaultTransitions) {
                if (transitionMap.find(transition.TransitionId) == transitionMap.end()) {
                    transitionOrder.push_back(transition.TransitionId);
                }
                transitionMap[transition.TransitionId] = transition;
            }
        }

        for (const std::string& transitionId : transitionOrder) {
            resolved.DefaultTransitions.push_back(transitionMap.at(transitionId));
        }

        return resolved;
    }

} // namespace Authoring
} // namespace UI
} // namespace Core

