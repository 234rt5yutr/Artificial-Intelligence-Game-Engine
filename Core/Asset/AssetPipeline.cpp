#include "AssetPipeline.h"
#include "Core/Log.h"
#include <algorithm>
#include <chrono>
#include <stack>
#include <unordered_set>

#define LOG_INFO(...) ENGINE_CORE_INFO(__VA_ARGS__)
#define LOG_WARN(...) ENGINE_CORE_WARN(__VA_ARGS__)
#define LOG_ERROR(...) ENGINE_CORE_ERROR(__VA_ARGS__)
#define LOG_TRACE(...) ENGINE_CORE_TRACE(__VA_ARGS__)

namespace Core {
namespace Asset {

    // AssetDependencyGraph implementation
    void AssetDependencyGraph::AddDependency(uint64_t assetId, uint64_t dependsOn) {
        std::lock_guard lock(m_Mutex);
        
        auto& deps = m_Dependencies[assetId];
        if (std::find(deps.begin(), deps.end(), dependsOn) == deps.end()) {
            deps.push_back(dependsOn);
        }
        
        auto& dependents = m_Dependents[dependsOn];
        if (std::find(dependents.begin(), dependents.end(), assetId) == dependents.end()) {
            dependents.push_back(assetId);
        }
    }

    void AssetDependencyGraph::RemoveDependency(uint64_t assetId, uint64_t dependsOn) {
        std::lock_guard lock(m_Mutex);
        
        auto it = m_Dependencies.find(assetId);
        if (it != m_Dependencies.end()) {
            auto& deps = it->second;
            deps.erase(std::remove(deps.begin(), deps.end(), dependsOn), deps.end());
        }
        
        auto it2 = m_Dependents.find(dependsOn);
        if (it2 != m_Dependents.end()) {
            auto& dependents = it2->second;
            dependents.erase(std::remove(dependents.begin(), dependents.end(), assetId), 
                           dependents.end());
        }
    }

    void AssetDependencyGraph::ClearDependencies(uint64_t assetId) {
        std::lock_guard lock(m_Mutex);
        
        // Remove from dependents lists
        auto it = m_Dependencies.find(assetId);
        if (it != m_Dependencies.end()) {
            for (uint64_t dep : it->second) {
                auto depIt = m_Dependents.find(dep);
                if (depIt != m_Dependents.end()) {
                    auto& dependents = depIt->second;
                    dependents.erase(std::remove(dependents.begin(), dependents.end(), assetId),
                                   dependents.end());
                }
            }
            m_Dependencies.erase(it);
        }
    }

    std::vector<uint64_t> AssetDependencyGraph::GetDependencies(uint64_t assetId) const {
        std::lock_guard lock(m_Mutex);
        
        auto it = m_Dependencies.find(assetId);
        if (it != m_Dependencies.end()) {
            return it->second;
        }
        return {};
    }

    std::vector<uint64_t> AssetDependencyGraph::GetDependents(uint64_t assetId) const {
        std::lock_guard lock(m_Mutex);
        
        auto it = m_Dependents.find(assetId);
        if (it != m_Dependents.end()) {
            return it->second;
        }
        return {};
    }

    std::vector<uint64_t> AssetDependencyGraph::GetTopologicalOrder() const {
        std::lock_guard lock(m_Mutex);
        
        std::vector<uint64_t> result;
        std::unordered_map<uint64_t, int> inDegree;
        std::queue<uint64_t> ready;
        
        // Collect all nodes
        std::unordered_set<uint64_t> allNodes;
        for (const auto& [node, deps] : m_Dependencies) {
            allNodes.insert(node);
            for (uint64_t dep : deps) {
                allNodes.insert(dep);
            }
        }
        
        // Calculate in-degrees
        for (uint64_t node : allNodes) {
            inDegree[node] = 0;
        }
        for (const auto& [node, deps] : m_Dependencies) {
            inDegree[node] = static_cast<int>(deps.size());
        }
        
        // Find nodes with no dependencies
        for (const auto& [node, degree] : inDegree) {
            if (degree == 0) {
                ready.push(node);
            }
        }
        
        // Process in order
        while (!ready.empty()) {
            uint64_t current = ready.front();
            ready.pop();
            result.push_back(current);
            
            auto it = m_Dependents.find(current);
            if (it != m_Dependents.end()) {
                for (uint64_t dependent : it->second) {
                    inDegree[dependent]--;
                    if (inDegree[dependent] == 0) {
                        ready.push(dependent);
                    }
                }
            }
        }
        
        return result;
    }

    bool AssetDependencyGraph::HasCycle() const {
        std::lock_guard lock(m_Mutex);
        
        std::unordered_set<uint64_t> visited;
        std::unordered_set<uint64_t> recursionStack;
        
        std::function<bool(uint64_t)> dfs = [&](uint64_t node) -> bool {
            visited.insert(node);
            recursionStack.insert(node);
            
            auto it = m_Dependencies.find(node);
            if (it != m_Dependencies.end()) {
                for (uint64_t dep : it->second) {
                    if (recursionStack.count(dep) > 0) {
                        return true; // Cycle detected
                    }
                    if (visited.count(dep) == 0 && dfs(dep)) {
                        return true;
                    }
                }
            }
            
            recursionStack.erase(node);
            return false;
        };
        
        for (const auto& [node, _] : m_Dependencies) {
            if (visited.count(node) == 0) {
                if (dfs(node)) {
                    return true;
                }
            }
        }
        
        return false;
    }

    // AssetPipeline implementation
    AssetPipeline::AssetPipeline() {
        // Register default cookers
        RegisterCooker(std::make_unique<TextureCooker>());
        RegisterCooker(std::make_unique<MeshCooker>());
        RegisterCooker(std::make_unique<ShaderCooker>());
    }

    AssetPipeline::~AssetPipeline() = default;

    void AssetPipeline::RegisterCooker(std::unique_ptr<IAssetCooker> cooker) {
        for (const auto& ext : cooker->GetSupportedExtensions()) {
            m_ExtensionMap[ext] = cooker.get();
        }
        m_Cookers.push_back(std::move(cooker));
    }

    IAssetCooker* AssetPipeline::FindCooker(const std::filesystem::path& sourcePath) {
        std::string ext = sourcePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        auto it = m_ExtensionMap.find(ext);
        if (it != m_ExtensionMap.end()) {
            return it->second;
        }
        return nullptr;
    }

    std::filesystem::path AssetPipeline::GetOutputPath(
        const std::filesystem::path& sourcePath) const {
        
        std::filesystem::path relative = sourcePath.filename();
        std::filesystem::path output = m_Options.OutputDirectory;
        output /= relative;
        output.replace_extension(".cooked");
        
        return output;
    }

    void AssetPipeline::ScanSourceDirectory(const std::filesystem::path& sourceDir) {
        LOG_INFO("Scanning source directory: {}", sourceDir.string());
        
        m_SourceAssets.clear();
        
        if (!std::filesystem::exists(sourceDir)) {
            LOG_ERROR("Source directory does not exist: {}", sourceDir.string());
            return;
        }
        
        for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceDir)) {
            if (!entry.is_regular_file()) continue;
            
            IAssetCooker* cooker = FindCooker(entry.path());
            if (cooker) {
                m_SourceAssets.push_back(entry.path());
            }
        }
        
        LOG_INFO("Found {} cookable assets", m_SourceAssets.size());
        m_Stats.TotalAssets = static_cast<uint32_t>(m_SourceAssets.size());
    }

    void AssetPipeline::CookAll() {
        LOG_INFO("Starting full asset cook...");
        
        auto startTime = std::chrono::steady_clock::now();
        m_Stats.Reset();
        m_Stats.TotalAssets = static_cast<uint32_t>(m_SourceAssets.size());
        
        if (m_Options.Parallel && m_SourceAssets.size() > 1) {
            CookParallel(m_SourceAssets);
        } else {
            for (const auto& asset : m_SourceAssets) {
                ProcessAsset(asset);
            }
        }
        
        auto endTime = std::chrono::steady_clock::now();
        m_Stats.CookingTimeMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());
        
        LOG_INFO("Asset cooking complete: {} cooked, {} up-to-date, {} failed ({} ms)",
                 m_Stats.CookedAssets.load(),
                 m_Stats.UpToDateAssets.load(),
                 m_Stats.FailedAssets.load(),
                 m_Stats.CookingTimeMs.load());
    }

    CookResult AssetPipeline::CookAsset(const std::filesystem::path& sourcePath) {
        IAssetCooker* cooker = FindCooker(sourcePath);
        if (!cooker) {
            LOG_WARN("No cooker found for: {}", sourcePath.string());
            return CookResult::InvalidFormat;
        }
        
        std::filesystem::path outputPath = GetOutputPath(sourcePath);
        return cooker->Cook(sourcePath, outputPath, m_Options);
    }

    void AssetPipeline::CookDirty() {
        LOG_INFO("Cooking dirty assets...");
        
        auto startTime = std::chrono::steady_clock::now();
        m_Stats.Reset();
        
        std::vector<std::filesystem::path> dirtyAssets;
        
        for (const auto& asset : m_SourceAssets) {
            IAssetCooker* cooker = FindCooker(asset);
            if (cooker) {
                std::filesystem::path outputPath = GetOutputPath(asset);
                if (cooker->NeedsCooking(asset, outputPath)) {
                    dirtyAssets.push_back(asset);
                }
            }
        }
        
        m_Stats.TotalAssets = static_cast<uint32_t>(dirtyAssets.size());
        LOG_INFO("Found {} dirty assets", dirtyAssets.size());
        
        if (m_Options.Parallel && dirtyAssets.size() > 1) {
            CookParallel(dirtyAssets);
        } else {
            for (const auto& asset : dirtyAssets) {
                ProcessAsset(asset);
            }
        }
        
        auto endTime = std::chrono::steady_clock::now();
        m_Stats.CookingTimeMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());
        
        LOG_INFO("Dirty cook complete: {} cooked, {} failed ({} ms)",
                 m_Stats.CookedAssets.load(),
                 m_Stats.FailedAssets.load(),
                 m_Stats.CookingTimeMs.load());
    }

    void AssetPipeline::Clean() {
        LOG_INFO("Cleaning cooked assets...");
        
        if (m_Options.OutputDirectory.empty()) {
            LOG_WARN("No output directory specified, cannot clean");
            return;
        }
        
        std::filesystem::path outputDir = m_Options.OutputDirectory;
        if (std::filesystem::exists(outputDir)) {
            uint32_t deleted = 0;
            for (const auto& entry : std::filesystem::directory_iterator(outputDir)) {
                if (entry.path().extension() == ".cooked") {
                    std::filesystem::remove(entry.path());
                    deleted++;
                }
            }
            LOG_INFO("Deleted {} cooked assets", deleted);
        }
        
        m_Manifest.Clear();
    }

    void AssetPipeline::ProcessAsset(const std::filesystem::path& sourcePath) {
        IAssetCooker* cooker = FindCooker(sourcePath);
        if (!cooker) {
            m_Stats.FailedAssets++;
            return;
        }
        
        std::filesystem::path outputPath = GetOutputPath(sourcePath);
        
        CookResult result = cooker->Cook(sourcePath, outputPath, m_Options);
        
        switch (result) {
            case CookResult::Success:
                m_Stats.CookedAssets++;
                if (std::filesystem::exists(outputPath)) {
                    m_Stats.TotalBytesWritten += std::filesystem::file_size(outputPath);
                }
                
                // Update manifest
                {
                    ManifestEntry entry;
                    entry.AssetId = ComputeAssetId(sourcePath.string());
                    entry.Type = cooker->GetSupportedTypes()[0];
                    entry.SourcePath = sourcePath.string();
                    entry.CookedPath = outputPath.string();
                    entry.SourceHash = ComputeAssetId(sourcePath.string());
                    
                    auto now = std::chrono::system_clock::now();
                    entry.CookedTimestamp = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::seconds>(
                            now.time_since_epoch()).count());
                    
                    std::lock_guard lock(m_AssetMutex);
                    m_Manifest.AddEntry(entry);
                }
                break;
                
            case CookResult::UpToDate:
                m_Stats.UpToDateAssets++;
                break;
                
            default:
                m_Stats.FailedAssets++;
                break;
        }
        
        // Report progress
        if (m_ProgressCallback && m_Stats.TotalAssets > 0) {
            uint32_t processed = m_Stats.CookedAssets + m_Stats.UpToDateAssets + 
                               m_Stats.FailedAssets;
            float progress = static_cast<float>(processed) / m_Stats.TotalAssets;
            m_ProgressCallback(sourcePath.filename().string(), progress);
        }
    }

    void AssetPipeline::CookParallel(const std::vector<std::filesystem::path>& assets) {
        LOG_TRACE("Cooking {} assets in parallel", assets.size());
        
        // Use simple parallel for loop
        size_t numThreads = m_Options.MaxThreads > 0 
            ? m_Options.MaxThreads 
            : std::thread::hardware_concurrency();
        
        std::vector<std::thread> threads;
        std::atomic<size_t> nextIndex{ 0 };
        
        for (size_t t = 0; t < numThreads; ++t) {
            threads.emplace_back([this, &assets, &nextIndex]() {
                while (true) {
                    size_t index = nextIndex.fetch_add(1);
                    if (index >= assets.size()) break;
                    ProcessAsset(assets[index]);
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
    }

    bool AssetPipeline::LoadManifest(const std::filesystem::path& path) {
        return m_Manifest.Load(path);
    }

    bool AssetPipeline::SaveManifest(const std::filesystem::path& path) const {
        return m_Manifest.Save(path);
    }

    // AssetDatabase implementation
    AssetDatabase& AssetDatabase::Get() {
        static AssetDatabase instance;
        return instance;
    }

    bool AssetDatabase::Initialize(const std::filesystem::path& manifestPath) {
        std::lock_guard lock(m_Mutex);
        
        if (!m_Manifest.Load(manifestPath)) {
            LOG_WARN("Failed to load asset manifest: {}", manifestPath.string());
            return false;
        }
        
        // Build lookup tables
        m_IdToIndex.clear();
        m_PathToIndex.clear();
        
        const auto& entries = m_Manifest.GetAllEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            m_IdToIndex[entries[i].AssetId] = i;
            m_PathToIndex[entries[i].SourcePath] = i;
        }
        
        LOG_INFO("Asset database initialized with {} assets", entries.size());
        return true;
    }

    void AssetDatabase::Shutdown() {
        std::lock_guard lock(m_Mutex);
        m_IdToIndex.clear();
        m_PathToIndex.clear();
        m_Manifest.Clear();
    }

    std::filesystem::path AssetDatabase::GetCookedPath(uint64_t assetId) const {
        std::lock_guard lock(m_Mutex);
        
        auto it = m_IdToIndex.find(assetId);
        if (it != m_IdToIndex.end()) {
            return m_Manifest.GetAllEntries()[it->second].CookedPath;
        }
        return {};
    }

    std::filesystem::path AssetDatabase::GetCookedPath(const std::string& sourcePath) const {
        std::lock_guard lock(m_Mutex);
        
        auto it = m_PathToIndex.find(sourcePath);
        if (it != m_PathToIndex.end()) {
            return m_Manifest.GetAllEntries()[it->second].CookedPath;
        }
        return {};
    }

    bool AssetDatabase::HasAsset(uint64_t assetId) const {
        std::lock_guard lock(m_Mutex);
        return m_IdToIndex.count(assetId) > 0;
    }

    bool AssetDatabase::HasAsset(const std::string& sourcePath) const {
        std::lock_guard lock(m_Mutex);
        return m_PathToIndex.count(sourcePath) > 0;
    }

    AssetType AssetDatabase::GetAssetType(uint64_t assetId) const {
        std::lock_guard lock(m_Mutex);
        
        auto it = m_IdToIndex.find(assetId);
        if (it != m_IdToIndex.end()) {
            return m_Manifest.GetAllEntries()[it->second].Type;
        }
        return AssetType::Unknown;
    }

} // namespace Asset
} // namespace Core
