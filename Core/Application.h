#pragma once

#include "Core/Window.h"
#include "Core/Event.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Core {
    namespace RHI { class VulkanContext; class VulkanDevice; }
    namespace ECS { class Scene; }
    namespace MCP { class MCPServer; }
    namespace Renderer { class RenderThread; struct FrameRenderData; }


    class Application {
    public:
        struct RuntimeOptions {
            enum class RuntimeProfile : uint8_t {
                Client = 0,
                ListenServer,
                DedicatedServer
            };

            bool EnableStartupWarmupMode = false;
            std::string PreferredUpscalerBackend;
            bool CaptureStartupGPUTrace = false;
            std::filesystem::path StartupTraceOutputPath;
            RuntimeProfile Profile = RuntimeProfile::Client;
            bool Headless = false;
            bool DisableRenderer = false;
            bool DisableUI = false;
            bool EnableMCPServer = true;
            std::string MCPHost = "127.0.0.1";
            int MCPPort = 3000;
            // Optional bearer token required on every MCP request. Empty means the
            // loopback binding is the only protection, which is fine locally but not
            // if MCPHost is widened.
            std::string MCPAuthToken;
            // Browser origins permitted to call the MCP endpoint. Empty rejects all
            // origin-bearing (browser) requests, which is the safe default.
            std::vector<std::string> MCPAllowedOrigins;
            // Run Vulkan submission on its own thread so the simulation for the
            // next frame overlaps the current frame's submission.
            bool EnableRenderThread = true;
        };

        Application();
        virtual ~Application();

        // Delete copy constructors (Singleton)
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        void Run();
        void Close();
        
        void OnEvent(Event& e);
        static void SetRuntimeOptions(const RuntimeOptions& options);
        static const RuntimeOptions& GetRuntimeOptions();

        static Application& Get() { return *s_Instance; }
        // Null before construction and in unit tests that never build one, so
        // tool code that may run headless uses this instead of Get().
        static Application* TryGet() { return s_Instance; }
        
        inline Core::Window& GetWindow() { return *m_Window; }
        ECS::Scene* GetRuntimeScene() const { return m_RuntimeScene.get(); }
        RHI::VulkanContext* GetVulkanContext() const { return m_VulkanContext.get(); }
        Renderer::RenderThread* GetRenderThread() const { return m_RenderThread.get(); }
        // Needed by tools that build geometry at runtime; null headless.
        std::shared_ptr<RHI::VulkanDevice> GetRHIDevice() const { return m_RHIDevice; }

    protected:
        bool m_Running = true;
        std::unique_ptr<Core::Window> m_Window;
        std::unique_ptr<RHI::VulkanContext> m_VulkanContext;
        // Concrete RHIDevice over the Vulkan context; handed to the scene so
        // terrain/foliage/sky can upload GPU resources.
        std::shared_ptr<RHI::VulkanDevice> m_RHIDevice;
        std::unique_ptr<ECS::Scene> m_RuntimeScene;
        std::unique_ptr<MCP::MCPServer> m_MCPServer;
        std::unique_ptr<Renderer::RenderThread> m_RenderThread;

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
        bool OnKeyPress(KeyPressedEvent& e);
        void ApplyRuntimeOptions();
        void CaptureRuntimeTraceNow(const std::filesystem::path& outputPath, const std::string& frameTag);
        // Copies this frame's draw list, lights, and camera out of the scene so
        // the render thread never reads live ECS state.
        void BuildFrameRenderData(Renderer::FrameRenderData& frame, float deltaTime);

    private:
        static Application* s_Instance;
        static RuntimeOptions s_RuntimeOptions;
        bool m_StartupTraceCapturePending = false;
        bool m_HeadlessRuntime = false;
        // Resizes arrive on the event thread but can only be applied when the
        // renderer is idle, so they are deferred to the frame's sync point.
        bool m_PendingResize = false;
        uint32_t m_PendingResizeWidth = 0;
        uint32_t m_PendingResizeHeight = 0;
        uint64_t m_FrameIndex = 0;
        float m_ElapsedSeconds = 0.0f;
    };

    // To be defined in the CLIENT to provide the specific application instance
    Application* CreateApplication();

} // namespace Core

