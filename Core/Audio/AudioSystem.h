#pragma once

// Audio System - Miniaudio Backend
// Provides 3D spatial audio, streaming, and sound effect playback
// using miniaudio as the cross-platform audio backend

#include "Core/Log.h"

#include <miniaudio.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <queue>

namespace Core {
namespace Audio {

    // ============================================================================
    // Forward Declarations
    // ============================================================================

    class AudioSystem;
    class AudioSource;
    class AudioListener;
    class AudioBuffer;

    // ============================================================================
    // Audio Configuration
    // ============================================================================

    struct AudioConfig {
        uint32_t SampleRate = 48000;
        uint32_t Channels = 2;           // Stereo output
        uint32_t BufferSizeFrames = 512; // Latency vs stability tradeoff
        float MasterVolume = 1.0f;
        float DopplerFactor = 1.0f;
        float SpeedOfSound = 343.3f;     // m/s at room temperature
        uint32_t MaxVoices = 64;         // Maximum simultaneous sounds
        bool EnableSpatialAudio = true;
    };

    // ============================================================================
    // Audio Formats
    // ============================================================================

    enum class AudioFormat {
        Unknown = 0,
        U8,         // Unsigned 8-bit
        S16,        // Signed 16-bit
        S24,        // Signed 24-bit
        S32,        // Signed 32-bit
        F32         // 32-bit float
    };

    // Convert miniaudio format to our enum
    inline AudioFormat FromMiniaudioFormat(ma_format format) {
        switch (format) {
            case ma_format_u8:  return AudioFormat::U8;
            case ma_format_s16: return AudioFormat::S16;
            case ma_format_s24: return AudioFormat::S24;
            case ma_format_s32: return AudioFormat::S32;
            case ma_format_f32: return AudioFormat::F32;
            default:            return AudioFormat::Unknown;
        }
    }

    inline ma_format ToMiniaudioFormat(AudioFormat format) {
        switch (format) {
            case AudioFormat::U8:  return ma_format_u8;
            case AudioFormat::S16: return ma_format_s16;
            case AudioFormat::S24: return ma_format_s24;
            case AudioFormat::S32: return ma_format_s32;
            case AudioFormat::F32: return ma_format_f32;
            default:               return ma_format_unknown;
        }
    }

    // ============================================================================
    // Attenuation Models
    // ============================================================================

    enum class AttenuationModel {
        None = 0,           // No distance attenuation
        Inverse,            // 1/distance
        Linear,             // Linear falloff
        Exponential         // Exponential falloff
    };

    inline ma_attenuation_model ToMiniaudioAttenuation(AttenuationModel model) {
        switch (model) {
            case AttenuationModel::None:        return ma_attenuation_model_none;
            case AttenuationModel::Inverse:     return ma_attenuation_model_inverse;
            case AttenuationModel::Linear:      return ma_attenuation_model_linear;
            case AttenuationModel::Exponential: return ma_attenuation_model_exponential;
            default:                            return ma_attenuation_model_inverse;
        }
    }

    // ============================================================================
    // Sound State
    // ============================================================================

    enum class SoundState {
        Stopped = 0,
        Playing,
        Paused,
        Finished
    };

    // ============================================================================
    // Audio Buffer
    // ============================================================================
    // Holds decoded audio data in memory for quick playback

    struct AudioBufferInfo {
        std::string FilePath;
        uint32_t SampleRate = 0;
        uint32_t Channels = 0;
        uint64_t TotalFrames = 0;
        AudioFormat Format = AudioFormat::Unknown;
        float DurationSeconds = 0.0f;
        size_t SizeBytes = 0;
    };

    class AudioBuffer {
    public:
        AudioBuffer() = default;
        ~AudioBuffer() { Unload(); }

        // Non-copyable
        AudioBuffer(const AudioBuffer&) = delete;
        AudioBuffer& operator=(const AudioBuffer&) = delete;

        // Movable
        AudioBuffer(AudioBuffer&& other) noexcept;
        AudioBuffer& operator=(AudioBuffer&& other) noexcept;

        bool LoadFromFile(const std::string& filePath, ma_engine* engine);
        void Unload();

        bool IsLoaded() const { return m_Loaded; }
        const AudioBufferInfo& GetInfo() const { return m_Info; }
        ma_sound* GetInternalSound() { return &m_Sound; }

    private:
        ma_sound m_Sound{};
        AudioBufferInfo m_Info;
        bool m_Loaded = false;
    };

    // ============================================================================
    // Spatialization Settings
    // ============================================================================

    struct SpatializationConfig {
        bool Enabled = true;
        AttenuationModel Attenuation = AttenuationModel::Inverse;
        float MinDistance = 1.0f;       // Distance at which attenuation starts
        float MaxDistance = 100.0f;     // Distance at which sound is inaudible
        float RolloffFactor = 1.0f;     // How quickly sound attenuates
        float ConeInnerAngle = 360.0f;  // Degrees - full volume inside
        float ConeOuterAngle = 360.0f;  // Degrees - attenuated outside
        float ConeOuterGain = 0.0f;     // Volume outside outer cone
        bool DopplerEnabled = true;
    };

    // ============================================================================
    // Sound Handle
    // ============================================================================
    // Lightweight handle to a playing sound instance

    using SoundHandle = uint64_t;
    constexpr SoundHandle InvalidSoundHandle = 0;

    // ============================================================================
    // Sound Instance
    // ============================================================================
    // Represents a playing or paused sound

    struct SoundInstance {
        SoundHandle Handle = InvalidSoundHandle;
        ma_sound Sound{};
        std::string SourcePath;
        glm::vec3 Position{0.0f};
        glm::vec3 Velocity{0.0f};
        glm::vec3 Direction{0.0f, 0.0f, -1.0f};
        float Volume = 1.0f;
        float Pitch = 1.0f;
        bool Looping = false;
        bool Spatial = true;
        SpatializationConfig SpatialConfig;
        SoundState State = SoundState::Stopped;
        std::function<void(SoundHandle)> OnComplete;
    };

    // ============================================================================
    // Listener Configuration
    // ============================================================================

    struct ListenerConfig {
        glm::vec3 Position{0.0f};
        glm::quat Orientation{1.0f, 0.0f, 0.0f, 0.0f};  // Identity quaternion
        glm::vec3 Velocity{0.0f};
        uint32_t ListenerIndex = 0;     // For multi-listener support
    };

    // ============================================================================
    // Audio System
    // ============================================================================
    // Main audio engine singleton that manages all audio playback

    class AudioSystem {
    public:
        static AudioSystem& Get();

        // Delete copy/move
        AudioSystem(const AudioSystem&) = delete;
        AudioSystem& operator=(const AudioSystem&) = delete;

        // Lifecycle
        bool Initialize(const AudioConfig& config = AudioConfig{});
        void Shutdown();
        bool IsInitialized() const { return m_Initialized; }

        // Update (call each frame for Doppler, etc.)
        void Update(float deltaTime);

        // Listener management
        void SetListenerPosition(const glm::vec3& position, uint32_t listenerIndex = 0);
        void SetListenerOrientation(const glm::quat& orientation, uint32_t listenerIndex = 0);
        void SetListenerVelocity(const glm::vec3& velocity, uint32_t listenerIndex = 0);
        void SetListener(const ListenerConfig& config);
        ListenerConfig GetListener(uint32_t listenerIndex = 0) const;

        // Sound playback
        SoundHandle PlaySound(const std::string& filePath, bool spatial = true, bool looping = false);
        SoundHandle PlaySoundAt(const std::string& filePath, const glm::vec3& position, 
                                bool looping = false, const SpatializationConfig& config = SpatializationConfig{});
        SoundHandle PlaySoundOneShot(const std::string& filePath, float volume = 1.0f);
        SoundHandle PlaySoundOneShotAt(const std::string& filePath, const glm::vec3& position, float volume = 1.0f);

        // Sound control
        void StopSound(SoundHandle handle);
        void PauseSound(SoundHandle handle);
        void ResumeSound(SoundHandle handle);
        void StopAllSounds();
        void PauseAllSounds();
        void ResumeAllSounds();

        // Sound properties
        void SetSoundPosition(SoundHandle handle, const glm::vec3& position);
        void SetSoundVelocity(SoundHandle handle, const glm::vec3& velocity);
        void SetSoundDirection(SoundHandle handle, const glm::vec3& direction);
        void SetSoundVolume(SoundHandle handle, float volume);
        void SetSoundPitch(SoundHandle handle, float pitch);
        void SetSoundLooping(SoundHandle handle, bool looping);
        void SetSoundSpatializationConfig(SoundHandle handle, const SpatializationConfig& config);

        // Sound queries
        bool IsSoundPlaying(SoundHandle handle) const;
        bool IsSoundPaused(SoundHandle handle) const;
        SoundState GetSoundState(SoundHandle handle) const;
        float GetSoundCurrentTime(SoundHandle handle) const;
        float GetSoundDuration(SoundHandle handle) const;
        void SetSoundCurrentTime(SoundHandle handle, float timeSeconds);

        // Callbacks
        void SetSoundCompletionCallback(SoundHandle handle, std::function<void(SoundHandle)> callback);

        // Volume control
        void SetMasterVolume(float volume);
        float GetMasterVolume() const { return m_Config.MasterVolume; }

        // Audio groups/categories (for mixing)
        void SetGroupVolume(const std::string& group, float volume);
        float GetGroupVolume(const std::string& group) const;

        // Configuration
        const AudioConfig& GetConfig() const { return m_Config; }
        void SetDopplerFactor(float factor);
        void SetSpeedOfSound(float speed);

        // Statistics
        uint32_t GetActiveSoundCount() const;
        uint32_t GetMaxVoices() const { return m_Config.MaxVoices; }

        // Resource management
        bool PreloadSound(const std::string& filePath);
        void UnloadSound(const std::string& filePath);
        void UnloadAllSounds();

        // Access to internal engine (for advanced use)
        ma_engine* GetEngine() { return &m_Engine; }

    private:
        AudioSystem() = default;
        ~AudioSystem();

        // Internal helpers
        SoundInstance* GetSoundInstance(SoundHandle handle);
        const SoundInstance* GetSoundInstance(SoundHandle handle) const;
        SoundHandle GenerateHandle();
        void CleanupFinishedSounds();
        void ApplySpatializationConfig(ma_sound* sound, const SpatializationConfig& config);

        // Callback from miniaudio when sound ends
        static void OnSoundEnd(void* pUserData, ma_sound* pSound);

    private:
        AudioConfig m_Config;
        ma_engine m_Engine{};
        ma_resource_manager m_ResourceManager{};
        bool m_Initialized = false;

        // Listener state
        std::vector<ListenerConfig> m_Listeners;

        // Sound instances
        mutable std::mutex m_SoundsMutex;
        std::unordered_map<SoundHandle, std::unique_ptr<SoundInstance>> m_Sounds;
        std::atomic<uint64_t> m_NextHandle{1};

        // Preloaded buffers
        mutable std::mutex m_BuffersMutex;
        std::unordered_map<std::string, std::unique_ptr<AudioBuffer>> m_Buffers;

        // Group volumes
        mutable std::mutex m_GroupsMutex;
        std::unordered_map<std::string, float> m_GroupVolumes;

        // Sounds pending cleanup
        std::queue<SoundHandle> m_FinishedSounds;
    };

    // ============================================================================
    // Convenience Functions
    // ============================================================================

    // Quick one-liner to play a sound
    inline SoundHandle PlaySound(const std::string& path, float volume = 1.0f) {
        auto handle = AudioSystem::Get().PlaySoundOneShot(path, volume);
        return handle;
    }

    inline SoundHandle PlaySound3D(const std::string& path, const glm::vec3& position, float volume = 1.0f) {
        auto handle = AudioSystem::Get().PlaySoundOneShotAt(path, position, volume);
        if (handle != InvalidSoundHandle) {
            AudioSystem::Get().SetSoundVolume(handle, volume);
        }
        return handle;
    }

    inline void StopAllSounds() {
        AudioSystem::Get().StopAllSounds();
    }

    inline void SetMasterVolume(float volume) {
        AudioSystem::Get().SetMasterVolume(volume);
    }

} // namespace Audio
} // namespace Core
