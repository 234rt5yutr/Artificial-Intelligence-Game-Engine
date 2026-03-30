#pragma once

#include "Core/Window.h"
#include <memory>

namespace Core {

    class Application {
    public:
        Application();
        virtual ~Application();

        // Delete copy constructors (Singleton)
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        void Run();
        void Close();

        static Application& Get() { return *s_Instance; }
        
        inline Core::Window& GetWindow() { return *m_Window; }

    protected:
        bool m_Running = true;
        std::unique_ptr<Core::Window> m_Window;

    private:
        static Application* s_Instance;
    };

    // To be defined in the CLIENT to provide the specific application instance
    Application* CreateApplication();

} // namespace Core
