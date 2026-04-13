#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"

#include <array>
#include <vector>

namespace Core::Animation {

std::vector<MotionTrajectorySample> PredictTrajectorySamples(
    const std::array<float, 3>& desiredVelocity,
    float horizonSec = 0.6f,
    float stepSec = 0.2f);

std::vector<float> BuildMotionQueryFeatureVector(
    const MotionMatchingQuery& query,
    uint32_t featureDimension);

} // namespace Core::Animation

