#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"

#include <vector>

namespace Core::Animation {

bool ValidateAnimationGraphBuildRequest(
    const AnimationGraphBuildRequest& request,
    std::vector<AnimationDiagnostic>& diagnostics);

} // namespace Core::Animation
