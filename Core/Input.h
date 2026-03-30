#pragma once

#include <cstdint>

namespace Core {

    class Input {
    public:
        // Core polling functionalities
        static bool IsKeyPressed(uint16_t keycode);
        
        static bool IsMouseButtonPressed(uint8_t button);
        static float GetMouseX();
        static float GetMouseY();

        // Gamepad utilities
        static bool IsGamepadConnected(int gamepadIndex = 0);
        static bool IsGamepadButtonPressed(int gamepadIndex, uint8_t button);
        static float GetGamepadAxis(int gamepadIndex, uint8_t axis);
        
        static void Init();
        static void Shutdown();

    // Prevent instantiation for static helper class
    protected:
        Input() = default;
    };

} // namespace Core
