#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"

namespace Core::Animation {

AnimationBlendTreeBuildResult CreateAnimationBlendTree(
    const AnimationBlendTreeBuildRequest& request);

} // namespace Core::Animation
