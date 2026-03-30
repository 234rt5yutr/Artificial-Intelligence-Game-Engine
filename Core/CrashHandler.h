#pragma once

namespace Core {
    class CrashHandler {
    public:
        // Registers signal handlers to intercept crashes
        static void Init();
    };
}