#pragma once

// Audio Source System
// Manages AudioSourceComponents, syncing their state with the AudioSystem
// Handles playback, fading, velocity tracking, and position updates

#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/AudioSourceComponent.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/Log.h"

#include <entt/entt.hpp>
#include <vector>
#include <random>

namespace Core {
namespace Audio {

    // ============================================================================
    // Audio Source System
    // ============================================================================
    // Updates all AudioSourceComponents each frame, managing their playback state,
    // position/velocity sync, and volume fading.

    class AudioSourceSystem {
    public:
        AudioSourceSystem() : m_RNG(std::random_device{}()) {}
        ~AudioSourceSystem() = default;

        // Initialize the system
        void Initialize() {
            m_Initialized = true;
            ENGINE_CORE_INFO("AudioSourceSystem: Initialized");
        }

        // Shutdown the system
        void Shutdown() {
            m_Initialized = false;
            ENGINE_CORE_INFO("AudioSourceSystem: Shutdown");
        }

        // Update all audio sources
        // Call this each frame after transforms are updated
        void Update(ECS::Scene* scene, float deltaTime) {
            if (!m_Initialized || !scene) return;

            auto& registry = scene->GetRegistry();
            auto& audioSystem = AudioSystem::Get();

            if (!audioSystem.IsInitialized()) return;

            // Process all audio source components
            auto view = registry.view<ECS::AudioSourceComponent>();

            for (auto entity : view) {
                auto& source = view.get<ECS::AudioSourceComponent>(entity);
                auto* transform = registry.try_get<ECS::TransformComponent>(entity);

                UpdateSource(entity, source, transform, deltaTime, audioSystem);
            }

            // Process pending removals
            ProcessPendingRemovals(registry);
        }

        // Handle component creation (called by registry observer)
        void OnAudioSourceCreated(ECS::Scene* scene, entt::entity entity) {
            if (!scene) return;

            auto& registry = scene->GetRegistry();
            auto* source = registry.try_get<ECS::AudioSourceComponent>(entity);

            if (source && source->PlayOnAwake) {
                source->Play();
                ENGINE_CORE_TRACE("AudioSourceSystem: Auto-playing source on entity {} (PlayOnAwake)",
                                 static_cast<uint32_t>(entity));
            }
        }

        // Manually trigger play on an entity's audio source
        void PlaySource(ECS::Scene* scene, entt::entity entity) {
            if (!scene) return;

            auto& registry = scene->GetRegistry();
            auto* source = registry.try_get<ECS::AudioSourceComponent>(entity);

            if (source) {
                source->Play();
            }
        }

        // Stop an entity's audio source
        void StopSource(ECS::Scene* scene, entt::entity entity) {
            if (!scene) return;

            auto& registry = scene->GetRegistry();
            auto* source = registry.try_get<ECS::AudioSourceComponent>(entity);

            if (source) {
                source->Stop();
            }
        }

        // Stop all audio sources
        void StopAllSources(ECS::Scene* scene) {
            if (!scene) return;

            auto& registry = scene->GetRegistry();
            auto view = registry.view<ECS::AudioSourceComponent>();

            for (auto entity : view) {
                auto& source = view.get<ECS::AudioSourceComponent>(entity);
                source.Stop();
            }
        }

        // Pause all audio sources
        void PauseAllSources(ECS::Scene* scene) {
            if (!scene) return;

            auto& registry = scene->GetRegistry();
            auto view = registry.view<ECS::AudioSourceComponent>();

            for (auto entity : view) {
                auto& source = view.get<ECS::AudioSourceComponent>(entity);
                source.Pause();
            }
        }

        // Resume all paused audio sources
        void ResumeAllSources(ECS::Scene* scene) {
            if (!scene) return;

            auto& registry = scene->GetRegistry();
            auto view = registry.view<ECS::AudioSourceComponent>();

            for (auto entity : view) {
                auto& source = view.get<ECS::AudioSourceComponent>(entity);
                if (source.IsPaused()) {
                    source.Resume();
                }
            }
        }

        // Play a one-shot sound at an entity's position
        SoundHandle PlayOneShotAt(ECS::Scene* scene, entt::entity entity, 
                                  const std::string& clipPath, float volume = 1.0f) {
            if (!scene) return InvalidSoundHandle;

            auto& registry = scene->GetRegistry();
            auto* transform = registry.try_get<ECS::TransformComponent>(entity);

            if (transform) {
                return AudioSystem::Get().PlaySoundOneShotAt(clipPath, transform->Position, volume);
            }
            
            return AudioSystem::Get().PlaySoundOneShot(clipPath, volume);
        }

        // Play a one-shot sound at a world position
        SoundHandle PlayOneShotAtPosition(const std::string& clipPath, 
                                          const Math::Vec3& position, float volume = 1.0f) {
            return AudioSystem::Get().PlaySoundOneShotAt(clipPath, position, volume);
        }

        // Get statistics
        uint32_t GetActiveSourceCount() const { return m_ActiveSourceCount; }
        uint32_t GetPlayingSourceCount() const { return m_PlayingSourceCount; }

    private:
        bool m_Initialized = false;
        std::mt19937 m_RNG;
        uint32_t m_ActiveSourceCount = 0;
        uint32_t m_PlayingSourceCount = 0;
        std::vector<entt::entity> m_PendingRemovals;

        // Update a single audio source
        void UpdateSource(entt::entity entity,
                         ECS::AudioSourceComponent& source,
                         ECS::TransformComponent* transform,
                         float deltaTime,
                         AudioSystem& audioSystem)
        {
            // Get position from transform
            Math::Vec3 position{0.0f};
            if (transform) {
                position = transform->Position;
            }

            // Calculate velocity for Doppler
            if (source.AutoCalculateVelocity && deltaTime > 0.0f) {
                source.CurrentVelocity = (position - source.LastPosition) / deltaTime;
            } else {
                source.CurrentVelocity = source.ManualVelocity;
            }
            source.LastPosition = position;

            // Handle state transitions
            switch (source.State) {
                case ECS::AudioPlaybackState::Stopped:
                    // Nothing to do
                    if (source.CurrentHandle != InvalidSoundHandle) {
                        audioSystem.StopSound(source.CurrentHandle);
                        source.CurrentHandle = InvalidSoundHandle;
                    }
                    break;

                case ECS::AudioPlaybackState::FadingIn:
                    UpdateFadeIn(source, deltaTime);
                    UpdatePlayingSource(source, position, audioSystem);
                    break;

                case ECS::AudioPlaybackState::Playing:
                    UpdatePlayingSource(source, position, audioSystem);
                    break;

                case ECS::AudioPlaybackState::Paused:
                    if (source.CurrentHandle != InvalidSoundHandle) {
                        audioSystem.PauseSound(source.CurrentHandle);
                    }
                    break;

                case ECS::AudioPlaybackState::FadingOut:
                    UpdateFadeOut(entity, source, deltaTime, audioSystem);
                    break;
            }

            // Check for playback completion
            if (source.CurrentHandle != InvalidSoundHandle && 
                source.State == ECS::AudioPlaybackState::Playing) {
                
                if (!audioSystem.IsSoundPlaying(source.CurrentHandle) && 
                    !audioSystem.IsSoundPaused(source.CurrentHandle)) {
                    
                    // Sound finished
                    if (!source.Loop) {
                        source.State = ECS::AudioPlaybackState::Stopped;
                        source.CurrentHandle = InvalidSoundHandle;
                        
                        // Handle one-shot removal
                        if (source.OneShot) {
                            m_PendingRemovals.push_back(entity);
                        }
                    }
                }
            }

            source.IsDirty = false;
        }

        // Update a playing source's position and properties
        void UpdatePlayingSource(ECS::AudioSourceComponent& source,
                                const Math::Vec3& position,
                                AudioSystem& audioSystem)
        {
            // Start playback if needed
            if (source.CurrentHandle == InvalidSoundHandle) {
                StartPlayback(source, position, audioSystem);
            }

            if (source.CurrentHandle == InvalidSoundHandle) return;

            // Update position
            if (source.Spatial) {
                audioSystem.SetSoundPosition(source.CurrentHandle, position);
                audioSystem.SetSoundVelocity(source.CurrentHandle, source.CurrentVelocity);
            }

            // Update volume
            audioSystem.SetSoundVolume(source.CurrentHandle, source.GetEffectiveVolume());

            // Update pitch
            audioSystem.SetSoundPitch(source.CurrentHandle, source.Pitch);
        }

        // Start playback of a source
        void StartPlayback(ECS::AudioSourceComponent& source,
                          const Math::Vec3& position,
                          AudioSystem& audioSystem)
        {
            // Select clip (support random selection)
            std::string clipPath = source.AudioClipPath;
            if (!source.RandomClips.empty()) {
                std::uniform_int_distribution<size_t> dist(0, source.RandomClips.size() - 1);
                clipPath = source.RandomClips[dist(m_RNG)];
            }

            if (clipPath.empty()) {
                ENGINE_CORE_WARN("AudioSourceSystem: Cannot play source with empty clip path");
                source.State = ECS::AudioPlaybackState::Stopped;
                return;
            }

            // Play the sound
            if (source.Spatial) {
                source.CurrentHandle = audioSystem.PlaySoundAt(clipPath, position, 
                                                               source.Loop, source.SpatialConfig);
            } else {
                source.CurrentHandle = audioSystem.PlaySound(clipPath, false, source.Loop);
            }

            if (source.CurrentHandle == InvalidSoundHandle) {
                ENGINE_CORE_ERROR("AudioSourceSystem: Failed to play '{}'", clipPath);
                source.State = ECS::AudioPlaybackState::Stopped;
                return;
            }

            // Apply initial settings
            audioSystem.SetSoundVolume(source.CurrentHandle, source.GetEffectiveVolume());
            audioSystem.SetSoundPitch(source.CurrentHandle, source.Pitch);

            // Set start time if specified
            if (source.StartTime > 0.0f) {
                audioSystem.SetSoundCurrentTime(source.CurrentHandle, source.StartTime);
            }

            ENGINE_CORE_TRACE("AudioSourceSystem: Started playback of '{}' (handle: {})", 
                             clipPath, source.CurrentHandle);
        }

        // Update fade-in
        void UpdateFadeIn(ECS::AudioSourceComponent& source, float deltaTime) {
            source.FadeTimer += deltaTime;
            
            if (source.FadeTimer >= source.FadeInTime) {
                // Fade complete
                source.CurrentVolume = source.Volume;
                source.State = ECS::AudioPlaybackState::Playing;
            } else {
                // Interpolate volume
                float t = source.FadeTimer / source.FadeInTime;
                source.CurrentVolume = source.Volume * t;
            }
        }

        // Update fade-out
        void UpdateFadeOut(entt::entity entity,
                          ECS::AudioSourceComponent& source, 
                          float deltaTime,
                          AudioSystem& audioSystem) 
        {
            source.FadeTimer += deltaTime;
            
            if (source.FadeTimer >= source.FadeOutTime) {
                // Fade complete - stop
                source.CurrentVolume = 0.0f;
                source.State = ECS::AudioPlaybackState::Stopped;
                
                if (source.CurrentHandle != InvalidSoundHandle) {
                    audioSystem.StopSound(source.CurrentHandle);
                    source.CurrentHandle = InvalidSoundHandle;
                }

                // Handle one-shot removal
                if (source.OneShot) {
                    m_PendingRemovals.push_back(entity);
                }
            } else {
                // Interpolate volume
                float t = 1.0f - (source.FadeTimer / source.FadeOutTime);
                source.CurrentVolume = source.Volume * t;
                
                if (source.CurrentHandle != InvalidSoundHandle) {
                    audioSystem.SetSoundVolume(source.CurrentHandle, source.GetEffectiveVolume());
                }
            }
        }

        // Process entities pending removal (one-shot sounds)
        void ProcessPendingRemovals(entt::registry& registry) {
            for (auto entity : m_PendingRemovals) {
                if (registry.valid(entity)) {
                    registry.remove<ECS::AudioSourceComponent>(entity);
                    ENGINE_CORE_TRACE("AudioSourceSystem: Removed one-shot AudioSource from entity {}",
                                     static_cast<uint32_t>(entity));
                }
            }
            m_PendingRemovals.clear();
        }
    };

    // ============================================================================
    // Global Audio Source System Instance
    // ============================================================================

    inline AudioSourceSystem& GetAudioSourceSystem() {
        static AudioSourceSystem instance;
        return instance;
    }

    // ============================================================================
    // Convenience Functions
    // ============================================================================

    // Play a one-shot sound at a position
    inline SoundHandle PlaySFX(const std::string& clipPath, const Math::Vec3& position, float volume = 1.0f) {
        return AudioSystem::Get().PlaySoundOneShotAt(clipPath, position, volume);
    }

    // Play a one-shot non-spatial sound
    inline SoundHandle PlaySFX(const std::string& clipPath, float volume = 1.0f) {
        return AudioSystem::Get().PlaySoundOneShot(clipPath, volume);
    }

    // Play a random sound from a list at a position
    inline SoundHandle PlayRandomSFX(const std::vector<std::string>& clips, 
                                     const Math::Vec3& position, float volume = 1.0f) {
        if (clips.empty()) return InvalidSoundHandle;
        
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, clips.size() - 1);
        
        return AudioSystem::Get().PlaySoundOneShotAt(clips[dist(rng)], position, volume);
    }

} // namespace Audio
} // namespace Core
