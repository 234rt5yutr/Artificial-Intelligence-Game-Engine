#include "Application.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/RHI/Vulkan/VulkanDevice.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/Assert.h"
#include "Core/Input.h"
#include "Core/Renderer/Diagnostics/GPUFrameTraceService.h"
#include "Core/Diagnostics/GPUProfilerCapture.h"
#include "Core/Renderer/Upscaling/TemporalUpscalerManager.h"
#include "Core/UI/UIManager.h"
#include "Core/Editor/EditorModule.h"
#include "Core/Asset/HotReload/AssetHotReloadService.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/SystemPipeline.h"
#include "Core/ECS/Systems/RenderSystem.h"
#include "Core/ECS/Systems/CameraSystem.h"
#include "Core/ECS/Systems/LightSystem.h"
#include "Core/Renderer/RenderThread.h"
#include "Core/Renderer/SceneRenderer.h"
#include "Core/MCP/MCPServer.h"
#include "Core/MCP/MCPToolFactory.h"
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

        m_RuntimeScene = std::make_unique<ECS::Scene>("Runtime Scene");

        // Bring up the simulation systems. Previously the scene only ticked its UI,
        // so physics/animation/cameras/transforms never ran at all.
        {
            ECS::SystemPipelineConfig pipelineConfig{};
            if (m_Window) {
                pipelineConfig.ScreenWidth = m_Window->GetWidth();
                pipelineConfig.ScreenHeight = m_Window->GetHeight();
            }
            // Terrain/foliage/sky need an RHI device. Headless runs have no Vulkan
            // context, so they get nullptr and those systems stay off.
            if (m_VulkanContext) {
                m_RHIDevice = std::make_shared<RHI::VulkanDevice>(m_VulkanContext.get());
            }
            m_RuntimeScene->InitializeSystems(pipelineConfig, m_RHIDevice);
        }

        if (!m_HeadlessRuntime && UI::UIManager::Get().IsInitialized()) {
            m_RuntimeScene->BindUIManager(&UI::UIManager::Get());
            UI::UIManager::Get().SetEditorScene(m_RuntimeScene.get());
        }
        if (s_RuntimeOptions.EnableMCPServer) {
            MCP::MCPServerConfig mcpConfig{};
            mcpConfig.Host = s_RuntimeOptions.MCPHost;
            mcpConfig.Port = s_RuntimeOptions.MCPPort;
            mcpConfig.AuthToken = s_RuntimeOptions.MCPAuthToken;
            mcpConfig.AllowedOrigins = s_RuntimeOptions.MCPAllowedOrigins;
            if (mcpConfig.Host != "127.0.0.1" && mcpConfig.Host != "localhost" &&
                mcpConfig.AuthToken.empty()) {
                ENGINE_CORE_WARN("MCP server is bound to '{}' without an auth token; "
                                 "anyone who can reach that address can drive the engine. "
                                 "Pass --mcp-token=<secret>.", mcpConfig.Host);
            }
            m_MCPServer = std::make_unique<MCP::MCPServer>(mcpConfig);
            m_MCPServer->SetActiveScene(m_RuntimeScene.get());
            // Without this the runtime server exposes zero tools and only answers
            // initialize/ping/tools-list.
            for (auto& tool : MCP::CreateAllMCPTools()) {
                m_MCPServer->RegisterTool(std::move(tool));
            }
            if (!m_MCPServer->Start()) {
                ENGINE_CORE_ERROR("Failed to start MCP server at {}:{}", mcpConfig.Host, mcpConfig.Port);
                m_MCPServer.reset();
            } else {
                ENGINE_CORE_INFO("MCP server started at {}", m_MCPServer->GetEndpointUrl());
            }
        } else {
            ENGINE_CORE_INFO("MCP server startup disabled via runtime options");
        }
        ENGINE_CORE_INFO("Stage 27 runtime scene and UISystem initialized");

        ApplyRuntimeOptions();
    }

    Application::~Application() {
        // Nothing below is safe while frames are still in flight.
        if (m_RenderThread) {
            m_RenderThread->Stop();
            m_RenderThread.reset();
        }
        if (m_MCPServer) {
            // Stop serving before the scene goes away: tools hold a raw Scene pointer.
            m_MCPServer->SetActiveScene(nullptr);
            m_MCPServer->Stop();
            m_MCPServer.reset();
        }
        if (m_RuntimeScene) {
            m_RuntimeScene->ShutdownSystems();
        }
        // Released after the systems that hold it, before the Vulkan context it wraps.
        m_RHIDevice.reset();
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
                if (m_MCPServer) {
                    m_MCPServer->ProcessPendingRequests();
                }
                Asset::HotReload::AssetHotReloadService::Get().PumpFrameSafePoint();
                if (m_RuntimeScene) {
                    m_RuntimeScene->OnUpdate(0.0f);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ENGINE_CORE_INFO("Application shutting down.");
            return;
        }

        m_RenderThread = std::make_unique<Renderer::RenderThread>();
        if (m_VulkanContext && s_RuntimeOptions.EnableRenderThread) {
            m_RenderThread->Start(m_VulkanContext.get());
        } else if (m_VulkanContext) {
            ENGINE_CORE_INFO("Render thread disabled; submission runs inline on the main thread");
        }

        auto lastFrameTime = std::chrono::high_resolution_clock::now();

        while (m_Running) {
            PROFILE_SCOPE("Application Loop");

            // Events only. A resize is recorded and applied at the sync point
            // below, because recreating the swapchain under a recording render
            // thread would destroy objects it is still using.
            m_Window->OnUpdate();

            auto now = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;
            m_ElapsedSeconds += deltaTime;

            // Simulation for this frame overlaps the render thread's submission
            // of the previous one. Nothing in here touches renderer state.
            if (m_RuntimeScene) {
                m_RuntimeScene->OnUpdateSimulation(deltaTime);
            }

            // --- sync point: the renderer is idle from here to SubmitFrame ---
            m_RenderThread->WaitForFrame();

            if (m_PendingResize && m_VulkanContext) {
                m_VulkanContext->RecreateSwapchain(m_PendingResizeWidth, m_PendingResizeHeight);
                if (m_RuntimeScene) {
                    if (auto* pipeline = m_RuntimeScene->GetSystemPipeline()) {
                        pipeline->SetScreenDimensions(m_PendingResizeWidth, m_PendingResizeHeight);
                    }
                }
                m_PendingResize = false;
            }

            // MCP tools drive the scene and the renderer, so they run inside the
            // idle window rather than racing the render thread.
            if (m_MCPServer) {
                m_MCPServer->ProcessPendingRequests();
            }
            Asset::HotReload::AssetHotReloadService::Get().PumpFrameSafePoint();

            auto& uiManager = UI::UIManager::Get();
            uiManager.BeginFrame();
            if (m_RuntimeScene) {
                m_RuntimeScene->OnUpdateUI();
            }
            uiManager.Update(deltaTime);

            if (m_StartupTraceCapturePending) {
                CaptureRuntimeTraceNow(
                    s_RuntimeOptions.StartupTraceOutputPath.empty() ? std::filesystem::path("build/diagnostics") : s_RuntimeOptions.StartupTraceOutputPath,
                    "startup");
                m_StartupTraceCapturePending = false;
            }

            if (m_VulkanContext) {
                Renderer::FrameRenderData frame;
                BuildFrameRenderData(frame, deltaTime);
                // Hands off and returns; with no render thread this renders
                // inline, so there is only one call site either way.
                m_RenderThread->SubmitFrame(std::move(frame));
            } else {
                uiManager.EndFrame();
            }

            ++m_FrameIndex;
        }

        // Drain before any of the systems the render thread reads go away.
        m_RenderThread->Stop();
        m_RenderThread.reset();

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

        // Deferred: the swapchain can only be recreated while the render thread
        // is idle, which the main loop guarantees at its sync point.
        m_PendingResize = true;
        m_PendingResizeWidth = e.GetWidth();
        m_PendingResizeHeight = e.GetHeight();

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
        Diagnostics::GetGPUProfilerCaptureService().SetVulkanContext(m_VulkanContext.get());

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

    void Application::BuildFrameRenderData(Renderer::FrameRenderData& frame, float deltaTime) {
        (void)deltaTime;

        frame.FrameIndex = m_FrameIndex;
        frame.TimeSeconds = m_ElapsedSeconds;

        const auto* pipeline = m_RuntimeScene ? m_RuntimeScene->GetSystemPipeline() : nullptr;
        if (!pipeline) {
            return;
        }

        // Copied, not referenced: RenderSystem rebuilds these vectors during the
        // next simulation step, which by design runs while this frame is still
        // being submitted.
        if (const auto* renderSystem = pipeline->GetRenderSystem()) {
            frame.DrawCommands = renderSystem->GetDrawCommands();
        }
        if (const auto* lightSystem = pipeline->GetLightSystem()) {
            frame.DirectionalLights = lightSystem->GetDirectionalLights();
            frame.PointLights = lightSystem->GetPointLights();
            frame.SpotLights = lightSystem->GetSpotLights();
        }
        if (const auto* cameraSystem = pipeline->GetCameraSystem()) {
            frame.View = cameraSystem->GetViewMatrix();
            frame.Projection = cameraSystem->GetProjectionMatrix();
            frame.ViewProjection = cameraSystem->GetViewProjectionMatrix();
            frame.CameraPosition = Math::Vec3(glm::inverse(frame.View)[3]);
        }

        // The editor viewport can fly its own camera; when it is driving, its
        // matrices replace the scene camera's for the whole frame so culling,
        // shading, and picking all agree on one view.
        if (auto* editorModule = UI::UIManager::Get().GetEditorModule()) {
            Math::Mat4 editorView(1.0f);
            Math::Mat4 editorProjection(1.0f);
            if (editorModule->GetViewportPanel().GetCameraOverride(editorView, editorProjection)) {
                frame.View = editorView;
                frame.Projection = editorProjection;
                frame.ViewProjection = editorProjection * editorView;
                frame.CameraPosition = Math::Vec3(glm::inverse(editorView)[3]);
            }
        }

        // Sub-pixel jitter for the temporal upscaler. Applied to the projection
        // here so the culling matrices and the draw matrices agree.
        if (auto* sceneRenderer = m_VulkanContext->GetSceneRenderer()) {
            const Math::Vec2 jitter = sceneRenderer->GetProjectionJitter(m_FrameIndex);
            if (jitter.x != 0.0f || jitter.y != 0.0f) {
                frame.Projection[2][0] += jitter.x;
                frame.Projection[2][1] += jitter.y;
                frame.ViewProjection = frame.Projection * frame.View;
            }
        }
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
