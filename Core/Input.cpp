#include "Input.h"
#include <SDL3/SDL.h>
#include <unordered_map>
#include "Core/Log.h"

namespace Core {

    static std::unordered_map<SDL_JoystickID, SDL_Gamepad*> s_Gamepads;

    void Input::Init() {
        // Initialize the SDL gamepad subsystem explicitly
        SDL_InitSubSystem(SDL_INIT_GAMEPAD);

        // Pre-cache initially connected gamepads
        int numJoysticks = 0;
        SDL_JoystickID* joysticks = SDL_GetGamepads(&numJoysticks);
        if (joysticks) {
            for (int i = 0; i < numJoysticks; ++i) {
                SDL_Gamepad* gamepad = SDL_OpenGamepad(joysticks[i]);
                if (gamepad) {
                    s_Gamepads[joysticks[i]] = gamepad;
                    ENGINE_CORE_INFO("Gamepad attached internally: {0}", SDL_GetGamepadName(gamepad));
                }
            }
            SDL_free(joysticks);
        }
    }

    void Input::Shutdown() {
        for (auto& [id, gamepad] : s_Gamepads) {
            SDL_CloseGamepad(gamepad);
        }
        s_Gamepads.clear();
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }

    bool Input::IsKeyPressed(uint16_t keycode) {
        int numKeys = 0;
        const bool* state = SDL_GetKeyboardState(&numKeys);
        
        SDL_Scancode scancode = SDL_GetScancodeFromKey(keycode, nullptr);
        if (scancode >= 0 && scancode < numKeys) {
            return state[scancode] == true;
        }
        return false;
    }

    bool Input::IsMouseButtonPressed(uint8_t button) {
        uint32_t mouseState = SDL_GetMouseState(nullptr, nullptr);
        return (mouseState & SDL_BUTTON_MASK(button)) != 0;
    }

    float Input::GetMouseX() {
        float x;
        SDL_GetMouseState(&x, nullptr);
        return x;
    }

    float Input::GetMouseY() {
        float y;
        SDL_GetMouseState(nullptr, &y);
        return y;
    }

    bool Input::IsGamepadConnected(int gamepadIndex) {
        if (s_Gamepads.empty()) return false;
        
        auto it = s_Gamepads.begin();
        std::advance(it, (gamepadIndex < s_Gamepads.size()) ? gamepadIndex : 0);
        return it != s_Gamepads.end();
    }

    bool Input::IsGamepadButtonPressed(int gamepadIndex, uint8_t button) {
        if (!IsGamepadConnected(gamepadIndex)) return false;

        auto it = s_Gamepads.begin();
        std::advance(it, gamepadIndex);
        return SDL_GetGamepadButton(it->second, static_cast<SDL_GamepadButton>(button));
    }

    float Input::GetGamepadAxis(int gamepadIndex, uint8_t axis) {
        if (!IsGamepadConnected(gamepadIndex)) return 0.0f;

        auto it = s_Gamepads.begin();
        std::advance(it, gamepadIndex);
        
        int16_t value = SDL_GetGamepadAxis(it->second, static_cast<SDL_GamepadAxis>(axis));
        
        // Normalize 16-bit signed integer [-32768, 32767] to roughly [-1.0, 1.0]
        float result = static_cast<float>(value) / 32767.0f;
        
        // Add a small deadzone locally
        if (result > -0.1f && result < 0.1f) return 0.0f;
        
        return result;
    }

} // namespace Core
