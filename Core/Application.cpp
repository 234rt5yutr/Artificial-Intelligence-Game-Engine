#include "Application.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/Assert.h"
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
    }

    Application::~Application() {
        PROFILE_FUNCTION();
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

            // Prevent pegging the CPU to 100% since loop is empty
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        ENGINE_CORE_INFO("Application shutting down.");
    }

    void Application::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(std::bind(&Application::OnWindowClose, this, std::placeholders::_1));
    }

    bool Application::OnWindowClose(WindowCloseEvent& e) {
        Close();
        return true;
    }

} // namespace Core
