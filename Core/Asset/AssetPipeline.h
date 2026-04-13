#pragma once

#include "AssetCooker.h"
#include "Core/JobSystem/JobSystem.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <queue>

namespace Core {
namespace Asset {

    // Cooking statistics
    struct CookingStats {
        std::atomic<uint32_t> TotalAssets{ 0 };
        std::atomic<uint32_t> CookedAssets{ 0 };
        std::atomic<uint32_t> UpToDateAssets{ 0 };
        std::atomic<uint32_t> FailedAssets{ 0 };
        std::atomic<uint64_t> TotalBytesWritten{ 0 };
        std::atomic<uint64_t> CookingTimeMs{ 0 };
        
        void Reset() {
            TotalAssets = 0;
            CookedAssets = 0;
            UpToDateAssets = 0;
            FailedAssets = 0;
            TotalBytesWritten = 0;
            CookingTimeMs = 0;
        }
    };

    // Dependency graph for assets
    class AssetDependencyGraph {
    public:
        void AddDependency(uint64_t assetId, uint64_t dependsOn);
        void RemoveDependency(uint64_t assetId, uint64_t dependsOn);
        void ClearDependencies(uint64_t assetId);
        
        std::vector<uint64_t> GetDependencies(uint64_t assetId) const;
        std::vector<uint64_t> GetDependents(uint64_t assetId) const;
        
        // Get assets in topological order (dependencies first)
        std::vector<uint64_t> GetTopologicalOrder() const;
        
        // Check for cycles
        bool HasCycle() const;

    private:
        std::unordered_map<uint64_t, std::vector<uint64_t>> m_Dependencies;
        std::unordered_map<uint64_t, std::vector<uint64_t>> m_Dependents;
        mutable std::mutex m_Mutex;
    };

    // Asset cooking pipeline orchestrator
    class AssetPipeline {
    public:
        AssetPipeline();
        ~AssetPipeline();

        // Register cookers
        void RegisterCooker(std::unique_ptr<IAssetCooker> cooker);
        
        // Set options
        void SetOptions(const CookOptions& options) { m_Options = options; }
        const CookOptions& GetOptions() const { return m_Options; }
        
        // Set progress callback
        void SetProgressCallback(CookProgressCallback callback) { 
            m_ProgressCallback = std::move(callback); 
        }

        // Scan for source assets
        void ScanSourceDirectory(const std::filesystem::path& sourceDir);
        
        // Cook all assets
        void CookAll();
        
        // Cook single asset
        CookResult CookAsset(const std::filesystem::path& sourcePath);
        
        // Cook assets that have changed
        void CookDirty();
        
        // Clean all cooked assets
        void Clean();
        
        // Get statistics
        const CookingStats& GetStats() const { return m_Stats; }
        
        // Get manifest
        AssetManifest& GetManifest() { return m_Manifest; }
        const AssetManifest& GetManifest() const { return m_Manifest; }
        
        // Load/save manifest
        bool LoadManifest(const std::filesystem::path& path);
        bool SaveManifest(const std::filesystem::path& path) const;

        // Stage 25 dependency helpers
        void RegisterAssetDependencyByPath(const std::string& assetPath,
                                           const std::string& dependsOnPath);
        std::vector<uint64_t> GetAssetDependentsByPath(const std::string& assetPath) const;

    private:
        // Find appropriate cooker for file
        IAssetCooker* FindCooker(const std::filesystem::path& sourcePath);
        
        // Generate output path for source
        std::filesystem::path GetOutputPath(const std::filesystem::path& sourcePath) const;
        
        // Process single asset (called from job)
        void ProcessAsset(const std::filesystem::path& sourcePath);
        
        // Parallel cooking implementation
        void CookParallel(const std::vector<std::filesystem::path>& assets);

    private:
        std::vector<std::unique_ptr<IAssetCooker>> m_Cookers;
        std::unordered_map<std::string, IAssetCooker*> m_ExtensionMap;
        
        CookOptions m_Options;
        CookProgressCallback m_ProgressCallback;
        
        AssetManifest m_Manifest;
        AssetDependencyGraph m_DependencyGraph;
        CookingStats m_Stats;
        
        std::vector<std::filesystem::path> m_SourceAssets;
        std::mutex m_AssetMutex;
    };

    // Asset database for runtime lookup
    class AssetDatabase {
    public:
        static AssetDatabase& Get();

        // Initialize from manifest
        bool Initialize(const std::filesystem::path& manifestPath);
        void Shutdown();
        
        // Lookup cooked asset path
        std::filesystem::path GetCookedPath(uint64_t assetId) const;
        std::filesystem::path GetCookedPath(const std::string& sourcePath) const;
        
        // Check if asset exists
        bool HasAsset(uint64_t assetId) const;
        bool HasAsset(const std::string& sourcePath) const;
        
        // Get asset type
        AssetType GetAssetType(uint64_t assetId) const;
        
        // Iterate all assets
        template<typename Func>
        void ForEachAsset(Func&& func) const {
            for (const auto& entry : m_Manifest.GetAllEntries()) {
                func(entry);
            }
        }

    private:
        AssetDatabase() = default;
        ~AssetDatabase() = default;
        
        AssetManifest m_Manifest;
        std::unordered_map<uint64_t, size_t> m_IdToIndex;
        std::unordered_map<std::string, size_t> m_PathToIndex;
        mutable std::mutex m_Mutex;
    };

} // namespace Asset
} // namespace Core
