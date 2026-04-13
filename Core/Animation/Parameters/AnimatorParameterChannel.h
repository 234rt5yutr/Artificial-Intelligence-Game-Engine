#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"

namespace Core {
namespace ECS {
struct AnimatorComponent;
} // namespace ECS
} // namespace Core

namespace Core::Animation {

AnimatorParameterSetResult SetAnimatorParameterValue(
    ECS::AnimatorComponent& animator,
    const AnimatorParameterSetRequest& request);

} // namespace Core::Animation
