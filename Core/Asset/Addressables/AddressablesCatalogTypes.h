#pragma once

#include "Core/Asset/AssetTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Core {
namespace Asset {
namespace Addressables {

    template <typename T>
    struct Result {
        bool Ok = false;
        T Value{};
        std::string Error;

        static Result Success(T value) {
            Result result;
            result.Ok = true;
            result.Value = std::move(value);
            return result;
        }

        static Result Failure(std::string error) {
            Result result;
            result.Ok = false;
            result.Error = std::move(error);
            return result;
        }
    };

    template <>
    struct Result<void> {
        bool Ok = false;
        std::string Error;

        static Result Success() {
            Result result;
            result.Ok = true;
            return result;
        }

        static Result Failure(std::string error) {
            Result result;
            result.Ok = false;
            result.Error = std::move(error);
            return result;
        }
    };

    enum class AddressKeyStrategy : uint8_t {
        PathHash = 0,
        ExplicitAddress = 1,
        Hybrid = 2
    };

    enum class DuplicateAddressPolicy : uint8_t {
        Fail = 0,
        Override = 1,
        Alias = 2
    };

    struct BuildAddressablesCatalogRequest {
        std::filesystem::path ManifestPath;
        std::filesystem::path OutputCatalogPath;
        AddressKeyStrategy KeyStrategy = AddressKeyStrategy::PathHash;
        DuplicateAddressPolicy DuplicatePolicy = DuplicateAddressPolicy::Fail;
        std::string BuildId;
        bool IncludeBundlePlacement = true;
    };

    struct AddressableCatalogDependency {
        uint64_t AssetId = 0;
        std::string AddressKey;
    };

    struct AddressableCatalogEntry {
        std::string AddressKey;
        uint64_t AssetId = 0;
        AssetType AssetKind = AssetType::Unknown;
        std::string SourcePath;
        std::string CookedPath;
        std::string BundleId;
        std::vector<AddressableCatalogDependency> Dependencies;
    };

    struct AddressablesCatalogMetadata {
        uint32_t SchemaVersion = 1;
        std::string BuildId;
        std::string Digest;
        uint64_t GeneratedAtEpochSeconds = 0;
    };

    struct AddressablesCatalogData {
        AddressablesCatalogMetadata Metadata;
        std::vector<AddressableCatalogEntry> Entries;
    };

    struct BuildAddressablesCatalogResult {
        AddressablesCatalogData Catalog;
        std::filesystem::path OutputPath;
        uint32_t EntryCount = 0;
        uint32_t ConflictCount = 0;
    };

} // namespace Addressables
} // namespace Asset
} // namespace Core

