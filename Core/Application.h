#pragma once

#include "Core/Window.h"
#include "Core/Event.h"
#include <filesystem>
#include <memory>
#include <string>

namespace Core {
    namespace RHI { class VulkanContext; }


    class Application {
    public:
        struct RuntimeOptions {
            bool EnableStartupWarmupMode = false;
            std::string PreferredUpscalerBackend;
            bool CaptureStartupGPUTrace = false;
            std::filesystem::path StartupTraceOutputPath;
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
        
        inline Core::Window& GetWindow() { return *m_Window; }

    protected:
        bool m_Running = true;
        std::unique_ptr<Core::Window> m_Window;
        std::unique_ptr<RHI::VulkanContext> m_VulkanContext;

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
        bool OnKeyPress(KeyPressedEvent& e);
        void ApplyRuntimeOptions();
        void CaptureRuntimeTraceNow(const std::filesystem::path& outputPath, const std::string& frameTag);

    private:
        static Application* s_Instance;
        static RuntimeOptions s_RuntimeOptions;
        bool m_StartupTraceCapturePending = false;
    };

    // To be defined in the CLIENT to provide the specific application instance
    Application* CreateApplication();

} // namespace Core

