#pragma once

#include "Core/Window.h"
#include "Core/Event.h"
#include <memory>

namespace Core {
    namespace RHI { class VulkanContext; }


    class Application {
    public:
        Application();
        virtual ~Application();

        // Delete copy constructors (Singleton)
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        void Run();
        void Close();
        
        void OnEvent(Event& e);

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

    private:
        static Application* s_Instance;
    };

    // To be defined in the CLIENT to provide the specific application instance
    Application* CreateApplication();

} // namespace Core

