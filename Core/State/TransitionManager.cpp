#include "TransitionManager.h"
#include "SceneLoader.h"
#include <algorithm>

namespace Core {
namespace State {

// =============================================================================
// Singleton Access
// =============================================================================

TransitionManager& TransitionManager::Get() {
    static TransitionManager instance;
    return instance;
}

// =============================================================================
// Initialization
// =============================================================================

void TransitionManager::Initialize() {
    if (m_Initialized) return;
    m_Initialized = true;
}

void TransitionManager::Shutdown() {
    if (!m_Initialized) return;
    
    CancelTransition();
    m_Initialized = false;
}

// =============================================================================
// Transitions
// =============================================================================

void TransitionManager::StartTransition(const std::string& scenePath, 
                                         const TransitionConfig& config) {
    if (m_State != TransitionState::Idle) {
        CancelTransition();
    }

    m_Config = config;
    m_PendingScenePath = scenePath;
    m_State = TransitionState::FadeOut;
    m_StateTimer = config.fadeOutDuration;
    m_CurrentAlpha = 0.0f;

    m_OnFadedOut = [this, scenePath]() {
        // Start loading the scene
        m_State = TransitionState::Loading;
        m_ShowLoadingScreen = true;
        
        SceneLoader::Get().LoadSceneAsync(scenePath,
            [this](ECS::Scene* scene) {
                // Scene loaded - start fade in
                (void)scene;
                m_ShowLoadingScreen = false;
                m_State = TransitionState::FadeIn;
                m_StateTimer = m_Config.fadeInDuration;
            },
            [this](const LoadingProgress& progress) {
                UpdateLoadingProgress(progress.progress, progress.description);
            });
    };

    m_OnComplete = nullptr;
}

void TransitionManager::StartCustomTransition(const TransitionConfig& config,
                                               TransitionCallback onFadedOut,
                                               TransitionCallback onComplete) {
    if (m_State != TransitionState::Idle) {
        CancelTransition();
    }

    m_Config = config;
    m_State = TransitionState::FadeOut;
    m_StateTimer = config.fadeOutDuration;
    m_CurrentAlpha = 0.0f;
    m_OnFadedOut = std::move(onFadedOut);
    m_OnComplete = std::move(onComplete);
}

void TransitionManager::CancelTransition() {
    m_State = TransitionState::Idle;
    m_StateTimer = 0.0f;
    m_CurrentAlpha = 0.0f;
    m_ShowLoadingScreen = false;
    m_PendingScenePath.clear();
    m_OnFadedOut = nullptr;
    m_OnComplete = nullptr;
}

float TransitionManager::GetProgress() const {
    switch (m_State) {
        case TransitionState::Idle:
            return 0.0f;
        case TransitionState::FadeOut: {
            float elapsed = m_Config.fadeOutDuration - m_StateTimer;
            return (elapsed / m_Config.fadeOutDuration) * 0.5f;
        }
        case TransitionState::Loading:
            return 0.5f;
        case TransitionState::FadeIn: {
            float elapsed = m_Config.fadeInDuration - m_StateTimer;
            return 0.5f + (elapsed / m_Config.fadeInDuration) * 0.5f;
        }
    }
    return 0.0f;
}

// =============================================================================
// Loading Screen
// =============================================================================

void TransitionManager::ShowLoadingScreen(const LoadingScreenConfig& config) {
    m_ShowLoadingScreen = true;
    m_LoadingConfig = config;
    m_LoadingProgress = 0.0f;
    m_LoadingMessage = config.loadingText;
    m_SpinnerAngle = 0.0f;
}

void TransitionManager::HideLoadingScreen() {
    m_ShowLoadingScreen = false;
}

void TransitionManager::UpdateLoadingProgress(float progress, const std::string& message) {
    m_LoadingProgress = std::clamp(progress, 0.0f, 1.0f);
    if (!message.empty()) {
        m_LoadingMessage = message;
    }
}

// =============================================================================
// Update & Render
// =============================================================================

void TransitionManager::Update(float deltaTime) {
    if (m_State == TransitionState::Idle) {
        return;
    }

    // Update spinner
    if (m_ShowLoadingScreen && m_LoadingConfig.showSpinner) {
        m_SpinnerAngle += deltaTime * 360.0f; // 1 rotation per second
        if (m_SpinnerAngle >= 360.0f) {
            m_SpinnerAngle -= 360.0f;
        }
    }

    // Update transition timer
    if (m_State == TransitionState::FadeOut || m_State == TransitionState::FadeIn) {
        m_StateTimer -= deltaTime;

        // Calculate alpha based on state
        if (m_State == TransitionState::FadeOut) {
            float elapsed = m_Config.fadeOutDuration - m_StateTimer;
            m_CurrentAlpha = std::clamp(elapsed / m_Config.fadeOutDuration, 0.0f, 1.0f);
            
            if (m_StateTimer <= 0.0f) {
                m_CurrentAlpha = 1.0f;
                AdvanceState();
            }
        } else if (m_State == TransitionState::FadeIn) {
            float elapsed = m_Config.fadeInDuration - m_StateTimer;
            m_CurrentAlpha = 1.0f - std::clamp(elapsed / m_Config.fadeInDuration, 0.0f, 1.0f);
            
            if (m_StateTimer <= 0.0f) {
                m_CurrentAlpha = 0.0f;
                AdvanceState();
            }
        }
    } else if (m_State == TransitionState::Loading) {
        m_CurrentAlpha = 1.0f; // Fully covered during loading
    }
}

void TransitionManager::AdvanceState() {
    switch (m_State) {
        case TransitionState::FadeOut:
            // Transition completed fade out
            if (m_OnFadedOut) {
                m_OnFadedOut();
            }
            
            // If not loading a scene, check for hold duration
            if (m_PendingScenePath.empty()) {
                if (m_Config.holdDuration > 0.0f) {
                    m_State = TransitionState::Loading;
                    m_StateTimer = m_Config.holdDuration;
                } else {
                    m_State = TransitionState::FadeIn;
                    m_StateTimer = m_Config.fadeInDuration;
                }
            }
            break;

        case TransitionState::Loading:
            // Loading complete, start fade in
            m_State = TransitionState::FadeIn;
            m_StateTimer = m_Config.fadeInDuration;
            break;

        case TransitionState::FadeIn:
            // Transition complete
            m_State = TransitionState::Idle;
            m_PendingScenePath.clear();
            if (m_OnComplete) {
                m_OnComplete();
                m_OnComplete = nullptr;
            }
            break;

        case TransitionState::Idle:
            break;
    }
}

void TransitionManager::Render() {
    if (m_State == TransitionState::Idle && !m_ShowLoadingScreen) {
        return;
    }

    // Render transition overlay
    // This would normally use ImGui or a custom shader to draw a full-screen quad
    // The UIManager or ImGuiSubsystem will handle the actual rendering
    
    // For now, transition data is exposed via GetAlpha(), GetColor(), etc.
    // which UI systems can query to render the overlay
}

} // namespace State
} // namespace Core
