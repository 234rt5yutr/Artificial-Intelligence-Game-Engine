#pragma once

#include <functional>
#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace Core {

// Forward declarations
namespace ECS {
    class Scene;
}

namespace State {

    /// @brief Transition types for scene changes
    enum class TransitionType {
        Fade,       ///< Fade to black and back
        FadeWhite,  ///< Fade to white and back
        Wipe,       ///< Horizontal wipe effect
        Circle,     ///< Circle iris in/out effect
        Dissolve,   ///< Pixelated dissolve effect
        Custom      ///< Custom shader-based transition
    };

    /// @brief Transition state
    enum class TransitionState {
        Idle,       ///< No transition active
        FadeOut,    ///< Fading out current scene
        Loading,    ///< Scene is loading (screen fully covered)
        FadeIn      ///< Fading in new scene
    };

    /// @brief Configuration for a transition
    struct TransitionConfig {
        TransitionType type = TransitionType::Fade;
        float fadeOutDuration = 0.5f;       ///< Time to fade out (seconds)
        float fadeInDuration = 0.5f;        ///< Time to fade in (seconds)
        float holdDuration = 0.0f;          ///< Time to hold at fully covered (extra wait)
        glm::vec4 color = {0, 0, 0, 1};     ///< Transition color (fade/wipe)
    };

    /// @brief Loading screen configuration
    struct LoadingScreenConfig {
        bool showProgressBar = true;
        bool showSpinner = true;
        bool showLoadingText = true;
        std::string loadingText = "Loading...";
        std::string backgroundImage;       ///< Optional background image path
    };

    // Callback types
    using TransitionCallback = std::function<void()>;

    /// @brief Manages scene transitions with visual effects
    /// 
    /// TransitionManager handles:
    /// - Visual transition effects between scenes
    /// - Loading screen display during scene loads
    /// - Configurable transition styles and timing
    /// - Callback hooks for transition events
    class TransitionManager {
    public:
        static TransitionManager& Get();

        // =====================================================================
        // Initialization
        // =====================================================================

        /// @brief Initialize the transition manager
        void Initialize();

        /// @brief Shutdown and cleanup
        void Shutdown();

        // =====================================================================
        // Transitions
        // =====================================================================

        /// @brief Start a transition to a new scene
        /// @param scenePath Path to the scene to load
        /// @param config Transition configuration
        void StartTransition(const std::string& scenePath, const TransitionConfig& config = {});

        /// @brief Start a transition with custom callbacks
        /// @param config Transition configuration
        /// @param onFadedOut Called when screen is fully covered
        /// @param onComplete Called when transition completes
        void StartCustomTransition(const TransitionConfig& config,
                                   TransitionCallback onFadedOut,
                                   TransitionCallback onComplete = nullptr);

        /// @brief Cancel current transition
        void CancelTransition();

        /// @brief Check if transition is active
        bool IsTransitioning() const { return m_State != TransitionState::Idle; }

        /// @brief Get current transition state
        TransitionState GetState() const { return m_State; }

        /// @brief Get transition progress (0.0 - 1.0)
        float GetProgress() const;

        /// @brief Get current transition alpha (for rendering overlay)
        float GetAlpha() const { return m_CurrentAlpha; }

        /// @brief Get transition color
        glm::vec4 GetColor() const { return m_Config.color; }

        // =====================================================================
        // Loading Screen
        // =====================================================================

        /// @brief Show loading screen manually
        /// @param config Loading screen configuration
        void ShowLoadingScreen(const LoadingScreenConfig& config = {});

        /// @brief Hide loading screen
        void HideLoadingScreen();

        /// @brief Check if loading screen is visible
        bool IsLoadingScreenVisible() const { return m_ShowLoadingScreen; }

        /// @brief Update loading progress for display
        /// @param progress 0.0 - 1.0
        /// @param message Status message
        void UpdateLoadingProgress(float progress, const std::string& message = "");

        /// @brief Get current loading progress
        float GetLoadingProgress() const { return m_LoadingProgress; }

        /// @brief Get current loading message
        const std::string& GetLoadingMessage() const { return m_LoadingMessage; }

        // =====================================================================
        // Update & Render
        // =====================================================================

        /// @brief Update transition state (call each frame)
        /// @param deltaTime Time since last frame in seconds
        void Update(float deltaTime);

        /// @brief Render transition overlay (call during UI pass)
        void Render();

    private:
        TransitionManager() = default;
        ~TransitionManager() = default;

        // Delete copy/move
        TransitionManager(const TransitionManager&) = delete;
        TransitionManager& operator=(const TransitionManager&) = delete;

        /// @brief Transition to next state
        void AdvanceState();

    private:
        bool m_Initialized = false;
        
        // Transition state
        TransitionState m_State = TransitionState::Idle;
        TransitionConfig m_Config;
        float m_StateTimer = 0.0f;
        float m_CurrentAlpha = 0.0f;
        
        std::string m_PendingScenePath;
        TransitionCallback m_OnFadedOut;
        TransitionCallback m_OnComplete;

        // Loading screen state
        bool m_ShowLoadingScreen = false;
        LoadingScreenConfig m_LoadingConfig;
        float m_LoadingProgress = 0.0f;
        std::string m_LoadingMessage;
        float m_SpinnerAngle = 0.0f;
    };

} // namespace State
} // namespace Core
