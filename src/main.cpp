#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/CrashHandler.h"
#include "Core/Application.h"

#include <string_view>

// Subclass Application for the game client
class SandboxApp : public Core::Application {
public:
    SandboxApp() {
    }

    ~SandboxApp() override {
    }
};

Core::Application* Core::CreateApplication() {
    return new SandboxApp();
}

int main(int argc, char** argv) {
    PROFILE_THREAD("Main Thread");
    PROFILE_FUNCTION();

    Core::Application::RuntimeOptions runtimeOptions{};
    for (int argIndex = 1; argIndex < argc; ++argIndex) {
        const std::string_view arg = argv[argIndex] == nullptr ? std::string_view() : std::string_view(argv[argIndex]);
        if (arg == "--warmup-mode") {
            runtimeOptions.EnableStartupWarmupMode = true;
            continue;
        }
        if (arg == "--capture-trace") {
            runtimeOptions.CaptureStartupGPUTrace = true;
            continue;
        }
        constexpr std::string_view upscalerPrefix = "--upscaler=";
        if (arg.rfind(upscalerPrefix, 0) == 0) {
            runtimeOptions.PreferredUpscalerBackend = std::string(arg.substr(upscalerPrefix.size()));
            continue;
        }
        constexpr std::string_view tracePathPrefix = "--capture-trace-path=";
        if (arg.rfind(tracePathPrefix, 0) == 0) {
            runtimeOptions.CaptureStartupGPUTrace = true;
            runtimeOptions.StartupTraceOutputPath = std::string(arg.substr(tracePathPrefix.size()));
            continue;
        }
    }

    Core::Application::SetRuntimeOptions(runtimeOptions);
    
    // Core foundational systems
    Engine::Log::Init();
    Core::CrashHandler::Init();
    
    ENGINE_CORE_INFO("Engine Logger Initialized...");
    
    auto app = Core::CreateApplication();
    app->Run();
    delete app;
    
    return 0;
}
