#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"

namespace Core::Animation {

AnimationGraphBuildResult CreateAnimationStateMachineGraph(
    const AnimationGraphBuildRequest& request);

} // namespace Core::Animation
