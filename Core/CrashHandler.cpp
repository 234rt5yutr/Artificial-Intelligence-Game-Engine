#include "CrashHandler.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include <csignal>
#include <cstdlib>

namespace Core {

    static void SignalHandler(int signal) {
        // Since we are crashing, tracing and profiling state may be corrupt,
        // but we'll try to log the fatal error and exit cleanly.
        const char* signalName = "Unknown Error";
        switch (signal) {
            case SIGSEGV: signalName = "SIGSEGV (Segmentation Fault)"; break;
            case SIGABRT: signalName = "SIGABRT (Abort)"; break;
            case SIGILL:  signalName = "SIGILL (Illegal Instruction)"; break;
            case SIGFPE:  signalName = "SIGFPE (Floating-Point Exception)"; break;
            case SIGINT:  signalName = "SIGINT (Interrupt)"; break;
            case SIGTERM: signalName = "SIGTERM (Termination)"; break;
        }

        ENGINE_CORE_CRITICAL("CRASH DETECTED: {0}", signalName);
        
        // Ensure logs are flushed before exiting
        if (Engine::Log::GetCoreLogger()) {
            Engine::Log::GetCoreLogger()->flush();
        }
        if (Engine::Log::GetClientLogger()) {
            Engine::Log::GetClientLogger()->flush();
        }
        
        // In a complete implementation, this might capture and print a stack trace.
        // For now, exit explicitly with the error signal.
        std::exit(signal);
    }

    void CrashHandler::Init() {
        PROFILE_FUNCTION();
        
        std::signal(SIGSEGV, SignalHandler);
        std::signal(SIGABRT, SignalHandler);
        std::signal(SIGILL,  SignalHandler);
        std::signal(SIGFPE,  SignalHandler);
        std::signal(SIGINT,  SignalHandler);
        std::signal(SIGTERM, SignalHandler);
        
        ENGINE_CORE_INFO("Crash handler initialized.");
    }

} // namespace Core
