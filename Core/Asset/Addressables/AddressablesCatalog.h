#pragma once

#include "AddressablesCatalogTypes.h"

namespace Core {
namespace Asset {
namespace Addressables {

    class AddressablesCatalogService {
    public:
        static constexpr uint32_t kCatalogSchemaVersion = 1;

        Result<BuildAddressablesCatalogResult> BuildAddressablesCatalog(
            const BuildAddressablesCatalogRequest& request) const;

        Result<AddressablesCatalogData> LoadCatalog(
            const std::filesystem::path& catalogPath) const;

        Result<void> SaveCatalog(
            const AddressablesCatalogData& catalog,
            const std::filesystem::path& outputPath) const;
    };

    Result<BuildAddressablesCatalogResult> BuildAddressablesCatalog(
        const BuildAddressablesCatalogRequest& request);

    Result<AddressablesCatalogData> LoadAddressablesCatalog(
        const std::filesystem::path& catalogPath);

} // namespace Addressables
} // namespace Asset
} // namespace Core

