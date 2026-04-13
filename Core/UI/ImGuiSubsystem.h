#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <functional>
#include <string>
#include <vector>
#include <chrono>

namespace Core {
    class Window;
    namespace RHI { class VulkanContext; }
namespace UI {

    /// @brief Performance statistics for display in debug overlay
    struct PerformanceStats {
        float fps = 0.0f;
        float frameTimeMs = 0.0f;
        float cpuTimeMs = 0.0f;
        float gpuTimeMs = 0.0f;
        size_t totalMemoryMB = 0;
        size_t usedMemoryMB = 0;
        uint32_t drawCalls = 0;
        uint32_t triangleCount = 0;
        uint32_t entityCount = 0;
    };

    /// @brief Configuration for ImGui overlay panels
    struct OverlayConfig {
        bool showPerformance = true;
        bool showMemory = false;
        bool showEntityInspector = false;
        bool showRenderStats = false;
        bool showContentDelivery = true;
        bool showUpscalingDiagnostics = true;
        bool showDemoWindow = false;
        float overlayAlpha = 0.85f;
    };

    /// @brief ImGui subsystem managing Dear ImGui integration with Vulkan
    /// 
    /// This class handles:
    /// - ImGui context initialization with Vulkan backend
    /// - SDL3 input event processing
    /// - Debug overlay rendering (FPS, memory, entity inspector)
    /// - Render pass integration after post-processing
    class ImGuiSubsystem {
    public:
        ImGuiSubsystem() = default;
        ~ImGuiSubsystem();

        // Delete copy/move to enforce singleton-like usage
        ImGuiSubsystem(const ImGuiSubsystem&) = delete;
        ImGuiSubsystem& operator=(const ImGuiSubsystem&) = delete;

        /// @brief Initialize ImGui with Vulkan backend
        /// @param vulkanContext The Vulkan context containing device/instance handles
        /// @param window The SDL window for input events
        /// @return true if initialization succeeded
        bool Initialize(RHI::VulkanContext* vulkanContext, Window* window);

        /// @brief Clean up all ImGui resources
        void Shutdown();

        /// @brief Process SDL event for ImGui input
        /// @param event The SDL event to process
        /// @return true if ImGui consumed the event
        bool ProcessEvent(const SDL_Event& event);

        /// @brief Begin a new ImGui frame - call at start of frame
        void BeginFrame();

        /// @brief Update performance statistics
        /// @param stats Current frame performance stats
        void UpdateStats(const PerformanceStats& stats);

        /// @brief Render ImGui draw data for a swapchain image
        /// @param commandBuffer The Vulkan command buffer to record into
        /// @param imageIndex Current swapchain image index
        void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        /// @brief End ImGui frame - call at end of frame
        void EndFrame();

        /// @brief Recreate ImGui framebuffers after swapchain resize/recreation
        void OnSwapchainRecreated();

        /// @brief Check if ImGui wants to capture keyboard input
        bool WantsKeyboardInput() const;

        /// @brief Check if ImGui wants to capture mouse input
        bool WantMouseInput() const;

        /// @brief Get/set overlay configuration
        OverlayConfig& GetConfig() { return m_Config; }
        const OverlayConfig& GetConfig() const { return m_Config; }

        /// @brief Set custom debug draw callback
        using DebugDrawCallback = std::function<void()>;
        void SetDebugDrawCallback(DebugDrawCallback callback) { m_DebugDrawCallback = std::move(callback); }

    private:
        /// @brief Create ImGui-specific Vulkan resources (descriptor pool, render pass)
        bool CreateVulkanResources();

        /// @brief Destroy ImGui Vulkan resources
        void DestroyVulkanResources();

        /// @brief Render the performance overlay panel
        void RenderPerformanceOverlay();

        /// @brief Render the memory statistics panel
        void RenderMemoryPanel();

        /// @brief Render the entity inspector panel
        void RenderEntityInspector();

        /// @brief Render the render statistics panel  
        void RenderRenderStats();

        /// @brief Render catalog/bundle/hot-reload diagnostics panel
        void RenderContentDeliveryPanel();

        /// @brief Render virtual geometry residency diagnostics section
        void RenderVirtualGeometryResidencySection();

        /// @brief Render upscaler/frame generation/trace diagnostics section
        void RenderUpscalingDiagnosticsSection();

    private:
        RHI::VulkanContext* m_VulkanContext = nullptr;
        Window* m_Window = nullptr;
        bool m_Initialized = false;

        // Vulkan resources for ImGui
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_Framebuffers;

        // Stats and config
        PerformanceStats m_Stats;
        OverlayConfig m_Config;

        // Frame timing
        std::chrono::high_resolution_clock::time_point m_LastFrameTime;
        float m_DeltaTime = 0.0f;

        // Custom debug callback
        DebugDrawCallback m_DebugDrawCallback;
    };

} // namespace UI
} // namespace Core
