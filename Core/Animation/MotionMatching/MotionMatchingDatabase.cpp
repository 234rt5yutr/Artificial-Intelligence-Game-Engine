#include "Core/Animation/MotionMatching/MotionMatchingDatabase.h"

#include "Core/Animation/MotionMatching/PoseFeatureExtractor.h"
#include "Core/Asset/AssetPipeline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace Core::Animation {

namespace {

void PushDiagnostic(std::vector<AnimationDiagnostic>& diagnostics,
                    const std::string& code,
                    const std::string& message,
                    const DiagnosticSeverity severity,
                    const std::string& context = {}) {
    AnimationDiagnostic diagnostic;
    diagnostic.Code = code;
    diagnostic.Message = message;
    diagnostic.Severity = severity;
    diagnostic.Context = context;
    diagnostics.push_back(std::move(diagnostic));
}

bool HasError(const std::vector<AnimationDiagnostic>& diagnostics) {
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const AnimationDiagnostic& diagnostic) {
            return diagnostic.Severity == DiagnosticSeverity::Error;
        });
}

void NormalizePoseFeatures(MotionMatchingDatabaseAsset& database) {
    if (database.Poses.empty() || database.FeatureDimension == 0) {
        return;
    }

    std::vector<float> minValues(
        database.FeatureDimension,
        std::numeric_limits<float>::max());
    std::vector<float> maxValues(
        database.FeatureDimension,
        std::numeric_limits<float>::lowest());

    for (const MotionPoseRecord& pose : database.Poses) {
        for (uint32_t index = 0; index < database.FeatureDimension; ++index) {
            minValues[index] = std::min(minValues[index], pose.Features[index]);
            maxValues[index] = std::max(maxValues[index], pose.Features[index]);
        }
    }

    for (MotionPoseRecord& pose : database.Poses) {
        for (uint32_t index = 0; index < database.FeatureDimension; ++index) {
            const float range = maxValues[index] - minValues[index];
            if (range > 0.00001f) {
                pose.Features[index] = (pose.Features[index] - minValues[index]) / range;
            } else {
                pose.Features[index] = 0.0f;
            }
        }
    }
}

} // namespace

MotionDatabaseBuildResult BuildMotionMatchingDatabase(
    const MotionDatabaseBuildRequest& request) {
    MotionDatabaseBuildResult result;

    if (request.DatabaseId.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_MOTION_DB_MISSING",
            "Motion database build requires DatabaseId.",
            DiagnosticSeverity::Error);
    }
    if (request.FeatureDimension == 0) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_MOTION_FEATURE_DIMENSION_MISMATCH",
            "Motion database FeatureDimension must be greater than zero.",
            DiagnosticSeverity::Error);
    }
    if (request.Clips.empty()) {
        PushDiagnostic(
            result.Diagnostics,
            "ANIM_MOTION_DB_MISSING",
            "Motion database build requires at least one clip input.",
            DiagnosticSeverity::Error);
    }

    if (HasError(result.Diagnostics)) {
        result.Success = false;
        return result;
    }

    MotionMatchingDatabaseAsset database;
    database.SchemaVersion = MOTION_DATABASE_SCHEMA_VERSION;
    database.DatabaseId = request.DatabaseId;
    database.FeatureDimension = request.FeatureDimension;

    for (const MotionDatabaseBuildClipInput& clipInput : request.Clips) {
        if (clipInput.Clip == nullptr) {
            PushDiagnostic(
                result.Diagnostics,
                "ANIM_MOTION_DB_CLIP_MISSING",
                "Motion database clip input was null and was skipped.",
                DiagnosticSeverity::Warning,
                request.DatabaseId);
            continue;
        }

        const Renderer::AnimationClip& clip = *clipInput.Clip;
        const std::string clipName =
            clipInput.ClipNameOverride.empty() ? clip.Name : clipInput.ClipNameOverride;
        const float duration = std::max(0.0f, clip.Duration);
        const float sampleStep = clipInput.SampleStepSec > 0.0f
            ? clipInput.SampleStepSec
            : (1.0f / 30.0f);

        uint32_t frameIndex = 0;
        for (float sampleTime = 0.0f; sampleTime <= duration + 0.0001f; sampleTime += sampleStep) {
            MotionPoseRecord pose;
            pose.ClipName = clipName;
            pose.FrameIndex = frameIndex;
            pose.SampleTimeSec = std::min(sampleTime, duration);
            pose.PoseId =
                clipName + "#" + std::to_string(frameIndex);
            pose.Features = ExtractPoseFeatures(
                clip,
                pose.SampleTimeSec,
                PoseFeatureExtractorSettings{request.FeatureDimension, true, true});
            pose.LeftFootContact = std::fmod(pose.SampleTimeSec, 0.4f) < 0.2f;
            pose.RightFootContact = !pose.LeftFootContact;
            database.Poses.push_back(std::move(pose));
            ++frameIndex;
        }
    }

    std::sort(
        database.Poses.begin(),
        database.Poses.end(),
        [](const MotionPoseRecord& lhs, const MotionPoseRecord& rhs) {
            if (lhs.ClipName != rhs.ClipName) {
                return lhs.ClipName < rhs.ClipName;
            }
            if (lhs.FrameIndex != rhs.FrameIndex) {
                return lhs.FrameIndex < rhs.FrameIndex;
            }
            return lhs.PoseId < rhs.PoseId;
        });

    if (request.NormalizeFeatures) {
        NormalizePoseFeatures(database);
    }

    std::vector<AnimationDiagnostic> validationDiagnostics;
    const bool isValid = ValidateMotionMatchingDatabase(database, validationDiagnostics);
    result.Diagnostics.insert(
        result.Diagnostics.end(),
        validationDiagnostics.begin(),
        validationDiagnostics.end());
    result.Database = std::move(database);
    result.Success = isValid && !HasError(result.Diagnostics);
    return result;
}

bool ValidateMotionMatchingDatabase(
    const MotionMatchingDatabaseAsset& database,
    std::vector<AnimationDiagnostic>& diagnostics) {
    diagnostics.clear();

    if (database.DatabaseId.empty()) {
        PushDiagnostic(
            diagnostics,
            "ANIM_MOTION_DB_MISSING",
            "Motion database id must not be empty.",
            DiagnosticSeverity::Error);
    }
    if (database.FeatureDimension == 0) {
        PushDiagnostic(
            diagnostics,
            "ANIM_MOTION_FEATURE_DIMENSION_MISMATCH",
            "Motion database feature dimension must be greater than zero.",
            DiagnosticSeverity::Error,
            database.DatabaseId);
    }
    if (database.Poses.empty()) {
        PushDiagnostic(
            diagnostics,
            "ANIM_MOTION_DB_MISSING",
            "Motion database must include at least one pose.",
            DiagnosticSeverity::Error,
            database.DatabaseId);
    }

    std::unordered_set<std::string> poseIds;
    for (const MotionPoseRecord& pose : database.Poses) {
        if (pose.Features.size() != database.FeatureDimension) {
            PushDiagnostic(
                diagnostics,
                "ANIM_MOTION_FEATURE_DIMENSION_MISMATCH",
                "Pose feature vector does not match database feature dimension.",
                DiagnosticSeverity::Error,
                pose.PoseId);
        }
        if (!poseIds.insert(pose.PoseId).second) {
            PushDiagnostic(
                diagnostics,
                "ANIM_MOTION_POSE_ID_DUPLICATE",
                "Duplicate pose id detected in motion database.",
                DiagnosticSeverity::Error,
                pose.PoseId);
        }
    }

    if (!HasError(diagnostics)) {
        PushDiagnostic(
            diagnostics,
            "ANIM_MOTION_DB_VALIDATED",
            "Motion database validated successfully.",
            DiagnosticSeverity::Info,
            database.DatabaseId);
    }
    return !HasError(diagnostics);
}

nlohmann::json SerializeMotionMatchingDatabase(
    const MotionMatchingDatabaseAsset& database) {
    nlohmann::json document = nlohmann::json::object();
    document["schemaVersion"] = database.SchemaVersion;
    document["databaseId"] = database.DatabaseId;
    document["featureDimension"] = database.FeatureDimension;

    nlohmann::json poses = nlohmann::json::array();
    for (const MotionPoseRecord& pose : database.Poses) {
        nlohmann::json jsonPose = nlohmann::json::object();
        jsonPose["poseId"] = pose.PoseId;
        jsonPose["clipName"] = pose.ClipName;
        jsonPose["frameIndex"] = pose.FrameIndex;
        jsonPose["sampleTimeSec"] = pose.SampleTimeSec;
        jsonPose["features"] = pose.Features;
        jsonPose["leftFootContact"] = pose.LeftFootContact;
        jsonPose["rightFootContact"] = pose.RightFootContact;
        poses.push_back(std::move(jsonPose));
    }
    document["poses"] = std::move(poses);
    return document;
}

std::optional<MotionMatchingDatabaseAsset> DeserializeMotionMatchingDatabase(
    const nlohmann::json& document,
    std::vector<AnimationDiagnostic>& diagnostics) {
    diagnostics.clear();
    if (!document.is_object()) {
        PushDiagnostic(
            diagnostics,
            "ANIM_MOTION_DB_MISSING",
            "Motion database document must be a JSON object.",
            DiagnosticSeverity::Error);
        return std::nullopt;
    }

    MotionMatchingDatabaseAsset database;
    database.SchemaVersion = document.value("schemaVersion", MOTION_DATABASE_SCHEMA_VERSION);
    database.DatabaseId = document.value("databaseId", std::string{});
    database.FeatureDimension = document.value("featureDimension", 0U);

    if (document.contains("poses") && document["poses"].is_array()) {
        for (const nlohmann::json& jsonPose : document["poses"]) {
            MotionPoseRecord pose;
            pose.PoseId = jsonPose.value("poseId", std::string{});
            pose.ClipName = jsonPose.value("clipName", std::string{});
            pose.FrameIndex = jsonPose.value("frameIndex", 0U);
            pose.SampleTimeSec = jsonPose.value("sampleTimeSec", 0.0f);
            if (jsonPose.contains("features") && jsonPose["features"].is_array()) {
                pose.Features = jsonPose["features"].get<std::vector<float>>();
            }
            pose.LeftFootContact = jsonPose.value("leftFootContact", false);
            pose.RightFootContact = jsonPose.value("rightFootContact", false);
            database.Poses.push_back(std::move(pose));
        }
    }

    if (!ValidateMotionMatchingDatabase(database, diagnostics)) {
        return std::nullopt;
    }
    return database;
}

void RegisterMotionDatabaseDependencies(
    Asset::AssetPipeline& pipeline,
    const std::string& databaseAssetPath,
    const std::vector<std::string>& sourceClipPaths) {
    std::vector<std::string> uniquePaths = sourceClipPaths;
    std::sort(uniquePaths.begin(), uniquePaths.end());
    uniquePaths.erase(
        std::unique(uniquePaths.begin(), uniquePaths.end()),
        uniquePaths.end());

    for (const std::string& clipPath : uniquePaths) {
        pipeline.RegisterAssetDependencyByPath(databaseAssetPath, clipPath);
    }
}

} // namespace Core::Animation

