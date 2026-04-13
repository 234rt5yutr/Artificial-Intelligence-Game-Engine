#include "Application.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/Assert.h"
#include "Core/Input.h"
#include "Core/Renderer/Diagnostics/GPUFrameTraceService.h"
#include "Core/Renderer/Upscaling/TemporalUpscalerManager.h"
#include "Core/UI/UIManager.h"
#include "Core/Asset/HotReload/AssetHotReloadService.h"
#include <thread>
#include <chrono>

namespace Core {

    Application* Application::s_Instance = nullptr;
    Application::RuntimeOptions Application::s_RuntimeOptions{};

    void Application::SetRuntimeOptions(const RuntimeOptions& options) {
        s_RuntimeOptions = options;
    }

    const Application::RuntimeOptions& Application::GetRuntimeOptions() {
        return s_RuntimeOptions;
    }

    Application::Application() {
        PROFILE_FUNCTION();
        ENGINE_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;
        m_HeadlessRuntime =
            s_RuntimeOptions.Headless ||
            s_RuntimeOptions.DisableRenderer ||
            s_RuntimeOptions.Profile == RuntimeOptions::RuntimeProfile::DedicatedServer;

        if (!m_HeadlessRuntime) {
            m_Window = std::make_unique<Window>(WindowProps("AIGameEngine", 1280, 720));
            m_Window->SetEventCallback(std::bind(&Application::OnEvent, this, std::placeholders::_1));

            Input::Init();
            m_VulkanContext = std::make_unique<RHI::VulkanContext>(m_Window.get());
            m_VulkanContext->Init();

            if (!s_RuntimeOptions.DisableUI) {
                // Initialize debug/development UI
                UI::UIManager::Get().Initialize(
                    m_VulkanContext.get(),
                    m_Window.get(),
                    m_VulkanContext->GetRenderPass()
                );

                if (UI::UIManager::Get().IsInitialized()) {
                    UI::UIManager::Get().SetDebugOverlayEnabled(true);
                    // Keep the interactive demo visible to confirm UI input works in release.
                    UI::UIManager::Get().GetImGui().GetConfig().showDemoWindow = true;
                    UI::UIManager::Get().GetImGui().GetConfig().showRenderStats = true;
                }
            }
        }

        ApplyRuntimeOptions();
    }

    Application::~Application() {
        UI::UIManager::Get().Shutdown();
    }

    void Application::Close() {
        m_Running = false;
    }

    void Application::Run() {
        PROFILE_FUNCTION();
        ENGINE_CORE_INFO("Application initialized and running.");

        if (m_HeadlessRuntime) {
            while (m_Running) {
                Asset::HotReload::AssetHotReloadService::Get().PumpFrameSafePoint();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ENGINE_CORE_INFO("Application shutting down.");
            return;
        }

        auto lastFrameTime = std::chrono::high_resolution_clock::now();

        while (m_Running) {
            PROFILE_SCOPE("Application Loop");

            m_Window->OnUpdate();
            Asset::HotReload::AssetHotReloadService::Get().PumpFrameSafePoint();

            auto now = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;

            auto& uiManager = UI::UIManager::Get();
            uiManager.BeginFrame();
            uiManager.Update(deltaTime);

            if (m_StartupTraceCapturePending) {
                CaptureRuntimeTraceNow(
                    s_RuntimeOptions.StartupTraceOutputPath.empty() ? std::filesystem::path("build/diagnostics") : s_RuntimeOptions.StartupTraceOutputPath,
                    "startup");
                m_StartupTraceCapturePending = false;
            }

            if (m_VulkanContext) {
                m_VulkanContext->DrawFrame();
            }

            uiManager.EndFrame();
        }

        ENGINE_CORE_INFO("Application shutting down.");
    }

    void Application::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(std::bind(&Application::OnWindowClose, this, std::placeholders::_1));
        dispatcher.Dispatch<WindowResizeEvent>(std::bind(&Application::OnWindowResize, this, std::placeholders::_1));
        dispatcher.Dispatch<KeyPressedEvent>(std::bind(&Application::OnKeyPress, this, std::placeholders::_1));
    }

    bool Application::OnWindowClose(WindowCloseEvent& /*e*/) {
        Close();
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e) {
        if (e.GetWidth() == 0 || e.GetHeight() == 0) {
            return true;
        }

        if (m_VulkanContext) {
            m_VulkanContext->RecreateSwapchain(e.GetWidth(), e.GetHeight());
        }

        return true;
    }

    bool Application::OnKeyPress(KeyPressedEvent& e) {
        if (m_HeadlessRuntime) {
            return false;
        }

        // Don't hijack keyboard if UI currently owns input.
        if (UI::UIManager::Get().WantsKeyboardInput()) {
            return false;
        }

        if (e.GetKeyCode() == SDLK_F9) {
            CaptureRuntimeTraceNow(std::filesystem::path("build/diagnostics"), "manual-hotkey");
            return true;
        }

        // Use the native input polling if you want, but since we get the event:
        // We'll close the app when Escape is pressed.
        // SDL3 defines SDLK_ESCAPE.
        if (e.GetKeyCode() == SDLK_ESCAPE) {
            Close();
            return true;
        }
        return false;
    }

    void Application::ApplyRuntimeOptions() {
        if (m_VulkanContext == nullptr) {
            return;
        }

        Renderer::GetGPUFrameTraceService().SetVulkanContext(m_VulkanContext.get());

        if (s_RuntimeOptions.EnableStartupWarmupMode) {
            m_VulkanContext->SetFrameMarkerEnabled(true);
            ENGINE_CORE_INFO("Startup warmup mode enabled");
        }

        const std::string preferred = s_RuntimeOptions.PreferredUpscalerBackend;
        if (preferred == "fsr2") {
            Renderer::TemporalUpscalerFSR2Config config{};
            config.Enabled = true;
            config.RuntimeFeatureEnabled = true;
            config.BackendAvailable = true;
            (void)Renderer::GetTemporalUpscalerManager().SetTemporalUpscalerFSR2(config);
        } else if (preferred == "dlss") {
            Renderer::TemporalUpscalerDLSSConfig config{};
            config.Enabled = true;
            config.RuntimeFeatureEnabled = true;
            config.BackendAvailable = true;
            (void)Renderer::GetTemporalUpscalerManager().SetTemporalUpscalerDLSS(config);
        } else if (preferred == "xess") {
            Renderer::TemporalUpscalerXeSSConfig config{};
            config.Enabled = true;
            config.RuntimeFeatureEnabled = true;
            config.BackendAvailable = true;
            (void)Renderer::GetTemporalUpscalerManager().SetTemporalUpscalerXeSS(config);
        }

        m_StartupTraceCapturePending = s_RuntimeOptions.CaptureStartupGPUTrace;
    }

    void Application::CaptureRuntimeTraceNow(const std::filesystem::path& outputPath, const std::string& frameTag) {
        Renderer::GPUFrameTraceRequest request{};
        request.FrameCount = 1;
        request.IncludeMarkers = true;
        request.IncludePipelineStats = true;
        request.OutputPath = outputPath;
        request.FrameTag = frameTag;

        const auto captureResult = Renderer::GetGPUFrameTraceService().CaptureGPUFrameTrace(request);
        if (!captureResult.Ok) {
            ENGINE_CORE_WARN("GPU frame trace capture failed: {}", captureResult.Error);
            return;
        }
        ENGINE_CORE_INFO("GPU frame trace captured: {}", captureResult.Value.JsonArtifactPath.string());
    }

} // namespace Core
