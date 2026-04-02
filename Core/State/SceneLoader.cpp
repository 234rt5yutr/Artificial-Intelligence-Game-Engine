#include "SceneLoader.h"
#include "../ECS/Scene.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <filesystem>

namespace Core {
namespace State {

using json = nlohmann::json;

// =============================================================================
// Singleton Access
// =============================================================================

SceneLoader& SceneLoader::Get() {
    static SceneLoader instance;
    return instance;
}

// =============================================================================
// Initialization
// =============================================================================

void SceneLoader::Initialize() {
    if (m_Initialized) return;
    m_Initialized = true;
}

void SceneLoader::Shutdown() {
    if (!m_Initialized) return;

    // Cancel any pending load
    CancelAsyncLoad();

    // Wait for async load to complete
    if (m_AsyncLoadFuture.valid()) {
        m_AsyncLoadFuture.wait();
    }

    // Unload all preloaded scenes
    m_PreloadedScenes.clear();

    m_Initialized = false;
}

// =============================================================================
// Synchronous Loading
// =============================================================================

std::unique_ptr<ECS::Scene> SceneLoader::LoadScene(const std::string& scenePath) {
    SetProgress(LoadingPhase::LoadingAssets, 0.0f, "Loading scene: " + scenePath);

    auto scene = ParseSceneFile(scenePath);
    if (!scene) {
        return nullptr;
    }

    // Call lifecycle callbacks
    if (m_OnSceneLoad) {
        m_OnSceneLoad(scene.get());
    }

    SetProgress(LoadingPhase::InitializingSystems, 0.8f, "Initializing systems...");

    SetProgress(LoadingPhase::Ready, 1.0f, "Scene ready");

    if (m_OnSceneReady) {
        m_OnSceneReady(scene.get());
    }

    return scene;
}

// =============================================================================
// Asynchronous Loading
// =============================================================================

void SceneLoader::LoadSceneAsync(const std::string& scenePath,
                                  SceneReadyCallback onReady,
                                  LoadingCallback onProgress) {
    if (m_IsLoading) {
        // Already loading - cancel previous load first
        CancelAsyncLoad();
    }

    m_IsLoading = true;
    m_CancelRequested = false;
    m_OnAsyncReady = std::move(onReady);
    m_OnAsyncProgress = std::move(onProgress);

    SetProgress(LoadingPhase::Starting, 0.0f, "Starting async load...");

    // Launch async load
    m_AsyncLoadFuture = std::async(std::launch::async, [this, scenePath]() {
        return ParseSceneFile(scenePath);
    });
}

void SceneLoader::CancelAsyncLoad() {
    if (!m_IsLoading) return;
    
    m_CancelRequested = true;
    
    // Wait for async task to complete
    if (m_AsyncLoadFuture.valid()) {
        m_AsyncLoadFuture.wait();
    }
    
    m_IsLoading = false;
    m_CancelRequested = false;
    m_AsyncLoadedScene = nullptr;
}

// =============================================================================
// Scene Transitions
// =============================================================================

void SceneLoader::TransitionToScene(const std::string& scenePath,
                                     TransitionStyle style,
                                     float duration) {
    m_TransitionStyle = style;
    m_TransitionDuration = duration;
    m_TransitionTimer = duration;
    m_PendingScenePath = scenePath;

    if (style == TransitionStyle::None) {
        // Immediate transition
        LoadSceneAsync(scenePath, [this](ECS::Scene* scene) {
            if (m_OnSceneReady) {
                m_OnSceneReady(scene);
            }
        });
    }
}

// =============================================================================
// Level Streaming
// =============================================================================

void SceneLoader::PreloadAdjacentScene(const std::string& scenePath) {
    if (m_PreloadedScenes.find(scenePath) != m_PreloadedScenes.end()) {
        return; // Already preloaded
    }

    auto scene = ParseSceneFile(scenePath);
    if (scene) {
        m_PreloadedScenes[scenePath] = std::move(scene);
    }
}

void SceneLoader::UnloadScene(const std::string& scenePath) {
    auto it = m_PreloadedScenes.find(scenePath);
    if (it != m_PreloadedScenes.end()) {
        if (m_OnSceneUnload) {
            m_OnSceneUnload(it->second.get());
        }
        m_PreloadedScenes.erase(it);
    }
}

bool SceneLoader::IsSceneLoaded(const std::string& scenePath) const {
    return m_PreloadedScenes.find(scenePath) != m_PreloadedScenes.end();
}

ECS::Scene* SceneLoader::GetPreloadedScene(const std::string& scenePath) {
    auto it = m_PreloadedScenes.find(scenePath);
    if (it != m_PreloadedScenes.end()) {
        return it->second.get();
    }
    return nullptr;
}

// =============================================================================
// Update
// =============================================================================

void SceneLoader::Update(float deltaTime) {
    // Handle transition
    if (m_TransitionTimer > 0.0f) {
        m_TransitionTimer -= deltaTime;
        
        if (m_TransitionTimer <= m_TransitionDuration / 2.0f && !m_IsLoading && !m_PendingScenePath.empty()) {
            // Start loading when halfway through fade out
            LoadSceneAsync(m_PendingScenePath, [this](ECS::Scene* scene) {
                if (m_OnSceneReady) {
                    m_OnSceneReady(scene);
                }
                m_PendingScenePath.clear();
            });
        }
    }

    // Check async load completion
    if (m_IsLoading && m_AsyncLoadFuture.valid()) {
        auto status = m_AsyncLoadFuture.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            m_AsyncLoadedScene = m_AsyncLoadFuture.get();
            m_IsLoading = false;

            if (!m_CancelRequested && m_AsyncLoadedScene) {
                // Call lifecycle callbacks
                if (m_OnSceneLoad) {
                    m_OnSceneLoad(m_AsyncLoadedScene.get());
                }

                SetProgress(LoadingPhase::Ready, 1.0f, "Scene ready");

                if (m_OnSceneReady) {
                    m_OnSceneReady(m_AsyncLoadedScene.get());
                }

                if (m_OnAsyncReady) {
                    m_OnAsyncReady(m_AsyncLoadedScene.get());
                }
            }
        }
    }
}

// =============================================================================
// Internal Methods
// =============================================================================

void SceneLoader::SetProgress(LoadingPhase phase, float progress, const std::string& description) {
    std::lock_guard<std::mutex> lock(m_ProgressMutex);
    m_CurrentProgress.phase = phase;
    m_CurrentProgress.progress = progress;
    m_CurrentProgress.description = description;

    if (m_OnAsyncProgress) {
        m_OnAsyncProgress(m_CurrentProgress);
    }
}

std::unique_ptr<ECS::Scene> SceneLoader::ParseSceneFile(const std::string& scenePath) {
    namespace fs = std::filesystem;

    if (m_CancelRequested) return nullptr;

    SetProgress(LoadingPhase::LoadingAssets, 0.1f, "Loading scene file...");

    // Check file exists
    if (!fs::exists(scenePath)) {
        return nullptr;
    }

    // Read file
    std::ifstream file(scenePath);
    if (!file.is_open()) {
        return nullptr;
    }

    if (m_CancelRequested) return nullptr;

    SetProgress(LoadingPhase::LoadingAssets, 0.3f, "Parsing scene data...");

    // Parse JSON
    json sceneData;
    try {
        file >> sceneData;
    } catch (const json::parse_error& e) {
        (void)e;
        return nullptr;
    }

    if (m_CancelRequested) return nullptr;

    SetProgress(LoadingPhase::DeserializingEntities, 0.5f, "Creating entities...");

    // Create scene
    auto scene = std::make_unique<ECS::Scene>();

    // Parse scene name
    if (sceneData.contains("name")) {
        // Scene name available in JSON
    }

    // Parse entities
    if (sceneData.contains("entities")) {
        const auto& entities = sceneData["entities"];
        size_t total = entities.size();
        size_t current = 0;

        for (const auto& entityData : entities) {
            if (m_CancelRequested) return nullptr;

            // Create entity
            auto entity = scene->CreateEntity();

            // Parse components
            if (entityData.contains("components")) {
                const auto& components = entityData["components"];

                // Transform component
                if (components.contains("transform")) {
                    const auto& t = components["transform"];
                    auto& transform = scene->GetRegistry().emplace<ECS::TransformComponent>(entity);
                    
                    if (t.contains("position")) {
                        transform.Position = {
                            t["position"][0].get<float>(),
                            t["position"][1].get<float>(),
                            t["position"][2].get<float>()
                        };
                    }
                    if (t.contains("rotation")) {
                        transform.Rotation = {
                            t["rotation"][0].get<float>(),
                            t["rotation"][1].get<float>(),
                            t["rotation"][2].get<float>()
                        };
                    }
                    if (t.contains("scale")) {
                        transform.Scale = {
                            t["scale"][0].get<float>(),
                            t["scale"][1].get<float>(),
                            t["scale"][2].get<float>()
                        };
                    }
                }
            }

            current++;
            float entityProgress = 0.5f + 0.3f * (static_cast<float>(current) / static_cast<float>(total));
            SetProgress(LoadingPhase::DeserializingEntities, entityProgress, 
                        "Creating entity " + std::to_string(current) + "/" + std::to_string(total));
        }
    }

    SetProgress(LoadingPhase::InitializingSystems, 0.9f, "Initializing systems...");

    return scene;
}

} // namespace State
} // namespace Core
