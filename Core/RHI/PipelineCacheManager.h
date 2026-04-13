#pragma once

#include "Core/RHI/ShaderPermutationLibrary.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace Core {
namespace RHI {

    enum class PipelineWarmupMode : uint8_t {
        Startup = 0,
        Background = 1,
        OnDemand = 2
    };

    struct PipelineCacheMetadata {
        uint32_t SchemaVersion = 1;
        std::string DeviceFingerprint;
        std::string DriverFingerprint;
        std::string ShaderLibraryDigest;
        uint64_t LastUpdatedEpochSeconds = 0;
        uint64_t CacheBlobSizeBytes = 0;
    };

    struct PipelineWarmupRequest {
        std::string SessionKey = "default";
        ShaderPermutationLibraryResult PermutationLibrary;
        std::vector<std::string> WarmupList;
        std::unordered_set<std::string> ExistingPipelineKeys;
        PipelineWarmupMode Mode = PipelineWarmupMode::Startup;
        double PerFrameBudgetMs = 2.0;
        bool PersistCache = true;
    };

    struct PipelineWarmupResult {
        bool Completed = false;
        uint32_t TotalCandidates = 0;
        uint32_t ProcessedThisCall = 0;
        uint32_t CompletedCandidates = 0;
        uint32_t RemainingCandidates = 0;
        uint32_t CacheHitCount = 0;
        uint32_t CacheMissCount = 0;
        std::vector<std::string> ProcessedPipelineKeys;
        std::vector<std::string> Diagnostics;
    };

    class PipelineCacheManager {
    public:
        static constexpr uint32_t kSchemaVersion = 1;

        static std::string BuildDeviceFingerprint(
            uint32_t vendorId,
            uint32_t deviceId,
            uint32_t driverVersion,
            uint32_t apiVersion);

        static bool ShouldInvalidate(
            const PipelineCacheMetadata& cachedMetadata,
            const PipelineCacheMetadata& runtimeMetadata,
            std::string* reason);

        static Result<PipelineCacheMetadata> LoadMetadata(const std::filesystem::path& metadataPath);
        static Result<void> SaveMetadata(const std::filesystem::path& metadataPath, const PipelineCacheMetadata& metadata);
        static Result<std::vector<uint8_t>> LoadCacheBlob(const std::filesystem::path& blobPath);
        static Result<void> SaveCacheBlob(const std::filesystem::path& blobPath, const std::vector<uint8_t>& blob);
    };

    Result<PipelineWarmupResult> WarmupPipelineCache(const PipelineWarmupRequest& request);

} // namespace RHI
} // namespace Core

