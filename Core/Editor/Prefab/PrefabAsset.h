#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Editor {

    using EditorJson = nlohmann::json;

    struct PrefabAssetData {
        std::string Guid;
        std::string DisplayName;
        std::string SourcePath;
        uint32_t SchemaVersion = 1;
        EditorJson Payload = EditorJson::object();
    };

    struct PrefabVariantData {
        std::string Guid;
        std::string ParentPrefabGuid;
        std::string DisplayName;
        std::string SourcePath;
        uint32_t SchemaVersion = 1;
        EditorJson Overrides = EditorJson::array();
    };

    struct PrefabRuntimeCache {
        std::unordered_map<std::string, PrefabAssetData> Assets;
        std::unordered_map<std::string, PrefabVariantData> Variants;
        std::unordered_map<std::string, std::string> InstanceToPrefab;
    };

} // namespace Editor
} // namespace Core

