#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"
#include "Core/Renderer/Mesh.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Core {
namespace Asset {
class AssetPipeline;
} // namespace Asset
} // namespace Core

namespace Core::Animation {

constexpr uint32_t MOTION_DATABASE_SCHEMA_VERSION = 1;

struct MotionPoseRecord {
    std::string PoseId;
    std::string ClipName;
    uint32_t FrameIndex = 0;
    float SampleTimeSec = 0.0f;
    std::vector<float> Features;
    bool LeftFootContact = false;
    bool RightFootContact = false;
};

struct MotionMatchingDatabaseAsset {
    uint32_t SchemaVersion = MOTION_DATABASE_SCHEMA_VERSION;
    std::string DatabaseId;
    uint32_t FeatureDimension = 0;
    std::vector<MotionPoseRecord> Poses;
};

struct MotionDatabaseBuildClipInput {
    const Renderer::AnimationClip* Clip = nullptr;
    std::string ClipNameOverride;
    float SampleStepSec = 1.0f / 30.0f;
};

struct MotionDatabaseBuildRequest {
    std::string DatabaseId;
    uint32_t FeatureDimension = 8;
    std::vector<MotionDatabaseBuildClipInput> Clips;
    bool NormalizeFeatures = true;
};

struct MotionDatabaseBuildResult {
    bool Success = false;
    MotionMatchingDatabaseAsset Database;
    std::vector<AnimationDiagnostic> Diagnostics;
};

MotionDatabaseBuildResult BuildMotionMatchingDatabase(
    const MotionDatabaseBuildRequest& request);

bool ValidateMotionMatchingDatabase(
    const MotionMatchingDatabaseAsset& database,
    std::vector<AnimationDiagnostic>& diagnostics);

nlohmann::json SerializeMotionMatchingDatabase(
    const MotionMatchingDatabaseAsset& database);

std::optional<MotionMatchingDatabaseAsset> DeserializeMotionMatchingDatabase(
    const nlohmann::json& document,
    std::vector<AnimationDiagnostic>& diagnostics);

void RegisterMotionDatabaseDependencies(
    Asset::AssetPipeline& pipeline,
    const std::string& databaseAssetPath,
    const std::vector<std::string>& sourceClipPaths);

} // namespace Core::Animation

