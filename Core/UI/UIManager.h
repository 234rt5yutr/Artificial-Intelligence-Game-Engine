#pragma once

#include "ImGuiSubsystem.h"
#include "TextRenderer.h"
#include "Anchoring.h"

#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <queue>
#include <chrono>

namespace Core {
    class Window;
    namespace RHI { class VulkanContext; }
    namespace ECS { class Scene; }
    namespace Editor { class EditorModule; }
namespace UI {

    // Forward declarations
    class WorldWidgetRenderer;
    namespace Widgets { class WidgetSystem; }

    /// @brief Message entry for on-screen display
    struct DisplayMessage {
        std::string text;
        float duration = 3.0f;
        float elapsed = 0.0f;
        glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        float fontSize = 24.0f;
        Anchor position = Anchor::Center;
        bool outline = true;
        
        enum class Type { Info, Warning, Error, Narrative, Tutorial, Success };
        Type type = Type::Info;
    };

    /// @brief Unified UI manager coordinating all UI subsystems
    /// 
    /// UIManager provides a single entry point for all UI operations:
    /// - ImGui debug overlays
    /// - MSDF text rendering
    /// - Widget system
    /// - World-space UI projection
    /// - Message display queue
    /// 
    /// Usage:
    /// @code
    /// // Initialize once at startup
    /// UIManager::Get().Initialize(vulkanContext, window, renderPass);
    /// 
    /// // Each frame
    /// UIManager::Get().BeginFrame();
    /// UIManager::Get().Update(deltaTime);
    /// // ... during render pass ...
    /// UIManager::Get().Render(commandBuffer);
    /// @endcode
    class UIManager {
    public:
        static UIManager& Get();

        // =====================================================================
        // Lifecycle
        // =====================================================================

        /// @brief Initialize all UI subsystems
        /// @param vulkanContext Vulkan context for resource creation
        /// @param window Window for input events and viewport size
        /// @param renderPass Render pass for pipeline creation
        void Initialize(RHI::VulkanContext* vulkanContext, Window* window, VkRenderPass renderPass);

        /// @brief Shutdown and release all resources
        void Shutdown();

        /// @brief Check if manager is initialized
        bool IsInitialized() const { return m_Initialized; }

        // =====================================================================
        // Per-Frame Operations
        // =====================================================================

        /// @brief Begin a new UI frame (call at start of frame)
        void BeginFrame();

        /// @brief Update all UI systems
        /// @param deltaTime Time since last frame in seconds
        void Update(float deltaTime);

        /// @brief Render all UI elements
        /// @param commandBuffer Active command buffer within render pass
        void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        /// @brief End the UI frame
        void EndFrame();

        // =====================================================================
        // Subsystem Access
        // =====================================================================

        /// @brief Get ImGui subsystem for debug overlays
        ImGuiSubsystem& GetImGui() { return *m_ImGui; }
        const ImGuiSubsystem& GetImGui() const { return *m_ImGui; }

        /// @brief Get text renderer for MSDF text
        TextRenderer& GetTextRenderer() { return *m_TextRenderer; }
        const TextRenderer& GetTextRenderer() const { return *m_TextRenderer; }

        /// @brief Get editor module when editor mode is enabled
        Editor::EditorModule* GetEditorModule() { return m_EditorModule.get(); }
        const Editor::EditorModule* GetEditorModule() const { return m_EditorModule.get(); }

        /// @brief Get widget runtime system (Stage 27)
        Widgets::WidgetSystem& GetWidgetSystem();

        // =====================================================================
        // Quick Text API
        // =====================================================================

        /// @brief Draw text at screen position
        void DrawText(std::string_view text, glm::vec2 screenPos, const TextStyle& style = {});

        /// @brief Draw text at 3D world position
        void DrawText3D(std::string_view text, glm::vec3 worldPos, const glm::mat4& viewProj,
                        const TextStyle& style = {}, bool billboard = true);

        /// @brief Draw text anchored to viewport
        void DrawTextAnchored(std::string_view text, Anchor anchor, glm::vec2 offset = {0, 0},
                              const TextStyle& style = {});

        // =====================================================================
        // Screen Messages (for MCP/narrative)
        // =====================================================================

        /// @brief Show a message on screen
        /// @param text Message text
        /// @param duration Display duration in seconds
        /// @param type Message type for styling
        void ShowMessage(std::string_view text, float duration = 3.0f, 
                        DisplayMessage::Type type = DisplayMessage::Type::Info);

        /// @brief Show message with custom style
        void ShowMessage(const DisplayMessage& message);

        /// @brief Clear all active messages
        void ClearMessages();

        /// @brief Get number of active messages
        size_t GetActiveMessageCount() const { return m_ActiveMessages.size(); }

        // =====================================================================
        // Viewport Management
        // =====================================================================

        /// @brief Handle viewport resize
        void OnResize(uint32_t width, uint32_t height);

        /// @brief Get current viewport size
        glm::vec2 GetViewportSize() const { return m_ViewportSize; }

        // =====================================================================
        // Input Handling
        // =====================================================================

        /// @brief Process input event
        /// @return true if UI consumed the event
        bool ProcessEvent(const SDL_Event& event);

        /// @brief Check if UI wants keyboard input
        bool WantsKeyboardInput() const;

        /// @brief Check if UI wants mouse input
        bool WantMouseInput() const;

        // =====================================================================
        // Configuration
        // =====================================================================

        /// @brief Set maximum concurrent messages
        void SetMaxMessages(size_t max) { m_MaxMessages = max; }

        /// @brief Enable/disable debug overlay
        void SetDebugOverlayEnabled(bool enabled);

        /// @brief Enable/disable editor panel rendering
        void SetEditorEnabled(bool enabled);

        /// @brief Bind active scene to editor module
        void SetEditorScene(ECS::Scene* scene);

        /// @brief Query whether editor module is enabled
        bool IsEditorEnabled() const;

    private:
        UIManager() = default;
        ~UIManager();

        // Delete copy/move
        UIManager(const UIManager&) = delete;
        UIManager& operator=(const UIManager&) = delete;

        /// @brief Get style color for message type
        glm::vec4 GetMessageTypeColor(DisplayMessage::Type type) const;

        /// @brief Get screen position for message
        glm::vec2 GetMessagePosition(const DisplayMessage& msg, size_t index) const;

        /// @brief Render active messages
        void RenderMessages();

    private:
        bool m_Initialized = false;
        RHI::VulkanContext* m_VulkanContext = nullptr;
        Window* m_Window = nullptr;
        glm::vec2 m_ViewportSize = {1920.0f, 1080.0f};

        // Subsystems
        std::unique_ptr<ImGuiSubsystem> m_ImGui;
        std::unique_ptr<TextRenderer> m_TextRenderer;
        std::unique_ptr<Editor::EditorModule> m_EditorModule;
        bool m_Stage27ServicesInitialized = false;

        // Message queue
        std::vector<DisplayMessage> m_ActiveMessages;
        size_t m_MaxMessages = 5;

        // Frame state
        bool m_InFrame = false;
        float m_DeltaTime = 0.0f;
    };

} // namespace UI
} // namespace Core
