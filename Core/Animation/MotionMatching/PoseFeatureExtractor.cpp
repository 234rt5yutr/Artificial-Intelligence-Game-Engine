#include "Core/Animation/MotionMatching/PoseFeatureExtractor.h"

#include <algorithm>
#include <cmath>

namespace Core::Animation {

std::vector<float> ExtractPoseFeatures(
    const Renderer::AnimationClip& clip,
    const float sampleTimeSec,
    const PoseFeatureExtractorSettings& settings) {
    std::vector<float> features = Renderer::ExtractMotionFeatureVector(
        clip,
        sampleTimeSec,
        settings.FeatureDimension);
    if (features.empty()) {
        return features;
    }

    if (settings.IncludeVelocity && settings.FeatureDimension > 0U) {
        const float duration = std::max(clip.Duration, 0.0001f);
        const float normalizedVelocity = std::clamp(sampleTimeSec / duration, 0.0f, 1.0f);
        features[0] = normalizedVelocity;
    }

    if (settings.IncludeContacts && settings.FeatureDimension > 7U) {
        features[7] = std::fmod(sampleTimeSec, 0.4f) < 0.2f ? 1.0f : 0.0f;
    }

    return features;
}

std::vector<std::vector<float>> ExtractPoseFeaturesForClip(
    const Renderer::AnimationClip& clip,
    const float sampleStepSec,
    const PoseFeatureExtractorSettings& settings) {
    std::vector<std::vector<float>> featureFrames;
    const float duration = std::max(0.0f, clip.Duration);
    const float step = sampleStepSec > 0.0f ? sampleStepSec : (1.0f / 30.0f);

    for (float sampleTime = 0.0f; sampleTime <= duration + 0.0001f; sampleTime += step) {
        featureFrames.push_back(ExtractPoseFeatures(clip, sampleTime, settings));
    }

    if (featureFrames.empty()) {
        featureFrames.push_back(ExtractPoseFeatures(clip, 0.0f, settings));
    }
    return featureFrames;
}

} // namespace Core::Animation

