#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/CrashHandler.h"

int main() {
    PROFILE_THREAD("Main Thread");
    PROFILE_FUNCTION();
    
    // Core foundational systems
    Engine::Log::Init();
    Core::CrashHandler::Init();
    
    ENGINE_CORE_INFO("Engine Logger Initialized");
    ENGINE_INFO("App Logger Initialized");
    
    return 0;
}