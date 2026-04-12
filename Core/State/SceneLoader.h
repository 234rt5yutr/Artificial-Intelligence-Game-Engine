#pragma once

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <queue>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <vector>

namespace Core {

// Forward declarations
namespace ECS {
    class Scene;
}

namespace State {

    /// @brief Loading phase enumeration
    enum class LoadingPhase {
        Starting,               ///< Initial phase
        UnloadingPrevious,      ///< Unloading current scene
        LoadingAssets,          ///< Loading scene assets
        DeserializingEntities,  ///< Parsing and creating entities
        InitializingSystems,    ///< Setting up ECS systems
        Ready                   ///< Scene is ready
    };

    /// @brief Progress information for loading callbacks
    struct LoadingProgress {
        LoadingPhase phase = LoadingPhase::Starting;
        float progress = 0.0f;          ///< 0.0 - 1.0
        std::string description;        ///< Human-readable status
    };

    /// @brief Scene transition styles
    enum class TransitionStyle {
        None,       ///< Instant transition (no effect)
        Fade,       ///< Fade to black then fade in
        Wipe,       ///< Horizontal wipe effect
        Custom      ///< Custom transition callback
    };

    // Callback types
    using LoadingCallback = std::function<void(const LoadingProgress&)>;
    using SceneReadyCallback = std::function<void(ECS::Scene*)>;
    using SceneCallback = std::function<void(ECS::Scene*)>;

    enum class SceneLayerStatus {
        PendingPrefetch,
        Loading,
        Activating,
        Active,
        Unloading,
        Failed
    };

    struct SceneLayerHandle {
        uint64_t Ticket = 0;
        std::string LayerId;
        std::string ScenePath;
    };

    struct SceneLayerInfo {
        std::string LayerId;
        std::string ScenePath;
        std::vector<std::string> Dependencies;
        SceneLayerStatus Status = SceneLayerStatus::PendingPrefetch;
        uint64_t Ticket = 0;
    };

    struct AdditiveSceneLoadRequest {
        std::string ScenePath;
        std::string LayerId;
        std::vector<std::string> Dependencies;
        bool AllowReload = false;
        bool ActivateOnLoad = true;
        SceneReadyCallback OnReady = nullptr;
        LoadingCallback OnProgress = nullptr;
    };

    struct AdditiveSceneUnloadRequest {
        std::string LayerId;
        bool Force = false;
        SceneCallback OnUnloaded = nullptr;
    };

    /// @brief Asynchronous scene loader with streaming support
    /// 
    /// SceneLoader handles:
    /// - Synchronous and asynchronous scene loading
    /// - Scene transition effects
    /// - Level streaming (preloading adjacent scenes)
    /// - Scene lifecycle callbacks
    /// - Loading progress reporting
    class SceneLoader {
    public:
        static SceneLoader& Get();

        // =====================================================================
        // Initialization
        // =====================================================================

        /// @brief Initialize the scene loader
        void Initialize();

        /// @brief Shutdown and cleanup
        void Shutdown();

        // =====================================================================
        // Synchronous Loading
        // =====================================================================

        /// @brief Load a scene synchronously (blocks until complete)
        /// @param scenePath Path to scene file
        /// @return Loaded scene, or nullptr on failure
        std::unique_ptr<ECS::Scene> LoadScene(const std::string& scenePath);

        // =====================================================================
        // Asynchronous Loading
        // =====================================================================

        /// @brief Load a scene asynchronously
        /// @param scenePath Path to scene file
        /// @param onReady Callback when scene is ready
        /// @param onProgress Optional progress callback
        void LoadSceneAsync(const std::string& scenePath,
                            SceneReadyCallback onReady,
                            LoadingCallback onProgress = nullptr);

        /// @brief Load a scene as an additive layer asynchronously
        /// @param request Additive load request
        /// @param outHandle Optional handle that tracks this layer request
        /// @return true when request accepted
        bool LoadSceneAdditiveAsync(const AdditiveSceneLoadRequest& request,
                                    SceneLayerHandle* outHandle = nullptr);

        /// @brief Unload an additive scene layer asynchronously
        /// @param request Additive unload request
        /// @return true when unload succeeded
        bool UnloadSceneAsync(const AdditiveSceneUnloadRequest& request);

        /// @brief Check whether an additive scene layer is active
        bool IsSceneLayerActive(const std::string& layerId) const;

        /// @brief Get active additive scene layer by id
        ECS::Scene* GetSceneLayer(const std::string& layerId);

        /// @brief Get additive layer metadata for runtime diagnostics
        std::vector<SceneLayerInfo> GetSceneLayerInfos() const;

        /// @brief Cancel pending async load
        void CancelAsyncLoad();

        /// @brief Check if async load is in progress
        bool IsLoading() const { return m_IsLoading; }

        /// @brief Get current loading progress
        LoadingProgress GetLoadingProgress() const { return m_CurrentProgress; }

        // =====================================================================
        // Scene Transitions
        // =====================================================================

        /// @brief Transition to a new scene with effects
        /// @param scenePath Path to scene file
        /// @param style Transition style
        /// @param duration Transition duration in seconds
        void TransitionToScene(const std::string& scenePath,
                               TransitionStyle style = TransitionStyle::Fade,
                               float duration = 0.5f);

        // =====================================================================
        // Level Streaming
        // =====================================================================

        /// @brief Preload a scene for later use
        /// @param scenePath Path to scene file
        void PreloadAdjacentScene(const std::string& scenePath);

        /// @brief Unload a preloaded scene
        /// @param scenePath Path to scene
        void UnloadScene(const std::string& scenePath);

        /// @brief Check if scene is loaded
        bool IsSceneLoaded(const std::string& scenePath) const;

        /// @brief Get a preloaded scene
        ECS::Scene* GetPreloadedScene(const std::string& scenePath);

        // =====================================================================
        // Lifecycle Callbacks
        // =====================================================================

        /// @brief Set callback for scene unload
        void SetOnSceneUnload(SceneCallback callback) { m_OnSceneUnload = std::move(callback); }

        /// @brief Set callback for scene load start
        void SetOnSceneLoad(SceneCallback callback) { m_OnSceneLoad = std::move(callback); }

        /// @brief Set callback for scene ready
        void SetOnSceneReady(SceneCallback callback) { m_OnSceneReady = std::move(callback); }

        // =====================================================================
        // Update
        // =====================================================================

        /// @brief Update async loading (call each frame)
        void Update(float deltaTime);

    private:
        SceneLoader() = default;
        ~SceneLoader() = default;

        // Delete copy/move
        SceneLoader(const SceneLoader&) = delete;
        SceneLoader& operator=(const SceneLoader&) = delete;

        /// @brief Internal async load worker
        void AsyncLoadWorker(const std::string& scenePath);

        /// @brief Update loading progress
        void SetProgress(LoadingPhase phase, float progress, const std::string& description);

        /// @brief Parse scene file
        std::unique_ptr<ECS::Scene> ParseSceneFile(const std::string& scenePath);

        /// @brief Update additive layer async jobs
        void UpdateAdditiveLoads();

        /// @brief Check whether another active layer depends on the target layer
        bool IsLayerUnloadBlocked(const std::string& targetLayerId) const;

        /// @brief Build deterministic layer id from request
        static std::string ResolveLayerId(const AdditiveSceneLoadRequest& request);

        struct AdditiveLayerRuntime {
            SceneLayerInfo Info;
            std::unique_ptr<ECS::Scene> SceneData;
        };

        struct PendingAdditiveLoad {
            SceneLayerInfo Info;
            AdditiveSceneLoadRequest Request;
            std::future<std::unique_ptr<ECS::Scene>> Future;
        };

    private:
        bool m_Initialized = false;
        std::atomic<bool> m_IsLoading{false};
        std::atomic<bool> m_CancelRequested{false};
        
        LoadingProgress m_CurrentProgress;
        std::mutex m_ProgressMutex;

        // Async load state
        SceneReadyCallback m_OnAsyncReady;
        LoadingCallback m_OnAsyncProgress;
        std::future<std::unique_ptr<ECS::Scene>> m_AsyncLoadFuture;
        std::unique_ptr<ECS::Scene> m_AsyncLoadedScene;

        // Preloaded scenes
        std::unordered_map<std::string, std::unique_ptr<ECS::Scene>> m_PreloadedScenes;
        std::unordered_map<std::string, AdditiveLayerRuntime> m_AdditiveLayers;
        std::unordered_map<std::string, PendingAdditiveLoad> m_PendingAdditiveLoads;
        mutable std::mutex m_AdditiveMutex;
        uint64_t m_NextLayerTicket = 1;

        // Lifecycle callbacks
        SceneCallback m_OnSceneUnload;
        SceneCallback m_OnSceneLoad;
        SceneCallback m_OnSceneReady;

        // Transition state
        TransitionStyle m_TransitionStyle = TransitionStyle::None;
        float m_TransitionDuration = 0.0f;
        float m_TransitionTimer = 0.0f;
        std::string m_PendingScenePath;
    };

} // namespace State
} // namespace Core
