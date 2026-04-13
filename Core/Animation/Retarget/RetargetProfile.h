#pragma once

#include "Core/Animation/Retarget/RetargetTypes.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <vector>

namespace Core::Animation {

RetargetProfileValidationResult ValidateRetargetProfile(
    const RetargetProfileDefinition& profile);

RetargetProfileDefinition CanonicalizeRetargetProfile(
    const RetargetProfileDefinition& profile);

nlohmann::json SerializeRetargetProfile(
    const RetargetProfileDefinition& profile);

std::optional<RetargetProfileDefinition> DeserializeRetargetProfile(
    const nlohmann::json& document,
    std::vector<AnimationDiagnostic>& diagnostics);

} // namespace Core::Animation

