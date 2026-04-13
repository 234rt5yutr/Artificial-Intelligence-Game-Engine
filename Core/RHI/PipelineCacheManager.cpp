#include "Core/RHI/PipelineCacheManager.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Core {
namespace RHI {

    namespace {
        struct WarmupSessionState {
            std::vector<std::string> Candidates;
            size_t NextIndex = 0;
            uint32_t CacheHitCount = 0;
            uint32_t CacheMissCount = 0;
            std::unordered_set<std::string> WarmedPipelines;
        };

        std::unordered_map<std::string, WarmupSessionState> g_WarmupSessions;

        uint32_t ComputeBudgetedCount(PipelineWarmupMode mode, double perFrameBudgetMs, uint32_t totalRemaining) {
            const double clampedBudget = perFrameBudgetMs > 0.0 ? perFrameBudgetMs : 1.0;
            uint32_t requestedCount = static_cast<uint32_t>(clampedBudget);
            if (requestedCount == 0u) {
                requestedCount = 1u;
            }

            switch (mode) {
                case PipelineWarmupMode::Startup: {
                    requestedCount = std::max<uint32_t>(requestedCount, 8u);
                    break;
                }
                case PipelineWarmupMode::Background: {
                    requestedCount = std::max<uint32_t>(requestedCount, 2u);
                    break;
                }
                case PipelineWarmupMode::OnDemand: {
                    requestedCount = 1u;
                    break;
                }
            }

            if (requestedCount > totalRemaining) {
                requestedCount = totalRemaining;
            }
            return requestedCount;
        }

        std::vector<std::string> BuildCandidateList(const PipelineWarmupRequest& request) {
            std::unordered_set<std::string> uniqueKeys;
            std::vector<std::string> candidates;

            for (const std::string& explicitWarmup : request.WarmupList) {
                if (uniqueKeys.insert(explicitWarmup).second) {
                    candidates.push_back(explicitWarmup);
                }
            }

            for (const auto& artifact : request.PermutationLibrary.Artifacts) {
                if (uniqueKeys.insert(artifact.PermutationKey).second) {
                    candidates.push_back(artifact.PermutationKey);
                }
            }

            std::sort(candidates.begin(), candidates.end());
            return candidates;
        }
    } // namespace

    std::string PipelineCacheManager::BuildDeviceFingerprint(
        uint32_t vendorId,
        uint32_t deviceId,
        uint32_t driverVersion,
        uint32_t apiVersion) {
        std::ostringstream stream;
        stream << "vendor=" << vendorId
               << ";device=" << deviceId
               << ";driver=" << driverVersion
               << ";api=" << apiVersion;
        return stream.str();
    }

    bool PipelineCacheManager::ShouldInvalidate(
        const PipelineCacheMetadata& cachedMetadata,
        const PipelineCacheMetadata& runtimeMetadata,
        std::string* reason) {
        if (cachedMetadata.SchemaVersion != runtimeMetadata.SchemaVersion) {
            if (reason != nullptr) {
                *reason = "schema mismatch";
            }
            return true;
        }

        if (cachedMetadata.DeviceFingerprint != runtimeMetadata.DeviceFingerprint) {
            if (reason != nullptr) {
                *reason = "device fingerprint mismatch";
            }
            return true;
        }

        if (cachedMetadata.DriverFingerprint != runtimeMetadata.DriverFingerprint) {
            if (reason != nullptr) {
                *reason = "driver fingerprint mismatch";
            }
            return true;
        }

        if (cachedMetadata.ShaderLibraryDigest != runtimeMetadata.ShaderLibraryDigest) {
            if (reason != nullptr) {
                *reason = "shader hash mismatch";
            }
            return true;
        }

        return false;
    }

    Result<PipelineCacheMetadata> PipelineCacheManager::LoadMetadata(const std::filesystem::path& metadataPath) {
        std::ifstream input(metadataPath);
        if (!input.is_open()) {
            return Result<PipelineCacheMetadata>::Failure("Unable to open metadata file: " + metadataPath.string());
        }

        PipelineCacheMetadata metadata{};
        std::string line;
        while (std::getline(input, line)) {
            const size_t split = line.find('=');
            if (split == std::string::npos) {
                continue;
            }

            const std::string key = line.substr(0, split);
            const std::string value = line.substr(split + 1u);
            if (key == "schema") {
                metadata.SchemaVersion = static_cast<uint32_t>(std::stoul(value));
            } else if (key == "device") {
                metadata.DeviceFingerprint = value;
            } else if (key == "driver") {
                metadata.DriverFingerprint = value;
            } else if (key == "shader_digest") {
                metadata.ShaderLibraryDigest = value;
            } else if (key == "updated") {
                metadata.LastUpdatedEpochSeconds = static_cast<uint64_t>(std::stoull(value));
            } else if (key == "blob_size") {
                metadata.CacheBlobSizeBytes = static_cast<uint64_t>(std::stoull(value));
            }
        }

        return Result<PipelineCacheMetadata>::Success(std::move(metadata));
    }

    Result<void> PipelineCacheManager::SaveMetadata(const std::filesystem::path& metadataPath, const PipelineCacheMetadata& metadata) {
        std::filesystem::create_directories(metadataPath.parent_path());

        std::ofstream output(metadataPath, std::ios::trunc);
        if (!output.is_open()) {
            return Result<void>::Failure("Unable to write metadata file: " + metadataPath.string());
        }

        output << "schema=" << metadata.SchemaVersion << "\n";
        output << "device=" << metadata.DeviceFingerprint << "\n";
        output << "driver=" << metadata.DriverFingerprint << "\n";
        output << "shader_digest=" << metadata.ShaderLibraryDigest << "\n";
        output << "updated=" << metadata.LastUpdatedEpochSeconds << "\n";
        output << "blob_size=" << metadata.CacheBlobSizeBytes << "\n";
        output.close();

        return Result<void>::Success();
    }

    Result<std::vector<uint8_t>> PipelineCacheManager::LoadCacheBlob(const std::filesystem::path& blobPath) {
        std::ifstream input(blobPath, std::ios::binary | std::ios::ate);
        if (!input.is_open()) {
            return Result<std::vector<uint8_t>>::Failure("Unable to open cache blob file: " + blobPath.string());
        }

        const std::streamsize size = input.tellg();
        if (size <= 0) {
            return Result<std::vector<uint8_t>>::Failure("Cache blob file is empty: " + blobPath.string());
        }

        std::vector<uint8_t> blob(static_cast<size_t>(size));
        input.seekg(0, std::ios::beg);
        input.read(reinterpret_cast<char*>(blob.data()), size);
        input.close();
        return Result<std::vector<uint8_t>>::Success(std::move(blob));
    }

    Result<void> PipelineCacheManager::SaveCacheBlob(const std::filesystem::path& blobPath, const std::vector<uint8_t>& blob) {
        std::filesystem::create_directories(blobPath.parent_path());

        std::ofstream output(blobPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return Result<void>::Failure("Unable to write cache blob file: " + blobPath.string());
        }

        output.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
        output.close();
        return Result<void>::Success();
    }

    Result<PipelineWarmupResult> WarmupPipelineCache(const PipelineWarmupRequest& request) {
        PipelineWarmupResult result{};
        WarmupSessionState& session = g_WarmupSessions[request.SessionKey];

        const std::vector<std::string> requestedCandidates = BuildCandidateList(request);
        if (session.Candidates != requestedCandidates) {
            session.Candidates = requestedCandidates;
            session.NextIndex = 0u;
            session.CacheHitCount = 0u;
            session.CacheMissCount = 0u;
            session.WarmedPipelines.clear();
        }

        result.TotalCandidates = static_cast<uint32_t>(session.Candidates.size());
        if (session.Candidates.empty()) {
            result.Completed = true;
            return Result<PipelineWarmupResult>::Success(std::move(result));
        }

        const uint32_t remaining = static_cast<uint32_t>(session.Candidates.size() - session.NextIndex);
        const uint32_t budgetedCount = ComputeBudgetedCount(request.Mode, request.PerFrameBudgetMs, remaining);

        for (uint32_t index = 0; index < budgetedCount; ++index) {
            const std::string& pipelineKey = session.Candidates[session.NextIndex];
            result.ProcessedPipelineKeys.push_back(pipelineKey);

            const bool cacheHit = (request.ExistingPipelineKeys.find(pipelineKey) != request.ExistingPipelineKeys.end()) ||
                                  (session.WarmedPipelines.find(pipelineKey) != session.WarmedPipelines.end());
            if (cacheHit) {
                ++session.CacheHitCount;
            } else {
                ++session.CacheMissCount;
            }

            session.WarmedPipelines.insert(pipelineKey);
            ++session.NextIndex;
        }

        result.ProcessedThisCall = budgetedCount;
        result.CompletedCandidates = static_cast<uint32_t>(session.NextIndex);
        result.RemainingCandidates = static_cast<uint32_t>(session.Candidates.size() - session.NextIndex);
        result.CacheHitCount = session.CacheHitCount;
        result.CacheMissCount = session.CacheMissCount;
        result.Completed = result.RemainingCandidates == 0u;

        if (!result.Completed) {
            result.Diagnostics.push_back("Warmup budget exhausted; continuing on a future frame.");
        }

        if (request.PersistCache) {
            result.Diagnostics.push_back("Warmup session configured for persistence.");
        }

        return Result<PipelineWarmupResult>::Success(std::move(result));
    }

} // namespace RHI
} // namespace Core

