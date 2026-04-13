#pragma once

#include "Core/Renderer/Mesh.h"

#include <cstdint>
#include <vector>

namespace Core::Animation {

struct PoseFeatureExtractorSettings {
    uint32_t FeatureDimension = 8;
    bool IncludeVelocity = true;
    bool IncludeContacts = true;
};

std::vector<float> ExtractPoseFeatures(
    const Renderer::AnimationClip& clip,
    float sampleTimeSec,
    const PoseFeatureExtractorSettings& settings);

std::vector<std::vector<float>> ExtractPoseFeaturesForClip(
    const Renderer::AnimationClip& clip,
    float sampleStepSec,
    const PoseFeatureExtractorSettings& settings);

} // namespace Core::Animation

