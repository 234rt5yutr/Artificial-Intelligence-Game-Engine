#pragma once

// AudioSourceComponent
// Represents a 3D sound emitter in the game world
// Plays spatialized audio with configurable attenuation, looping, and effects

#include "Core/Math/Math.h"
#include "Core/Audio/AudioSystem.h"

#include <string>
#include <vector>
#include <cstdint>

namespace Core {
namespace ECS {

    // ============================================================================
    // Audio Source Playback State
    // ============================================================================

    enum class AudioPlaybackState : uint8_t {
        Stopped = 0,
        Playing,
        Paused,
        FadingIn,
        FadingOut
    };

    // ============================================================================
    // Audio Source Trigger Mode
    // ============================================================================

    enum class AudioTriggerMode : uint8_t {
        Manual = 0,         // Explicitly triggered via Play()
        OnAwake,            // Play when component is created
        OnEnable,           // Play when component becomes active
        OnTriggerEnter,     // Play when something enters trigger volume
        OnCollision         // Play on physics collision
    };

    // ============================================================================
    // Audio Source Component
    // ============================================================================
    // Attaches a 3D sound emitter to an entity. The sound is spatialized based
    // on the entity's transform position relative to the active AudioListener.

    struct AudioSourceComponent {
        // ========================================================================
        // Audio Clip Settings
        // ========================================================================

        std::string AudioClipPath;              // Path to audio file
        std::vector<std::string> RandomClips;   // Optional: randomly select from these

        // ========================================================================
        // Playback Settings
        // ========================================================================

        bool PlayOnAwake = false;               // Start playing when component is added
        bool Loop = false;                      // Loop the sound
        float Volume = 1.0f;                    // Base volume (0-1, can exceed for boost)
        float Pitch = 1.0f;                     // Playback speed/pitch (0.5 = half speed, 2.0 = double)
        float StartTime = 0.0f;                 // Start offset in seconds
        float FadeInTime = 0.0f;                // Fade in duration (0 = instant)
        float FadeOutTime = 0.0f;               // Fade out duration (0 = instant)
        
        // Priority for voice management (higher = more important, less likely to be stolen)
        int32_t Priority = 128;                 // 0-256, 128 = default

        // ========================================================================
        // 3D Spatialization Settings
        // ========================================================================

        bool Spatial = true;                    // Enable 3D spatialization
        Audio::SpatializationConfig SpatialConfig;  // Detailed spatialization settings

        // Velocity tracking for Doppler
        bool AutoCalculateVelocity = true;      // Calculate velocity from position changes
        Math::Vec3 ManualVelocity{0.0f};        // Manual velocity if not auto-calculating

        // ========================================================================
        // Audio Mixing / Groups
        // ========================================================================

        std::string MixerGroup = "SFX";         // Mixer group for volume control
        float GroupVolumeMultiplier = 1.0f;     // Cached group volume

        // ========================================================================
        // Runtime State (managed by AudioSourceSystem)
        // ========================================================================

        Audio::SoundHandle CurrentHandle = Audio::InvalidSoundHandle;
        AudioPlaybackState State = AudioPlaybackState::Stopped;
        float CurrentVolume = 0.0f;             // Current volume (for fading)
        float FadeTimer = 0.0f;                 // Current fade progress
        float PlaybackTime = 0.0f;              // Current playback position
        Math::Vec3 LastPosition{0.0f};          // For velocity calculation
        Math::Vec3 CurrentVelocity{0.0f};       // Computed velocity
        bool IsDirty = true;                    // Needs to update audio system
        bool WasPlaying = false;                // Was playing before pause/disable

        // Hot-reload generation tracking (Stage 23)
        uint64_t ClipGeneration = 0;
        uint64_t LastReboundClipGeneration = 0;

        // ========================================================================
        // Trigger Settings
        // ========================================================================

        AudioTriggerMode TriggerMode = AudioTriggerMode::Manual;
        bool OneShot = false;                   // If true, destroy component after playback
        float RetriggerDelay = 0.0f;            // Minimum time between triggers
        float LastTriggerTime = 0.0f;           // Time of last trigger

        // ========================================================================
        // Constructors
        // ========================================================================

        AudioSourceComponent() = default;

        explicit AudioSourceComponent(const std::string& clipPath)
            : AudioClipPath(clipPath)
        {}

        AudioSourceComponent(const std::string& clipPath, bool loop, bool playOnAwake = false)
            : AudioClipPath(clipPath)
            , PlayOnAwake(playOnAwake)
            , Loop(loop)
        {}

        // ========================================================================
        // Factory Methods
        // ========================================================================

        // Create a simple one-shot sound effect
        static AudioSourceComponent CreateSFX(const std::string& clipPath, float volume = 1.0f) {
            AudioSourceComponent source(clipPath);
            source.Volume = volume;
            source.Loop = false;
            source.Spatial = true;
            source.MixerGroup = "SFX";
            return source;
        }

        // Create a looping ambient sound
        static AudioSourceComponent CreateAmbient(const std::string& clipPath, float volume = 0.5f) {
            AudioSourceComponent source(clipPath);
            source.Volume = volume;
            source.Loop = true;
            source.PlayOnAwake = true;
            source.Spatial = true;
            source.SpatialConfig.MaxDistance = 50.0f;
            source.SpatialConfig.Attenuation = Audio::AttenuationModel::Linear;
            source.MixerGroup = "Ambient";
            return source;
        }

        // Create background music (non-spatial)
        static AudioSourceComponent CreateMusic(const std::string& clipPath, float volume = 0.7f) {
            AudioSourceComponent source(clipPath);
            source.Volume = volume;
            source.Loop = true;
            source.PlayOnAwake = true;
            source.Spatial = false;
            source.FadeInTime = 2.0f;
            source.FadeOutTime = 2.0f;
            source.MixerGroup = "Music";
            return source;
        }

        // Create a directional sound (e.g., speaker, spotlight)
        static AudioSourceComponent CreateDirectional(const std::string& clipPath, 
                                                       float innerAngle, float outerAngle) {
            AudioSourceComponent source(clipPath);
            source.Spatial = true;
            source.SpatialConfig.ConeInnerAngle = innerAngle;
            source.SpatialConfig.ConeOuterAngle = outerAngle;
            source.SpatialConfig.ConeOuterGain = 0.2f;
            return source;
        }

        // ========================================================================
        // Configuration Methods
        // ========================================================================

        // Set volume with clamping
        AudioSourceComponent& SetVolume(float vol) {
            Volume = glm::clamp(vol, 0.0f, 2.0f);
            IsDirty = true;
            return *this;
        }

        // Set pitch with clamping
        AudioSourceComponent& SetPitch(float p) {
            Pitch = glm::clamp(p, 0.1f, 4.0f);
            IsDirty = true;
            return *this;
        }

        // Set distance attenuation range
        AudioSourceComponent& SetDistanceRange(float minDist, float maxDist) {
            SpatialConfig.MinDistance = glm::max(0.0f, minDist);
            SpatialConfig.MaxDistance = glm::max(minDist, maxDist);
            IsDirty = true;
            return *this;
        }

        // Set audio cone for directional sound
        AudioSourceComponent& SetCone(float innerAngleDeg, float outerAngleDeg, float outerGain = 0.0f) {
            SpatialConfig.ConeInnerAngle = glm::clamp(innerAngleDeg, 0.0f, 360.0f);
            SpatialConfig.ConeOuterAngle = glm::clamp(outerAngleDeg, innerAngleDeg, 360.0f);
            SpatialConfig.ConeOuterGain = glm::clamp(outerGain, 0.0f, 1.0f);
            IsDirty = true;
            return *this;
        }

        // Enable/disable Doppler effect
        AudioSourceComponent& SetDopplerEnabled(bool enabled) {
            SpatialConfig.DopplerEnabled = enabled;
            IsDirty = true;
            return *this;
        }

        // Set attenuation model
        AudioSourceComponent& SetAttenuation(Audio::AttenuationModel model, float rolloff = 1.0f) {
            SpatialConfig.Attenuation = model;
            SpatialConfig.RolloffFactor = glm::max(0.0f, rolloff);
            IsDirty = true;
            return *this;
        }

        // ========================================================================
        // Playback Control (use AudioSourceSystem for full control)
        // ========================================================================

        // Request to start playing (handled by system)
        void Play() {
            if (State == AudioPlaybackState::Stopped || State == AudioPlaybackState::Paused) {
                State = (FadeInTime > 0.0f) ? AudioPlaybackState::FadingIn : AudioPlaybackState::Playing;
                CurrentVolume = (FadeInTime > 0.0f) ? 0.0f : Volume;
                FadeTimer = 0.0f;
                IsDirty = true;
            }
        }

        // Request to stop playing
        void Stop() {
            if (State == AudioPlaybackState::Playing || State == AudioPlaybackState::FadingIn) {
                if (FadeOutTime > 0.0f) {
                    State = AudioPlaybackState::FadingOut;
                    FadeTimer = 0.0f;
                } else {
                    State = AudioPlaybackState::Stopped;
                    CurrentVolume = 0.0f;
                }
                IsDirty = true;
            }
        }

        // Request to pause
        void Pause() {
            if (State == AudioPlaybackState::Playing || State == AudioPlaybackState::FadingIn) {
                WasPlaying = true;
                State = AudioPlaybackState::Paused;
                IsDirty = true;
            }
        }

        // Request to resume from pause
        void Resume() {
            if (State == AudioPlaybackState::Paused) {
                State = AudioPlaybackState::Playing;
                IsDirty = true;
            }
        }

        // Check if currently playing or fading
        bool IsPlaying() const {
            return State == AudioPlaybackState::Playing || 
                   State == AudioPlaybackState::FadingIn ||
                   State == AudioPlaybackState::FadingOut;
        }

        // Check if stopped
        bool IsStopped() const {
            return State == AudioPlaybackState::Stopped;
        }

        // Check if paused
        bool IsPaused() const {
            return State == AudioPlaybackState::Paused;
        }

        // Get effective volume (including fades and group)
        float GetEffectiveVolume() const {
            return CurrentVolume * GroupVolumeMultiplier;
        }
    };

    // Alias for clarity
    using AudioSource = AudioSourceComponent;

} // namespace ECS
} // namespace Core
