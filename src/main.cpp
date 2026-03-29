#include "Core/Log.h"

int main() {
    Engine::Log::Init();
    
    ENGINE_CORE_INFO("Engine Logger Initialized");
    ENGINE_INFO("App Logger Initialized");
    
    return 0;
}