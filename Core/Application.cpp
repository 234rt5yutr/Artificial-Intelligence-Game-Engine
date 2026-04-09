#include "Application.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/Assert.h"
#include "Core/Input.h"
#include "Core/UI/UIManager.h"
#include <thread>
#include <chrono>

namespace Core {

    Application* Application::s_Instance = nullptr;

    Application::Application() {
        PROFILE_FUNCTION();
        ENGINE_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;

        m_Window = std::make_unique<Window>(WindowProps("AIGameEngine", 1280, 720));
        m_Window->SetEventCallback(std::bind(&Application::OnEvent, this, std::placeholders::_1));

        Input::Init();
        m_VulkanContext = std::make_unique<RHI::VulkanContext>(m_Window.get());
        m_VulkanContext->Init();

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

    Application::~Application() {
        UI::UIManager::Get().Shutdown();
    }

    void Application::Close() {
        m_Running = false;
    }

    void Application::Run() {
        PROFILE_FUNCTION();
        ENGINE_CORE_INFO("Application initialized and running.");

        auto lastFrameTime = std::chrono::high_resolution_clock::now();

        while (m_Running) {
            PROFILE_SCOPE("Application Loop");

            m_Window->OnUpdate();

            auto now = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;

            auto& uiManager = UI::UIManager::Get();
            uiManager.BeginFrame();
            uiManager.Update(deltaTime);

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
        // Don't hijack keyboard if UI currently owns input.
        if (UI::UIManager::Get().WantsKeyboardInput()) {
            return false;
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

} // namespace Core
