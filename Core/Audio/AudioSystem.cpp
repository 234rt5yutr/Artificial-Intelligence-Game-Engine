// Audio System Implementation
// Uses miniaudio for cross-platform audio playback with 3D spatialization

// Define miniaudio implementation in this translation unit
#define MINIAUDIO_IMPLEMENTATION
#include "AudioSystem.h"
#include "Core/Security/PathValidator.h"

#include <algorithm>
#include <cstring>

namespace Core {
namespace Audio {

    // ============================================================================
    // AudioBuffer Implementation
    // ============================================================================

    AudioBuffer::AudioBuffer(AudioBuffer&& other) noexcept
        : m_Sound(other.m_Sound)
        , m_Info(std::move(other.m_Info))
        , m_Loaded(other.m_Loaded)
    {
        other.m_Loaded = false;
        std::memset(&other.m_Sound, 0, sizeof(ma_sound));
    }

    AudioBuffer& AudioBuffer::operator=(AudioBuffer&& other) noexcept {
        if (this != &other) {
            Unload();
            m_Sound = other.m_Sound;
            m_Info = std::move(other.m_Info);
            m_Loaded = other.m_Loaded;
            other.m_Loaded = false;
            std::memset(&other.m_Sound, 0, sizeof(ma_sound));
        }
        return *this;
    }

    bool AudioBuffer::LoadFromFile(const std::string& filePath, ma_engine* engine) {
        if (m_Loaded) {
            Unload();
        }

        // Validate path against traversal attacks
        std::filesystem::path path(filePath);
        auto validatedPath = Security::PathValidator::ValidateAssetPath(path);
        if (!validatedPath) {
            ENGINE_CORE_ERROR("AudioBuffer: Path validation failed for '{}'", 
                             Security::PathValidator::SanitizeForLogging(path));
            return false;
        }
        
        // Check file size limit
        if (!Security::PathValidator::ValidateFileSize(*validatedPath, Security::MAX_AUDIO_SIZE)) {
            ENGINE_CORE_ERROR("AudioBuffer: File size exceeds limit for '{}'", 
                             Security::PathValidator::SanitizeForLogging(path));
            return false;
        }

        ma_result result = ma_sound_init_from_file(engine, validatedPath->string().c_str(), 
            MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION,
            nullptr, nullptr, &m_Sound);

        if (result != MA_SUCCESS) {
            ENGINE_CORE_ERROR("AudioBuffer: Failed to load '{}', error: {}", 
                             Security::PathValidator::SanitizeForLogging(path), static_cast<int>(result));
            return false;
        }

        // Get sound info
        m_Info.FilePath = validatedPath->string();
        
        ma_sound_get_data_format(&m_Sound, nullptr, &m_Info.Channels, &m_Info.SampleRate, nullptr, 0);
        
        ma_uint64 lengthInFrames = 0;
        if (ma_sound_get_length_in_pcm_frames(&m_Sound, &lengthInFrames) == MA_SUCCESS) {
            m_Info.TotalFrames = lengthInFrames;
            m_Info.DurationSeconds = static_cast<float>(lengthInFrames) / static_cast<float>(m_Info.SampleRate);
        }

        m_Loaded = true;
        ENGINE_CORE_INFO("AudioBuffer: Loaded '{}' ({:.2f}s, {}ch, {}Hz)", 
                        path.filename().string(), m_Info.DurationSeconds, m_Info.Channels, m_Info.SampleRate);
        
        return true;
    }

    void AudioBuffer::Unload() {
        if (m_Loaded) {
            ma_sound_uninit(&m_Sound);
            m_Loaded = false;
            m_Info = AudioBufferInfo{};
        }
    }

    // ============================================================================
    // AudioSystem Singleton
    // ============================================================================

    AudioSystem& AudioSystem::Get() {
        static AudioSystem instance;
        return instance;
    }

    AudioSystem::~AudioSystem() {
        if (m_Initialized) {
            Shutdown();
        }
    }

    // ============================================================================
    // Lifecycle
    // ============================================================================

    bool AudioSystem::Initialize(const AudioConfig& config) {
        if (m_Initialized) {
            ENGINE_CORE_WARN("AudioSystem: Already initialized");
            return true;
        }

        m_Config = config;

        // Configure engine
        ma_engine_config engineConfig = ma_engine_config_init();
        engineConfig.channels = config.Channels;
        engineConfig.sampleRate = config.SampleRate;
        engineConfig.listenerCount = 1;  // Start with one listener

        // Initialize engine
        ma_result result = ma_engine_init(&engineConfig, &m_Engine);
        if (result != MA_SUCCESS) {
            ENGINE_CORE_ERROR("AudioSystem: Failed to initialize miniaudio engine, error: {}", 
                            static_cast<int>(result));
            return false;
        }

        // Set initial master volume
        ma_engine_set_volume(&m_Engine, config.MasterVolume);

        // Initialize listener
        m_Listeners.push_back(ListenerConfig{});

        m_Initialized = true;
        ENGINE_CORE_INFO("AudioSystem: Initialized ({}Hz, {} channels, {} max voices)",
                        config.SampleRate, config.Channels, config.MaxVoices);

        return true;
    }

    void AudioSystem::Shutdown() {
        if (!m_Initialized) return;

        ENGINE_CORE_INFO("AudioSystem: Shutting down...");

        // Stop all sounds
        StopAllSounds();

        // Clear all sounds
        {
            std::lock_guard lock(m_SoundsMutex);
            for (auto& [handle, instance] : m_Sounds) {
                ma_sound_uninit(&instance->Sound);
            }
            m_Sounds.clear();
        }

        // Clear buffers
        {
            std::lock_guard lock(m_BuffersMutex);
            m_Buffers.clear();
        }

        // Uninit engine
        ma_engine_uninit(&m_Engine);

        m_Initialized = false;
        ENGINE_CORE_INFO("AudioSystem: Shutdown complete");
    }

    // ============================================================================
    // Update
    // ============================================================================

    void AudioSystem::Update(float deltaTime) {
        if (!m_Initialized) return;

        (void)deltaTime;  // May be used for Doppler calculations in future

        // Cleanup finished sounds
        CleanupFinishedSounds();
    }

    // ============================================================================
    // Listener Management
    // ============================================================================

    void AudioSystem::SetListenerPosition(const glm::vec3& position, uint32_t listenerIndex) {
        if (!m_Initialized || listenerIndex >= m_Listeners.size()) return;

        m_Listeners[listenerIndex].Position = position;
        ma_engine_listener_set_position(&m_Engine, listenerIndex, position.x, position.y, position.z);
    }

    void AudioSystem::SetListenerOrientation(const glm::quat& orientation, uint32_t listenerIndex) {
        if (!m_Initialized || listenerIndex >= m_Listeners.size()) return;

        m_Listeners[listenerIndex].Orientation = orientation;

        // Convert quaternion to forward and up vectors
        glm::vec3 forward = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 up = orientation * glm::vec3(0.0f, 1.0f, 0.0f);

        ma_engine_listener_set_direction(&m_Engine, listenerIndex, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(&m_Engine, listenerIndex, up.x, up.y, up.z);
    }

    void AudioSystem::SetListenerVelocity(const glm::vec3& velocity, uint32_t listenerIndex) {
        if (!m_Initialized || listenerIndex >= m_Listeners.size()) return;

        m_Listeners[listenerIndex].Velocity = velocity;
        ma_engine_listener_set_velocity(&m_Engine, listenerIndex, velocity.x, velocity.y, velocity.z);
    }

    void AudioSystem::SetListener(const ListenerConfig& config) {
        SetListenerPosition(config.Position, config.ListenerIndex);
        SetListenerOrientation(config.Orientation, config.ListenerIndex);
        SetListenerVelocity(config.Velocity, config.ListenerIndex);
    }

    ListenerConfig AudioSystem::GetListener(uint32_t listenerIndex) const {
        if (listenerIndex < m_Listeners.size()) {
            return m_Listeners[listenerIndex];
        }
        return ListenerConfig{};
    }

    // ============================================================================
    // Sound Playback
    // ============================================================================

    SoundHandle AudioSystem::PlaySound(const std::string& filePath, bool spatial, bool looping) {
        if (!m_Initialized) return InvalidSoundHandle;

        auto instance = std::make_unique<SoundInstance>();
        instance->Handle = GenerateHandle();
        instance->SourcePath = filePath;
        instance->Looping = looping;
        instance->Spatial = spatial;

        // Initialize sound from file
        ma_uint32 flags = 0;
        if (!spatial) {
            flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
        }

        ma_result result = ma_sound_init_from_file(&m_Engine, filePath.c_str(), flags,
                                                    nullptr, nullptr, &instance->Sound);
        if (result != MA_SUCCESS) {
            ENGINE_CORE_ERROR("AudioSystem: Failed to play '{}', error: {}", filePath, static_cast<int>(result));
            return InvalidSoundHandle;
        }

        // Configure sound
        ma_sound_set_looping(&instance->Sound, looping ? MA_TRUE : MA_FALSE);
        
        if (spatial) {
            ApplySpatializationConfig(&instance->Sound, instance->SpatialConfig);
        }

        // Start playback
        result = ma_sound_start(&instance->Sound);
        if (result != MA_SUCCESS) {
            ma_sound_uninit(&instance->Sound);
            ENGINE_CORE_ERROR("AudioSystem: Failed to start '{}', error: {}", filePath, static_cast<int>(result));
            return InvalidSoundHandle;
        }

        instance->State = SoundState::Playing;
        SoundHandle handle = instance->Handle;

        // Store instance
        {
            std::lock_guard lock(m_SoundsMutex);
            m_Sounds[handle] = std::move(instance);
        }

        ENGINE_CORE_TRACE("AudioSystem: Playing '{}' (handle: {})", filePath, handle);
        return handle;
    }

    SoundHandle AudioSystem::PlaySoundAt(const std::string& filePath, const glm::vec3& position,
                                         bool looping, const SpatializationConfig& config) {
        SoundHandle handle = PlaySound(filePath, true, looping);
        if (handle != InvalidSoundHandle) {
            SetSoundPosition(handle, position);
            SetSoundSpatializationConfig(handle, config);
        }
        return handle;
    }

    SoundHandle AudioSystem::PlaySoundOneShot(const std::string& filePath, float volume) {
        if (!m_Initialized) return InvalidSoundHandle;

        // For one-shot sounds, we use a simpler approach
        SoundHandle handle = PlaySound(filePath, false, false);
        if (handle != InvalidSoundHandle) {
            SetSoundVolume(handle, volume);
        }
        return handle;
    }

    SoundHandle AudioSystem::PlaySoundOneShotAt(const std::string& filePath, const glm::vec3& position, float volume) {
        SoundHandle handle = PlaySound(filePath, true, false);
        if (handle != InvalidSoundHandle) {
            SetSoundPosition(handle, position);
            SetSoundVolume(handle, volume);
        }
        return handle;
    }

    // ============================================================================
    // Sound Control
    // ============================================================================

    void AudioSystem::StopSound(SoundHandle handle) {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            ma_sound_stop(&it->second->Sound);
            it->second->State = SoundState::Stopped;
        }
    }

    void AudioSystem::PauseSound(SoundHandle handle) {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end() && it->second->State == SoundState::Playing) {
            ma_sound_stop(&it->second->Sound);
            it->second->State = SoundState::Paused;
        }
    }

    void AudioSystem::ResumeSound(SoundHandle handle) {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end() && it->second->State == SoundState::Paused) {
            ma_sound_start(&it->second->Sound);
            it->second->State = SoundState::Playing;
        }
    }

    void AudioSystem::StopAllSounds() {
        std::lock_guard lock(m_SoundsMutex);
        for (auto& [handle, instance] : m_Sounds) {
            ma_sound_stop(&instance->Sound);
            instance->State = SoundState::Stopped;
        }
    }

    void AudioSystem::PauseAllSounds() {
        std::lock_guard lock(m_SoundsMutex);
        for (auto& [handle, instance] : m_Sounds) {
            if (instance->State == SoundState::Playing) {
                ma_sound_stop(&instance->Sound);
                instance->State = SoundState::Paused;
            }
        }
    }

    void AudioSystem::ResumeAllSounds() {
        std::lock_guard lock(m_SoundsMutex);
        for (auto& [handle, instance] : m_Sounds) {
            if (instance->State == SoundState::Paused) {
                ma_sound_start(&instance->Sound);
                instance->State = SoundState::Playing;
            }
        }
    }

    // ============================================================================
    // Sound Properties
    // ============================================================================

    void AudioSystem::SetSoundPosition(SoundHandle handle, const glm::vec3& position) {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            it->second->Position = position;
            ma_sound_set_position(&it->second->Sound, position.x, position.y, position.z);
        }
    }

    void AudioSystem::SetSoundVelocity(SoundHandle handle, const glm::vec3& velocity) {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            it->second->Velocity = velocity;
            ma_sound_set_velocity(&it->second->Sound, velocity.x, velocity.y, velocity.z);
        }
    }

    void AudioSystem::SetSoundDirection(SoundHandle handle, const glm::vec3& direction) {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            it->second->Direction = direction;
            ma_sound_set_direction(&it->second->Sound, direction.x, direction.y, direction.z);
        }
    }

    void AudioSystem::SetSoundVolume(SoundHandle handle, float volume) {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            it->second->Volume = std::clamp(volume, 0.0f, 2.0f);
            ma_sound_set_volume(&it->second->Sound, it->second->Volume);
        }
    }

    void AudioSystem::SetSoundPitch(SoundHandle handle, float pitch) {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            it->second->Pitch = std::clamp(pitch, 0.1f, 4.0f);
            ma_sound_set_pitch(&it->second->Sound, it->second->Pitch);
        }
    }

    void AudioSystem::SetSoundLooping(SoundHandle handle, bool looping) {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            it->second->Looping = looping;
            ma_sound_set_looping(&it->second->Sound, looping ? MA_TRUE : MA_FALSE);
        }
    }

    void AudioSystem::SetSoundSpatializationConfig(SoundHandle handle, const SpatializationConfig& config) {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            it->second->SpatialConfig = config;
            ApplySpatializationConfig(&it->second->Sound, config);
        }
    }

    // ============================================================================
    // Sound Queries
    // ============================================================================

    bool AudioSystem::IsSoundPlaying(SoundHandle handle) const {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            return ma_sound_is_playing(&it->second->Sound) == MA_TRUE;
        }
        return false;
    }

    bool AudioSystem::IsSoundPaused(SoundHandle handle) const {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            return it->second->State == SoundState::Paused;
        }
        return false;
    }

    SoundState AudioSystem::GetSoundState(SoundHandle handle) const {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            // Check if sound finished naturally
            if (it->second->State == SoundState::Playing && 
                ma_sound_is_playing(&it->second->Sound) == MA_FALSE) {
                return SoundState::Finished;
            }
            return it->second->State;
        }
        return SoundState::Stopped;
    }

    float AudioSystem::GetSoundCurrentTime(SoundHandle handle) const {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            float cursor = 0.0f;
            ma_sound_get_cursor_in_seconds(&it->second->Sound, &cursor);
            return cursor;
        }
        return 0.0f;
    }

    float AudioSystem::GetSoundDuration(SoundHandle handle) const {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            float length = 0.0f;
            ma_sound_get_length_in_seconds(&it->second->Sound, &length);
            return length;
        }
        return 0.0f;
    }

    void AudioSystem::SetSoundCurrentTime(SoundHandle handle, float timeSeconds) {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            ma_uint64 frames = static_cast<ma_uint64>(timeSeconds * static_cast<float>(m_Config.SampleRate));
            ma_sound_seek_to_pcm_frame(&it->second->Sound, frames);
        }
    }

    // ============================================================================
    // Callbacks
    // ============================================================================

    void AudioSystem::SetSoundCompletionCallback(SoundHandle handle, std::function<void(SoundHandle)> callback) {
        std::lock_guard lock(m_SoundsMutex);
        auto it = m_Sounds.find(handle);
        if (it != m_Sounds.end()) {
            it->second->OnComplete = std::move(callback);
        }
    }

    // ============================================================================
    // Volume Control
    // ============================================================================

    void AudioSystem::SetMasterVolume(float volume) {
        m_Config.MasterVolume = std::clamp(volume, 0.0f, 2.0f);
        if (m_Initialized) {
            ma_engine_set_volume(&m_Engine, m_Config.MasterVolume);
        }
    }

    void AudioSystem::SetGroupVolume(const std::string& group, float volume) {
        std::lock_guard lock(m_GroupsMutex);
        m_GroupVolumes[group] = std::clamp(volume, 0.0f, 2.0f);
    }

    float AudioSystem::GetGroupVolume(const std::string& group) const {
        std::lock_guard lock(m_GroupsMutex);
        auto it = m_GroupVolumes.find(group);
        return (it != m_GroupVolumes.end()) ? it->second : 1.0f;
    }

    // ============================================================================
    // Configuration
    // ============================================================================

    void AudioSystem::SetDopplerFactor(float factor) {
        m_Config.DopplerFactor = std::max(0.0f, factor);
        // Note: miniaudio handles Doppler internally based on velocities
    }

    void AudioSystem::SetSpeedOfSound(float speed) {
        m_Config.SpeedOfSound = std::max(1.0f, speed);
    }

    // ============================================================================
    // Statistics
    // ============================================================================

    uint32_t AudioSystem::GetActiveSoundCount() const {
        std::lock_guard lock(m_SoundsMutex);
        uint32_t count = 0;
        for (const auto& [handle, instance] : m_Sounds) {
            if (instance->State == SoundState::Playing || instance->State == SoundState::Paused) {
                ++count;
            }
        }
        return count;
    }

    // ============================================================================
    // Resource Management
    // ============================================================================

    bool AudioSystem::PreloadSound(const std::string& filePath) {
        if (!m_Initialized) return false;

        std::lock_guard lock(m_BuffersMutex);
        
        // Check if already loaded
        if (m_Buffers.find(filePath) != m_Buffers.end()) {
            return true;
        }

        auto buffer = std::make_unique<AudioBuffer>();
        if (!buffer->LoadFromFile(filePath, &m_Engine)) {
            return false;
        }

        m_Buffers[filePath] = std::move(buffer);
        return true;
    }

    void AudioSystem::UnloadSound(const std::string& filePath) {
        std::lock_guard lock(m_BuffersMutex);
        m_Buffers.erase(filePath);
    }

    void AudioSystem::UnloadAllSounds() {
        // First stop all playing sounds
        StopAllSounds();

        // Clear sound instances
        {
            std::lock_guard lock(m_SoundsMutex);
            for (auto& [handle, instance] : m_Sounds) {
                ma_sound_uninit(&instance->Sound);
            }
            m_Sounds.clear();
        }

        // Clear buffers
        {
            std::lock_guard lock(m_BuffersMutex);
            m_Buffers.clear();
        }
    }

    // ============================================================================
    // Internal Helpers
    // ============================================================================

    SoundInstance* AudioSystem::GetSoundInstance(SoundHandle handle) {
        auto it = m_Sounds.find(handle);
        return (it != m_Sounds.end()) ? it->second.get() : nullptr;
    }

    const SoundInstance* AudioSystem::GetSoundInstance(SoundHandle handle) const {
        auto it = m_Sounds.find(handle);
        return (it != m_Sounds.end()) ? it->second.get() : nullptr;
    }

    SoundHandle AudioSystem::GenerateHandle() {
        return m_NextHandle.fetch_add(1, std::memory_order_relaxed);
    }

    void AudioSystem::CleanupFinishedSounds() {
        std::lock_guard lock(m_SoundsMutex);

        std::vector<SoundHandle> toRemove;
        
        for (auto& [handle, instance] : m_Sounds) {
            // Check if sound finished playing
            if (instance->State == SoundState::Playing && 
                ma_sound_is_playing(&instance->Sound) == MA_FALSE &&
                !instance->Looping) {
                
                instance->State = SoundState::Finished;
                
                // Call completion callback
                if (instance->OnComplete) {
                    instance->OnComplete(handle);
                }
                
                // Mark for removal (non-looping finished sounds)
                toRemove.push_back(handle);
            }
        }

        // Remove finished sounds
        for (SoundHandle handle : toRemove) {
            auto it = m_Sounds.find(handle);
            if (it != m_Sounds.end()) {
                ma_sound_uninit(&it->second->Sound);
                m_Sounds.erase(it);
            }
        }
    }

    void AudioSystem::ApplySpatializationConfig(ma_sound* sound, const SpatializationConfig& config) {
        if (!sound) return;

        ma_sound_set_spatialization_enabled(sound, config.Enabled ? MA_TRUE : MA_FALSE);
        
        if (config.Enabled) {
            ma_sound_set_attenuation_model(sound, ToMiniaudioAttenuation(config.Attenuation));
            ma_sound_set_min_distance(sound, config.MinDistance);
            ma_sound_set_max_distance(sound, config.MaxDistance);
            ma_sound_set_rolloff(sound, config.RolloffFactor);
            ma_sound_set_doppler_factor(sound, config.DopplerEnabled ? 1.0f : 0.0f);

            // Cone settings (convert degrees to radians)
            float innerAngleRad = glm::radians(config.ConeInnerAngle);
            float outerAngleRad = glm::radians(config.ConeOuterAngle);
            ma_sound_set_cone(sound, innerAngleRad, outerAngleRad, config.ConeOuterGain);
        }
    }

    void AudioSystem::OnSoundEnd(void* pUserData, ma_sound* pSound) {
        (void)pUserData;
        (void)pSound;
        // This callback is called from the audio thread
        // We mark the sound for cleanup in the main thread
    }

} // namespace Audio
} // namespace Core
