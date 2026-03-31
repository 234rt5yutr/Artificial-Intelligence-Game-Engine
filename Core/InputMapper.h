#pragma once

#include "Core/Math/Math.h"
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

namespace Core {

    // Input action types
    enum class InputActionType : uint8_t {
        Button,     // Binary on/off (key press, button)
        Axis1D,     // Single axis value (-1 to 1)
        Axis2D      // Two-axis value (stick, mouse delta)
    };

    // Input source types
    enum class InputSource : uint8_t {
        Keyboard,
        MouseButton,
        MouseAxis,
        GamepadButton,
        GamepadAxis
    };

    // Key binding definition
    struct InputBinding {
        InputSource Source = InputSource::Keyboard;
        int PrimaryCode = 0;        // Key code, button index, or axis index
        int SecondaryCode = -1;     // For Axis2D: second axis; for keyboard: negative key
        float Scale = 1.0f;         // Multiplier for axis values
        int GamepadIndex = 0;       // Which gamepad (for gamepad sources)
    };

    // Input action state
    struct InputActionState {
        float Value = 0.0f;         // Current value (0-1 for buttons, -1 to 1 for axes)
        float ValueX = 0.0f;        // For Axis2D
        float ValueY = 0.0f;        // For Axis2D
        bool IsPressed = false;     // Currently held
        bool JustPressed = false;   // Pressed this frame
        bool JustReleased = false;  // Released this frame
    };

    // Predefined common actions
    namespace InputActions {
        constexpr const char* MoveForward = "MoveForward";
        constexpr const char* MoveRight = "MoveRight";
        constexpr const char* MoveUp = "MoveUp";
        constexpr const char* Jump = "Jump";
        constexpr const char* Sprint = "Sprint";
        constexpr const char* Crouch = "Crouch";
        constexpr const char* LookX = "LookX";
        constexpr const char* LookY = "LookY";
        constexpr const char* Look = "Look";          // Axis2D version
        constexpr const char* PrimaryAction = "PrimaryAction";
        constexpr const char* SecondaryAction = "SecondaryAction";
        constexpr const char* Interact = "Interact";
        constexpr const char* ToggleView = "ToggleView";
        constexpr const char* Pause = "Pause";
    }

    // Input action definition
    struct InputAction {
        std::string Name;
        InputActionType Type = InputActionType::Button;
        std::vector<InputBinding> Bindings;
        InputActionState State;
        InputActionState PreviousState;
    };

    class InputMapper {
    public:
        InputMapper();
        ~InputMapper() = default;

        // Setup default bindings for FPS-style controls
        void SetupDefaultBindings();

        // Register a new action
        void RegisterAction(const std::string& name, InputActionType type);

        // Add binding to an action
        void AddBinding(const std::string& actionName, const InputBinding& binding);

        // Remove all bindings for an action
        void ClearBindings(const std::string& actionName);

        // Update all input states (call once per frame)
        void Update(float deltaTime);

        // Query action states
        bool IsActionPressed(const std::string& actionName) const;
        bool IsActionJustPressed(const std::string& actionName) const;
        bool IsActionJustReleased(const std::string& actionName) const;
        float GetActionValue(const std::string& actionName) const;
        Math::Vec2 GetActionAxis2D(const std::string& actionName) const;

        // Get composite movement vector from standard move actions
        Math::Vec3 GetMovementVector() const;

        // Get look delta from standard look actions
        Math::Vec2 GetLookDelta() const;

        // Mouse settings
        void SetMouseSensitivity(float sensitivity) { m_MouseSensitivity = sensitivity; }
        float GetMouseSensitivity() const { return m_MouseSensitivity; }

        void SetInvertMouseY(bool invert) { m_InvertMouseY = invert; }
        bool IsMouseYInverted() const { return m_InvertMouseY; }

        // Gamepad settings
        void SetGamepadSensitivity(float sensitivity) { m_GamepadSensitivity = sensitivity; }
        float GetGamepadSensitivity() const { return m_GamepadSensitivity; }

        void SetGamepadDeadzone(float deadzone) { m_GamepadDeadzone = deadzone; }
        float GetGamepadDeadzone() const { return m_GamepadDeadzone; }

        // Raw mouse delta (accumulated since last update)
        void AccumulateMouseDelta(float dx, float dy);
        void ResetMouseDelta();

    private:
        float PollBinding(const InputBinding& binding);
        float ApplyDeadzone(float value, float deadzone);

    private:
        std::unordered_map<std::string, InputAction> m_Actions;

        // Mouse state
        float m_MouseSensitivity = 0.1f;
        bool m_InvertMouseY = false;
        float m_MouseDeltaX = 0.0f;
        float m_MouseDeltaY = 0.0f;
        float m_LastMouseX = 0.0f;
        float m_LastMouseY = 0.0f;
        bool m_FirstMouseUpdate = true;

        // Gamepad settings
        float m_GamepadSensitivity = 2.0f;
        float m_GamepadDeadzone = 0.15f;
    };

} // namespace Core
