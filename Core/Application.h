#pragma once

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

    protected:
        bool m_Running = true;

    private:
        static Application* s_Instance;
    };

    // To be defined in the CLIENT to provide the specific application instance
    Application* CreateApplication();

} // namespace Core
