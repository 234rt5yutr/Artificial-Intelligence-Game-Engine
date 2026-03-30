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

            // Placeholder for real tick logic (Update, Render, Poll Events)
            // Once we have a Window and Event system, we will pump events here


            // Prevent pegging the CPU to 100% since loop is empty
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        ENGINE_CORE_INFO("Application shutting down.");
    }

} // namespace Core
