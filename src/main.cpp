#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/CrashHandler.h"
#include "Core/Application.h"

#include <charconv>
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
        if (arg == "--headless") {
            runtimeOptions.Headless = true;
            continue;
        }
        if (arg == "--disable-renderer") {
            runtimeOptions.DisableRenderer = true;
            continue;
        }
        if (arg == "--disable-ui") {
            runtimeOptions.DisableUI = true;
            continue;
        }
        if (arg == "--disable-mcp") {
            runtimeOptions.EnableMCPServer = false;
            continue;
        }
        if (arg == "--capture-trace") {
            runtimeOptions.CaptureStartupGPUTrace = true;
            continue;
        }
        constexpr std::string_view mcpHostPrefix = "--mcp-host=";
        if (arg.rfind(mcpHostPrefix, 0) == 0) {
            runtimeOptions.MCPHost = std::string(arg.substr(mcpHostPrefix.size()));
            continue;
        }
        constexpr std::string_view mcpPortPrefix = "--mcp-port=";
        if (arg.rfind(mcpPortPrefix, 0) == 0) {
            const std::string_view portValue = arg.substr(mcpPortPrefix.size());
            int parsedPort = 0;
            const char* begin = portValue.data();
            const char* end = begin + portValue.size();
            auto [ptr, ec] = std::from_chars(begin, end, parsedPort);
            if (ec == std::errc() && ptr == end && parsedPort > 0 && parsedPort <= 65535) {
                runtimeOptions.MCPPort = parsedPort;
            }
            continue;
        }
        constexpr std::string_view runtimeProfilePrefix = "--runtime-profile=";
        if (arg.rfind(runtimeProfilePrefix, 0) == 0) {
            const std::string_view profileValue = arg.substr(runtimeProfilePrefix.size());
            if (profileValue == "client") {
                runtimeOptions.Profile = Core::Application::RuntimeOptions::RuntimeProfile::Client;
            } else if (profileValue == "listen") {
                runtimeOptions.Profile = Core::Application::RuntimeOptions::RuntimeProfile::ListenServer;
            } else if (profileValue == "dedicated") {
                runtimeOptions.Profile = Core::Application::RuntimeOptions::RuntimeProfile::DedicatedServer;
            }
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
