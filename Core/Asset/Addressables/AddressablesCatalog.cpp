#include "AddressablesCatalog.h"

#include "Core/Asset/AssetCooker.h"
#include "Core/Log.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>

namespace Core {
namespace Asset {
namespace Addressables {
namespace {

    std::string NormalizePath(const std::string& rawPath) {
        std::string normalized = rawPath;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        std::transform(
            normalized.begin(),
            normalized.end(),
            normalized.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return normalized;
    }

    std::string ToHexString(uint64_t value) {
        std::ostringstream stream;
        stream << std::hex << std::setw(16) << std::setfill('0') << value;
        return stream.str();
    }

    std::string BuildAddressKey(const ManifestEntry& entry,
                                const std::string& normalizedSourcePath,
                                AddressKeyStrategy strategy) {
        switch (strategy) {
            case AddressKeyStrategy::ExplicitAddress:
                return entry.AddressKey.empty() ? normalizedSourcePath : entry.AddressKey;
            case AddressKeyStrategy::Hybrid:
                if (!entry.AddressKey.empty()) {
                    return entry.AddressKey;
                }
                return "addr_" + ToHexString(ComputeAssetId(normalizedSourcePath));
            case AddressKeyStrategy::PathHash:
            default:
                return "addr_" + ToHexString(ComputeAssetId(normalizedSourcePath));
        }
    }

    std::string ComputeCatalogDigest(const AddressablesCatalogData& catalog) {
        uint64_t digest = 14695981039346656037ull;
        const auto accumulate = [&digest](const std::string& value) {
            for (char c : value) {
                digest ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
                digest *= 1099511628211ull;
            }
        };

        accumulate(std::to_string(catalog.Metadata.SchemaVersion));
        accumulate(catalog.Metadata.BuildId);
        for (const auto& entry : catalog.Entries) {
            accumulate(entry.AddressKey);
            accumulate(std::to_string(entry.AssetId));
            accumulate(std::to_string(static_cast<uint32_t>(entry.AssetKind)));
            accumulate(entry.CookedPath);
            accumulate(entry.BundleId);
            for (const auto& dependency : entry.Dependencies) {
                accumulate(std::to_string(dependency.AssetId));
                accumulate(dependency.AddressKey);
            }
        }

        return ToHexString(digest);
    }

    nlohmann::json SerializeCatalog(const AddressablesCatalogData& catalog) {
        nlohmann::json root = nlohmann::json::object();
        root["metadata"] = {
            {"schemaVersion", catalog.Metadata.SchemaVersion},
            {"buildId", catalog.Metadata.BuildId},
            {"digest", catalog.Metadata.Digest},
            {"generatedAtEpochSeconds", catalog.Metadata.GeneratedAtEpochSeconds}
        };

        root["entries"] = nlohmann::json::array();
        for (const auto& entry : catalog.Entries) {
            nlohmann::json dependencyArray = nlohmann::json::array();
            for (const auto& dependency : entry.Dependencies) {
                dependencyArray.push_back({
                    {"assetId", dependency.AssetId},
                    {"addressKey", dependency.AddressKey}
                });
            }

            root["entries"].push_back({
                {"addressKey", entry.AddressKey},
                {"assetId", entry.AssetId},
                {"assetKind", static_cast<uint32_t>(entry.AssetKind)},
                {"sourcePath", entry.SourcePath},
                {"cookedPath", entry.CookedPath},
                {"bundleId", entry.BundleId},
                {"dependencies", dependencyArray}
            });
        }

        return root;
    }

} // namespace

    Result<BuildAddressablesCatalogResult> AddressablesCatalogService::BuildAddressablesCatalog(
        const BuildAddressablesCatalogRequest& request) const {

        AssetManifest manifest;
        if (!manifest.Load(request.ManifestPath)) {
            return Result<BuildAddressablesCatalogResult>::Failure(
                "Failed to load asset manifest: " + request.ManifestPath.string());
        }

        std::vector<const ManifestEntry*> orderedManifestEntries;
        orderedManifestEntries.reserve(manifest.GetAllEntries().size());
        for (const auto& entry : manifest.GetAllEntries()) {
            orderedManifestEntries.push_back(&entry);
        }

        std::sort(
            orderedManifestEntries.begin(),
            orderedManifestEntries.end(),
            [](const ManifestEntry* a, const ManifestEntry* b) {
                const std::string normalizedA = NormalizePath(a->SourcePath);
                const std::string normalizedB = NormalizePath(b->SourcePath);
                if (normalizedA == normalizedB) {
                    return a->AssetId < b->AssetId;
                }
                return normalizedA < normalizedB;
            });

        AddressablesCatalogData catalog;
        catalog.Metadata.SchemaVersion = kCatalogSchemaVersion;
        catalog.Metadata.BuildId = request.BuildId.empty() ? "default" : request.BuildId;

        uint32_t conflictCount = 0;
        uint64_t deterministicGeneratedAt = 0;
        std::unordered_map<std::string, size_t> keyToIndex;
        std::unordered_map<uint64_t, const ManifestEntry*> manifestByAssetId;
        std::unordered_map<uint64_t, std::string> keyByAssetId;

        for (const ManifestEntry* manifestEntry : orderedManifestEntries) {
            deterministicGeneratedAt = std::max(deterministicGeneratedAt, manifestEntry->CookedTimestamp);
            manifestByAssetId[manifestEntry->AssetId] = manifestEntry;

            const std::string normalizedSourcePath = NormalizePath(manifestEntry->SourcePath);
            std::string addressKey = BuildAddressKey(*manifestEntry, normalizedSourcePath, request.KeyStrategy);

            auto keyIt = keyToIndex.find(addressKey);
            if (keyIt != keyToIndex.end()) {
                ++conflictCount;
                if (request.DuplicatePolicy == DuplicateAddressPolicy::Fail) {
                    return Result<BuildAddressablesCatalogResult>::Failure(
                        "Duplicate address key detected: " + addressKey);
                }
                if (request.DuplicatePolicy == DuplicateAddressPolicy::Alias) {
                    addressKey += "#" + ToHexString(manifestEntry->AssetId);
                }
            }

            AddressableCatalogEntry catalogEntry;
            catalogEntry.AddressKey = addressKey;
            catalogEntry.AssetId = manifestEntry->AssetId;
            catalogEntry.AssetKind = manifestEntry->Type;
            catalogEntry.SourcePath = manifestEntry->SourcePath;
            catalogEntry.CookedPath = manifestEntry->CookedPath;
            catalogEntry.BundleId = request.IncludeBundlePlacement ? manifestEntry->BundleId : std::string{};

            keyByAssetId[manifestEntry->AssetId] = catalogEntry.AddressKey;

            if (keyIt != keyToIndex.end() &&
                request.DuplicatePolicy == DuplicateAddressPolicy::Override) {
                catalog.Entries[keyIt->second] = std::move(catalogEntry);
            } else {
                keyToIndex[catalogEntry.AddressKey] = catalog.Entries.size();
                catalog.Entries.push_back(std::move(catalogEntry));
            }
        }

        for (auto& catalogEntry : catalog.Entries) {
            auto manifestIt = manifestByAssetId.find(catalogEntry.AssetId);
            if (manifestIt == manifestByAssetId.end()) {
                continue;
            }

            std::vector<uint64_t> dependencyIds = manifestIt->second->Dependencies;
            std::sort(dependencyIds.begin(), dependencyIds.end());

            for (uint64_t dependencyId : dependencyIds) {
                AddressableCatalogDependency dependency;
                dependency.AssetId = dependencyId;

                auto depKeyIt = keyByAssetId.find(dependencyId);
                if (depKeyIt != keyByAssetId.end()) {
                    dependency.AddressKey = depKeyIt->second;
                } else {
                    dependency.AddressKey = "missing:" + ToHexString(dependencyId);
                }

                catalogEntry.Dependencies.push_back(std::move(dependency));
            }
        }

        std::sort(
            catalog.Entries.begin(),
            catalog.Entries.end(),
            [](const AddressableCatalogEntry& a, const AddressableCatalogEntry& b) {
                if (a.AddressKey == b.AddressKey) {
                    return a.AssetId < b.AssetId;
                }
                return a.AddressKey < b.AddressKey;
            });

        catalog.Metadata.GeneratedAtEpochSeconds = deterministicGeneratedAt;
        catalog.Metadata.Digest = ComputeCatalogDigest(catalog);

        auto saveResult = SaveCatalog(catalog, request.OutputCatalogPath);
        if (!saveResult.Ok) {
            return Result<BuildAddressablesCatalogResult>::Failure(saveResult.Error);
        }

        BuildAddressablesCatalogResult result;
        result.Catalog = std::move(catalog);
        result.OutputPath = request.OutputCatalogPath;
        result.EntryCount = static_cast<uint32_t>(result.Catalog.Entries.size());
        result.ConflictCount = conflictCount;
        return Result<BuildAddressablesCatalogResult>::Success(std::move(result));
    }

    Result<AddressablesCatalogData> AddressablesCatalogService::LoadCatalog(
        const std::filesystem::path& catalogPath) const {

        std::ifstream file(catalogPath);
        if (!file) {
            return Result<AddressablesCatalogData>::Failure(
                "Failed to open catalog file: " + catalogPath.string());
        }

        nlohmann::json payload;
        try {
            file >> payload;
        } catch (const nlohmann::json::parse_error& parseError) {
            return Result<AddressablesCatalogData>::Failure(
                "Failed to parse catalog JSON: " + std::string(parseError.what()));
        }

        AddressablesCatalogData catalog;
        if (!payload.contains("metadata") || !payload["metadata"].is_object()) {
            return Result<AddressablesCatalogData>::Failure("Catalog metadata missing");
        }

        const auto& metadata = payload["metadata"];
        catalog.Metadata.SchemaVersion = metadata.value("schemaVersion", 0u);
        catalog.Metadata.BuildId = metadata.value("buildId", std::string{});
        catalog.Metadata.Digest = metadata.value("digest", std::string{});
        catalog.Metadata.GeneratedAtEpochSeconds = metadata.value("generatedAtEpochSeconds", 0ull);

        if (catalog.Metadata.SchemaVersion != kCatalogSchemaVersion) {
            return Result<AddressablesCatalogData>::Failure(
                "Unsupported catalog schema version: " + std::to_string(catalog.Metadata.SchemaVersion));
        }

        if (!payload.contains("entries") || !payload["entries"].is_array()) {
            return Result<AddressablesCatalogData>::Failure("Catalog entries missing");
        }

        for (const auto& jsonEntry : payload["entries"]) {
            AddressableCatalogEntry entry;
            entry.AddressKey = jsonEntry.value("addressKey", std::string{});
            entry.AssetId = jsonEntry.value("assetId", 0ull);
            entry.AssetKind = static_cast<AssetType>(jsonEntry.value("assetKind", 0u));
            entry.SourcePath = jsonEntry.value("sourcePath", std::string{});
            entry.CookedPath = jsonEntry.value("cookedPath", std::string{});
            entry.BundleId = jsonEntry.value("bundleId", std::string{});

            if (jsonEntry.contains("dependencies") && jsonEntry["dependencies"].is_array()) {
                for (const auto& jsonDependency : jsonEntry["dependencies"]) {
                    AddressableCatalogDependency dependency;
                    dependency.AssetId = jsonDependency.value("assetId", 0ull);
                    dependency.AddressKey = jsonDependency.value("addressKey", std::string{});
                    entry.Dependencies.push_back(std::move(dependency));
                }
            }

            if (entry.AddressKey.empty() || entry.AssetId == 0) {
                return Result<AddressablesCatalogData>::Failure("Catalog entry validation failed");
            }

            catalog.Entries.push_back(std::move(entry));
        }

        return Result<AddressablesCatalogData>::Success(std::move(catalog));
    }

    Result<void> AddressablesCatalogService::SaveCatalog(
        const AddressablesCatalogData& catalog,
        const std::filesystem::path& outputPath) const {

        std::error_code errorCode;
        const std::filesystem::path parentPath = outputPath.parent_path();
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath, errorCode);
        }
        if (errorCode) {
            return Result<void>::Failure("Failed to create catalog directory: " + parentPath.string());
        }

        std::ofstream output(outputPath, std::ios::trunc);
        if (!output) {
            return Result<void>::Failure("Failed to open catalog output path: " + outputPath.string());
        }

        const nlohmann::json payload = SerializeCatalog(catalog);
        output << payload.dump(2);
        output << '\n';
        if (!output) {
            return Result<void>::Failure("Failed to write catalog output: " + outputPath.string());
        }

        ENGINE_CORE_INFO("Built addressables catalog '{}' with {} entries",
                         outputPath.string(),
                         catalog.Entries.size());
        return Result<void>::Success();
    }

    Result<BuildAddressablesCatalogResult> BuildAddressablesCatalog(
        const BuildAddressablesCatalogRequest& request) {
        AddressablesCatalogService service;
        return service.BuildAddressablesCatalog(request);
    }

    Result<AddressablesCatalogData> LoadAddressablesCatalog(
        const std::filesystem::path& catalogPath) {
        AddressablesCatalogService service;
        return service.LoadCatalog(catalogPath);
    }

} // namespace Addressables
} // namespace Asset
} // namespace Core

