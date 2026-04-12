#pragma once

#include "AddressablesCatalogTypes.h"
#include "Core/Asset/AssetLoader.h"

#include <cstdint>
#include <filesystem>
#include <future>
#include <string>

namespace Core {
namespace Asset {
namespace Addressables {

    enum class AddressableLoadStatus : uint8_t {
        Success = 0,
        KeyNotFound,
        KeyAmbiguous,
        TypeMismatch,
        DependencyMissing,
        DependencyCycleDetected,
        Cancelled,
        LoadFailed
    };

    enum class AddressableReleasePolicy : uint8_t {
        Immediate = 0,
        Deferred,
        MemoryPressure
    };

    struct LoadAddressableAssetRequest {
        std::string AddressKey;
        AssetType ExpectedType = AssetType::Unknown;
        bool IncludeTransitiveDependencies = true;
        std::string CancellationToken;
    };

    struct AddressableLoadResult {
        AddressableLoadStatus Status = AddressableLoadStatus::LoadFailed;
        std::string Error;
        std::string AddressKey;
        std::filesystem::path ResolvedCookedPath;
        AssetType LoadedType = AssetType::Unknown;
        bool IsFromCache = false;

        LoadedTexture Texture;
        LoadedMesh Mesh;
        LoadedShader Shader;
        LoadedStructuredAsset StructuredAsset;

        bool IsValid() const {
            return Status == AddressableLoadStatus::Success;
        }
    };

    struct AddressableLoadTicket {
        uint64_t TicketId = 0;
        std::string AddressKey;
        bool SharedInFlightTicket = false;
        std::shared_future<AddressableLoadResult> Future;
    };

    struct ReleaseAddressableAssetRequest {
        std::string AddressKey;
        AddressableReleasePolicy Policy = AddressableReleasePolicy::Deferred;
    };

    struct ReleaseAddressableAssetResult {
        std::string AddressKey;
        uint32_t RemainingReferenceCount = 0;
        bool Evicted = false;
    };

    struct AddressableRuntimeDiagnostics {
        uint64_t TotalLoadRequests = 0;
        uint64_t CacheHits = 0;
        uint64_t SharedInFlightTickets = 0;
        uint64_t FailedLoadRequests = 0;
    };

} // namespace Addressables
} // namespace Asset
} // namespace Core

