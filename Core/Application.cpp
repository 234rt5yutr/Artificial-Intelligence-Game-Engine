#include "Application.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/Assert.h"
#include "Core/Input.h"
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
    }

    Application::~Application() {
    }

    void Application::Close() {
        m_Running = false;
    }

    void Application::Run() {
        PROFILE_FUNCTION();
        ENGINE_CORE_INFO("Application initialized and running.");

        while (m_Running) {
            PROFILE_SCOPE("Application Loop");

            m_Window->OnUpdate();
            if (m_VulkanContext) m_VulkanContext->DrawFrame();
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
            return false;
        }

        // Notify renderer of resize later
        return false;
    }

    bool Application::OnKeyPress(KeyPressedEvent& e) {
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
