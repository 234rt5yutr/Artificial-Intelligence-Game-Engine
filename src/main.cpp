#include "Core/Log.h"
#include "Core/Profile.h"

int main() {
    PROFILE_THREAD("Main Thread");
    PROFILE_FUNCTION();
    Engine::Log::Init();
    
    ENGINE_CORE_INFO("Engine Logger Initialized");
    ENGINE_INFO("App Logger Initialized");
    
    return 0;
}