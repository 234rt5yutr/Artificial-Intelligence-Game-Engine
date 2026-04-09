#include "Core/InputMapper.h"
#include "Core/Input.h"
#include "Core/Log.h"
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_gamepad.h>
#include <cmath>
#include <algorithm>

namespace Core {

    InputMapper::InputMapper()
    {
        SetupDefaultBindings();
    }

    void InputMapper::SetupDefaultBindings()
    {
        // Movement actions
        RegisterAction(InputActions::MoveForward, InputActionType::Axis1D);
        AddBinding(InputActions::MoveForward, { InputSource::Keyboard, SDLK_W, SDLK_S, 1.0f });
        AddBinding(InputActions::MoveForward, { InputSource::GamepadAxis, SDL_GAMEPAD_AXIS_LEFTY, -1, -1.0f });

        RegisterAction(InputActions::MoveRight, InputActionType::Axis1D);
        AddBinding(InputActions::MoveRight, { InputSource::Keyboard, SDLK_D, SDLK_A, 1.0f });
        AddBinding(InputActions::MoveRight, { InputSource::GamepadAxis, SDL_GAMEPAD_AXIS_LEFTX, -1, 1.0f });

        RegisterAction(InputActions::MoveUp, InputActionType::Axis1D);
        AddBinding(InputActions::MoveUp, { InputSource::Keyboard, SDLK_SPACE, SDLK_LCTRL, 1.0f });

        // Jump action
        RegisterAction(InputActions::Jump, InputActionType::Button);
        AddBinding(InputActions::Jump, { InputSource::Keyboard, SDLK_SPACE, -1, 1.0f });
        AddBinding(InputActions::Jump, { InputSource::GamepadButton, SDL_GAMEPAD_BUTTON_SOUTH, -1, 1.0f });

        // Sprint action
        RegisterAction(InputActions::Sprint, InputActionType::Button);
        AddBinding(InputActions::Sprint, { InputSource::Keyboard, SDLK_LSHIFT, -1, 1.0f });
        AddBinding(InputActions::Sprint, { InputSource::GamepadButton, SDL_GAMEPAD_BUTTON_LEFT_STICK, -1, 1.0f });

        // Crouch action
        RegisterAction(InputActions::Crouch, InputActionType::Button);
        AddBinding(InputActions::Crouch, { InputSource::Keyboard, SDLK_LCTRL, -1, 1.0f });
        AddBinding(InputActions::Crouch, { InputSource::GamepadButton, SDL_GAMEPAD_BUTTON_RIGHT_STICK, -1, 1.0f });

        // Look actions (1D axes for keyboard/gamepad)
        RegisterAction(InputActions::LookX, InputActionType::Axis1D);
        AddBinding(InputActions::LookX, { InputSource::MouseAxis, 0, -1, 1.0f });  // Mouse X
        AddBinding(InputActions::LookX, { InputSource::GamepadAxis, SDL_GAMEPAD_AXIS_RIGHTX, -1, 1.0f });

        RegisterAction(InputActions::LookY, InputActionType::Axis1D);
        AddBinding(InputActions::LookY, { InputSource::MouseAxis, 1, -1, 1.0f });  // Mouse Y
        AddBinding(InputActions::LookY, { InputSource::GamepadAxis, SDL_GAMEPAD_AXIS_RIGHTY, -1, 1.0f });

        // Combined look (2D axis)
        RegisterAction(InputActions::Look, InputActionType::Axis2D);
        AddBinding(InputActions::Look, { InputSource::MouseAxis, 0, 1, 1.0f });
        AddBinding(InputActions::Look, { InputSource::GamepadAxis, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, 1.0f });

        // Primary action (fire/use)
        RegisterAction(InputActions::PrimaryAction, InputActionType::Button);
        AddBinding(InputActions::PrimaryAction, { InputSource::MouseButton, 1, -1, 1.0f });  // Left click
        AddBinding(InputActions::PrimaryAction, { InputSource::GamepadAxis, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -1, 1.0f });

        // Secondary action (aim/alt fire)
        RegisterAction(InputActions::SecondaryAction, InputActionType::Button);
        AddBinding(InputActions::SecondaryAction, { InputSource::MouseButton, 3, -1, 1.0f });  // Right click
        AddBinding(InputActions::SecondaryAction, { InputSource::GamepadAxis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -1, 1.0f });

        // Interact
        RegisterAction(InputActions::Interact, InputActionType::Button);
        AddBinding(InputActions::Interact, { InputSource::Keyboard, SDLK_E, -1, 1.0f });
        AddBinding(InputActions::Interact, { InputSource::GamepadButton, SDL_GAMEPAD_BUTTON_WEST, -1, 1.0f });

        // Toggle view (FP/TP)
        RegisterAction(InputActions::ToggleView, InputActionType::Button);
        AddBinding(InputActions::ToggleView, { InputSource::Keyboard, SDLK_V, -1, 1.0f });
        AddBinding(InputActions::ToggleView, { InputSource::GamepadButton, SDL_GAMEPAD_BUTTON_BACK, -1, 1.0f });

        // Pause
        RegisterAction(InputActions::Pause, InputActionType::Button);
        AddBinding(InputActions::Pause, { InputSource::Keyboard, SDLK_ESCAPE, -1, 1.0f });
        AddBinding(InputActions::Pause, { InputSource::GamepadButton, SDL_GAMEPAD_BUTTON_START, -1, 1.0f });

        ENGINE_CORE_INFO("InputMapper: Default bindings configured");
    }

    void InputMapper::RegisterAction(const std::string& name, InputActionType type)
    {
        InputAction action;
        action.Name = name;
        action.Type = type;
        m_Actions[name] = action;
    }

    void InputMapper::AddBinding(const std::string& actionName, const InputBinding& binding)
    {
        auto it = m_Actions.find(actionName);
        if (it != m_Actions.end()) {
            it->second.Bindings.push_back(binding);
        }
        else {
            ENGINE_CORE_WARN("InputMapper: Action '{}' not found", actionName);
        }
    }

    void InputMapper::ClearBindings(const std::string& actionName)
    {
        auto it = m_Actions.find(actionName);
        if (it != m_Actions.end()) {
            it->second.Bindings.clear();
        }
    }

    void InputMapper::Update(float deltaTime)
    {
        // Calculate mouse delta
        float currentMouseX = Input::GetMouseX();
        float currentMouseY = Input::GetMouseY();

        if (!m_FirstMouseUpdate) {
            m_MouseDeltaX = (currentMouseX - m_LastMouseX) * m_MouseSensitivity;
            m_MouseDeltaY = (currentMouseY - m_LastMouseY) * m_MouseSensitivity;
            if (m_InvertMouseY) {
                m_MouseDeltaY = -m_MouseDeltaY;
            }
        }
        else {
            m_FirstMouseUpdate = false;
            m_MouseDeltaX = 0.0f;
            m_MouseDeltaY = 0.0f;
        }

        m_LastMouseX = currentMouseX;
        m_LastMouseY = currentMouseY;

        // Update all actions
        for (auto& [name, action] : m_Actions) {
            action.PreviousState = action.State;

            float totalValue = 0.0f;
            float totalValueX = 0.0f;
            float totalValueY = 0.0f;

            for (const auto& binding : action.Bindings) {
                float value = PollBinding(binding);

                if (action.Type == InputActionType::Axis2D && 
                    (binding.Source == InputSource::MouseAxis || binding.Source == InputSource::GamepadAxis)) {
                    // For 2D axes, poll both components
                    if (binding.Source == InputSource::MouseAxis) {
                        totalValueX += m_MouseDeltaX;
                        totalValueY += m_MouseDeltaY;
                    }
                    else if (binding.Source == InputSource::GamepadAxis) {
                        float axisX = Input::GetGamepadAxis(binding.GamepadIndex, static_cast<uint8_t>(binding.PrimaryCode));
                        float axisY = Input::GetGamepadAxis(binding.GamepadIndex, static_cast<uint8_t>(binding.SecondaryCode));
                        axisX = ApplyDeadzone(axisX, m_GamepadDeadzone);
                        axisY = ApplyDeadzone(axisY, m_GamepadDeadzone);
                        totalValueX += axisX * m_GamepadSensitivity * deltaTime * 60.0f;
                        totalValueY += axisY * m_GamepadSensitivity * deltaTime * 60.0f;
                    }
                }
                else {
                    totalValue += value;
                }
            }

            // Clamp values
            action.State.Value = std::clamp(totalValue, -1.0f, 1.0f);
            action.State.ValueX = totalValueX;
            action.State.ValueY = totalValueY;
            action.State.IsPressed = std::abs(action.State.Value) > 0.5f;
            action.State.JustPressed = action.State.IsPressed && !action.PreviousState.IsPressed;
            action.State.JustReleased = !action.State.IsPressed && action.PreviousState.IsPressed;
        }

        // Reset mouse delta after processing
        m_MouseDeltaX = 0.0f;
        m_MouseDeltaY = 0.0f;
    }

    // Input validation limits based on SDL3 constants
    namespace InputLimits {
        constexpr int MAX_GAMEPAD_AXIS = SDL_GAMEPAD_AXIS_COUNT;
        constexpr int MAX_GAMEPAD_BUTTON = SDL_GAMEPAD_BUTTON_COUNT;
        constexpr int MAX_MOUSE_BUTTON = 5;
        constexpr int MAX_MOUSE_AXIS = 2;
    }

    float InputMapper::PollBinding(const InputBinding& binding)
    {
        float value = 0.0f;

        switch (binding.Source) {
            case InputSource::Keyboard: {
                if (Input::IsKeyPressed(static_cast<uint16_t>(binding.PrimaryCode))) {
                    value = 1.0f;
                }
                if (binding.SecondaryCode >= 0 && Input::IsKeyPressed(static_cast<uint16_t>(binding.SecondaryCode))) {
                    value -= 1.0f;
                }
                break;
            }

            case InputSource::MouseButton: {
                // Validate mouse button index
                if (binding.PrimaryCode < 0 || binding.PrimaryCode >= InputLimits::MAX_MOUSE_BUTTON) {
                    ENGINE_CORE_WARN("Invalid mouse button code: {}", binding.PrimaryCode);
                    break;
                }
                if (Input::IsMouseButtonPressed(static_cast<uint8_t>(binding.PrimaryCode))) {
                    value = 1.0f;
                }
                break;
            }

            case InputSource::MouseAxis: {
                // Validate mouse axis index
                if (binding.PrimaryCode < 0 || binding.PrimaryCode >= InputLimits::MAX_MOUSE_AXIS) {
                    ENGINE_CORE_WARN("Invalid mouse axis code: {}", binding.PrimaryCode);
                    break;
                }
                if (binding.PrimaryCode == 0) {
                    value = m_MouseDeltaX;
                }
                else if (binding.PrimaryCode == 1) {
                    value = m_MouseDeltaY;
                }
                break;
            }

            case InputSource::GamepadButton: {
                // Validate gamepad button index
                if (binding.PrimaryCode < 0 || binding.PrimaryCode >= InputLimits::MAX_GAMEPAD_BUTTON) {
                    ENGINE_CORE_WARN("Invalid gamepad button code: {}", binding.PrimaryCode);
                    break;
                }
                if (Input::IsGamepadButtonPressed(binding.GamepadIndex, static_cast<uint8_t>(binding.PrimaryCode))) {
                    value = 1.0f;
                }
                break;
            }

            case InputSource::GamepadAxis: {
                // Validate gamepad axis index
                if (binding.PrimaryCode < 0 || binding.PrimaryCode >= InputLimits::MAX_GAMEPAD_AXIS) {
                    ENGINE_CORE_WARN("Invalid gamepad axis code: {}", binding.PrimaryCode);
                    break;
                }
                float axisValue = Input::GetGamepadAxis(binding.GamepadIndex, static_cast<uint8_t>(binding.PrimaryCode));
                value = ApplyDeadzone(axisValue, m_GamepadDeadzone);
                break;
            }
        }

        return value * binding.Scale;
    }

    float InputMapper::ApplyDeadzone(float value, float deadzone)
    {
        if (std::abs(value) < deadzone) {
            return 0.0f;
        }
        // Remap value outside deadzone to 0-1 range
        float sign = value > 0.0f ? 1.0f : -1.0f;
        return sign * (std::abs(value) - deadzone) / (1.0f - deadzone);
    }

    bool InputMapper::IsActionPressed(const std::string& actionName) const
    {
        auto it = m_Actions.find(actionName);
        if (it != m_Actions.end()) {
            return it->second.State.IsPressed;
        }
        return false;
    }

    bool InputMapper::IsActionJustPressed(const std::string& actionName) const
    {
        auto it = m_Actions.find(actionName);
        if (it != m_Actions.end()) {
            return it->second.State.JustPressed;
        }
        return false;
    }

    bool InputMapper::IsActionJustReleased(const std::string& actionName) const
    {
        auto it = m_Actions.find(actionName);
        if (it != m_Actions.end()) {
            return it->second.State.JustReleased;
        }
        return false;
    }

    float InputMapper::GetActionValue(const std::string& actionName) const
    {
        auto it = m_Actions.find(actionName);
        if (it != m_Actions.end()) {
            return it->second.State.Value;
        }
        return 0.0f;
    }

    Math::Vec2 InputMapper::GetActionAxis2D(const std::string& actionName) const
    {
        auto it = m_Actions.find(actionName);
        if (it != m_Actions.end()) {
            return Math::Vec2(it->second.State.ValueX, it->second.State.ValueY);
        }
        return Math::Vec2(0.0f);
    }

    Math::Vec3 InputMapper::GetMovementVector() const
    {
        float forward = GetActionValue(InputActions::MoveForward);
        float right = GetActionValue(InputActions::MoveRight);
        float up = GetActionValue(InputActions::MoveUp);

        Math::Vec3 movement(right, up, -forward);  // -Z is forward in our coordinate system

        // Normalize if magnitude > 1 (diagonal movement)
        float length = glm::length(movement);
        if (length > 1.0f) {
            movement /= length;
        }

        return movement;
    }

    Math::Vec2 InputMapper::GetLookDelta() const
    {
        // Try Axis2D first
        Math::Vec2 look = GetActionAxis2D(InputActions::Look);
        if (glm::length(look) > 0.001f) {
            return look;
        }

        // Fall back to separate axes
        float lookX = GetActionValue(InputActions::LookX);
        float lookY = GetActionValue(InputActions::LookY);
        return Math::Vec2(lookX, lookY);
    }

    void InputMapper::AccumulateMouseDelta(float dx, float dy)
    {
        m_MouseDeltaX += dx * m_MouseSensitivity;
        m_MouseDeltaY += dy * m_MouseSensitivity;
        if (m_InvertMouseY) {
            m_MouseDeltaY = -m_MouseDeltaY;
        }
    }

    void InputMapper::ResetMouseDelta()
    {
        m_MouseDeltaX = 0.0f;
        m_MouseDeltaY = 0.0f;
    }

} // namespace Core
