#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/CrashHandler.h"
#include "Core/Application.h"

// Subclass Application for the game client
class SandboxApp : public Core::Application {
public:
    SandboxApp() {
        // [TEMPORARY] Close instantly so the terminal doesn't hang forever 
        // since we don't have Window events to close it yet.
        Close();
    }

    ~SandboxApp() override {
    }
};

Core::Application* Core::CreateApplication() {
    return new SandboxApp();
}

int main() {
    PROFILE_THREAD("Main Thread");
    PROFILE_FUNCTION();
    
    // Core foundational systems
    Engine::Log::Init();
    Core::CrashHandler::Init();
    
    ENGINE_CORE_INFO("Engine Logger Initialized...");
    
    auto app = Core::CreateApplication();
    app->Run();
    delete app;
    
    return 0;
}