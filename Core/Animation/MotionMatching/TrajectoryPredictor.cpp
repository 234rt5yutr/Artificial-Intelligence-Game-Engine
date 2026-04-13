#include "Core/Animation/MotionMatching/TrajectoryPredictor.h"

#include <algorithm>
#include <cmath>

namespace Core::Animation {

std::vector<MotionTrajectorySample> PredictTrajectorySamples(
    const std::array<float, 3>& desiredVelocity,
    const float horizonSec,
    const float stepSec) {
    std::vector<MotionTrajectorySample> samples;
    const float clampedStep = std::max(stepSec, 0.01f);
    const float clampedHorizon = std::max(horizonSec, clampedStep);
    const uint32_t sampleCount = std::max(
        1U,
        static_cast<uint32_t>(std::floor(clampedHorizon / clampedStep)));

    samples.reserve(sampleCount);
    for (uint32_t index = 1; index <= sampleCount; ++index) {
        const float time = clampedStep * static_cast<float>(index);
        MotionTrajectorySample sample;
        sample.Time = time;
        sample.PosX = desiredVelocity[0] * time;
        sample.PosZ = desiredVelocity[2] * time;
        sample.FacingYaw = std::atan2(desiredVelocity[0], desiredVelocity[2]);
        samples.push_back(sample);
    }
    return samples;
}

std::vector<float> BuildMotionQueryFeatureVector(
    const MotionMatchingQuery& query,
    const uint32_t featureDimension) {
    std::vector<float> features(featureDimension, 0.0f);
    if (featureDimension == 0) {
        return features;
    }

    features[0] = query.DesiredVelocity[0];
    if (featureDimension > 1) {
        features[1] = query.DesiredVelocity[1];
    }
    if (featureDimension > 2) {
        features[2] = query.DesiredVelocity[2];
    }

    std::vector<MotionTrajectorySample> trajectory = query.Trajectory;
    if (trajectory.empty()) {
        trajectory = PredictTrajectorySamples(query.DesiredVelocity);
    }

    float avgPosX = 0.0f;
    float avgPosZ = 0.0f;
    float avgYaw = 0.0f;
    for (const MotionTrajectorySample& sample : trajectory) {
        avgPosX += sample.PosX;
        avgPosZ += sample.PosZ;
        avgYaw += sample.FacingYaw;
    }
    if (!trajectory.empty()) {
        const float reciprocal = 1.0f / static_cast<float>(trajectory.size());
        avgPosX *= reciprocal;
        avgPosZ *= reciprocal;
        avgYaw *= reciprocal;
    }

    if (featureDimension > 3) {
        features[3] = avgPosX;
    }
    if (featureDimension > 4) {
        features[4] = avgPosZ;
    }
    if (featureDimension > 5) {
        features[5] = avgYaw;
    }
    if (featureDimension > 6) {
        features[6] = static_cast<float>(trajectory.size());
    }
    if (featureDimension > 7) {
        features[7] = query.ContinuityWeight;
    }
    return features;
}

} // namespace Core::Animation

