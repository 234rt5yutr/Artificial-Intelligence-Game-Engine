#pragma once

// AudioListenerComponent
// Represents the "ears" in the 3D audio environment
// Typically attached to the active camera or player entity

#include "Core/Math/Math.h"

namespace Core {
namespace ECS {

    // ============================================================================
    // Audio Listener Component
    // ============================================================================
    // The audio listener defines the point in 3D space from which sounds are heard.
    // Its position and orientation determine how 3D sounds are spatialized
    // (panning, distance attenuation, Doppler effect).
    //
    // Usually, there is one active listener in a scene, attached to the player
    // camera. However, the system supports multiple listeners for split-screen
    // or other advanced scenarios.

    struct AudioListenerComponent {
        // Listener identification
        uint32_t ListenerIndex = 0;             // Index for multi-listener support (0 = primary)
        bool IsActive = true;                   // Is this listener currently active?
        int32_t Priority = 0;                   // Higher priority listeners take precedence

        // Volume control
        float MasterGain = 1.0f;                // Master volume multiplier for this listener
        
        // Spatial settings
        bool AutoUpdateFromTransform = true;    // Automatically update from entity's TransformComponent
        bool AutoBindToActiveCamera = true;     // If true, auto-activate when on active camera entity
        
        // Velocity tracking for Doppler effect
        Math::Vec3 LastPosition{0.0f};          // Position from previous frame (for velocity calc)
        Math::Vec3 Velocity{0.0f};              // Current velocity (can be manually set or auto-calculated)
        bool AutoCalculateVelocity = true;      // Calculate velocity from position changes
        
        // Orientation override (if not using transform)
        Math::Quat OrientationOverride{1.0f, 0.0f, 0.0f, 0.0f};
        bool UseOrientationOverride = false;

        // World-up vector for listener orientation
        Math::Vec3 WorldUp{0.0f, 1.0f, 0.0f};

        // Audio zones/environments (for reverb, etc. - set by AudioVolumeSystem)
        float ReverbMix = 0.0f;                 // 0 = dry, 1 = full reverb
        float ReverbDecayTime = 1.0f;           // Reverb decay in seconds

        // Debug/diagnostics
        bool DebugVisualize = false;            // Show debug visualization

        // Constructors
        AudioListenerComponent() = default;

        explicit AudioListenerComponent(uint32_t listenerIndex, bool active = true)
            : ListenerIndex(listenerIndex)
            , IsActive(active)
        {}

        // Factory method for primary listener
        static AudioListenerComponent CreatePrimary() {
            AudioListenerComponent listener;
            listener.ListenerIndex = 0;
            listener.IsActive = true;
            listener.Priority = 100;
            return listener;
        }

        // Factory method for secondary listener (split-screen, etc.)
        static AudioListenerComponent CreateSecondary(uint32_t index) {
            AudioListenerComponent listener;
            listener.ListenerIndex = index;
            listener.IsActive = true;
            listener.Priority = 50;
            return listener;
        }

        // Set gain with clamping
        void SetGain(float gain) {
            MasterGain = glm::clamp(gain, 0.0f, 2.0f);
        }

        // Set reverb parameters
        void SetReverb(float mix, float decayTime) {
            ReverbMix = glm::clamp(mix, 0.0f, 1.0f);
            ReverbDecayTime = glm::max(0.0f, decayTime);
        }
    };

    // Alias for backward compatibility and clarity
    using AudioListener = AudioListenerComponent;

} // namespace ECS
} // namespace Core
