#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace Core {
namespace ECS {

    struct PrefabInstanceComponent {
        std::string PrefabGuid;
        std::string InstanceGuid;
        nlohmann::json Overrides = nlohmann::json::array();
        bool HasLocalOverrides = false;
    };

} // namespace ECS
} // namespace Core

