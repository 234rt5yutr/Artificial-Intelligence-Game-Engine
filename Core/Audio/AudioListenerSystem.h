#pragma once

// Audio Listener System
// Updates the audio system's listener position/orientation based on AudioListenerComponents
// Automatically binds to active cameras when configured

#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/CameraComponent.h"
#include "Core/ECS/Components/AudioListenerComponent.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/Log.h"

#include <entt/entt.hpp>
#include <vector>
#include <algorithm>

namespace Core {
namespace Audio {

    // ============================================================================
    // Audio Listener System
    // ============================================================================
    // This system synchronizes AudioListenerComponents with the AudioSystem.
    // It should be updated each frame after transforms are finalized.

    class AudioListenerSystem {
    public:
        AudioListenerSystem() = default;
        ~AudioListenerSystem() = default;

        // Initialize the system
        void Initialize() {
            m_Initialized = true;
            ENGINE_CORE_INFO("AudioListenerSystem: Initialized");
        }

        // Shutdown the system
        void Shutdown() {
            m_Initialized = false;
            ENGINE_CORE_INFO("AudioListenerSystem: Shutdown");
        }

        // Update all audio listeners
        // Call this each frame after transforms are updated
        void Update(ECS::Scene* scene, float deltaTime) {
            if (!m_Initialized || !scene) return;

            auto& registry = scene->GetRegistry();
            auto& audioSystem = AudioSystem::Get();

            if (!audioSystem.IsInitialized()) return;

            // First, handle auto-binding to active cameras
            if (m_AutoBindToCameras) {
                AutoBindListenersToActiveCameras(registry);
            }

            // Collect all active listeners with their priorities
            struct ListenerData {
                entt::entity Entity;
                ECS::AudioListenerComponent* Listener;
                ECS::TransformComponent* Transform;
                int32_t Priority;
            };

            std::vector<ListenerData> activeListeners;

            auto view = registry.view<ECS::AudioListenerComponent>();
            for (auto entity : view) {
                auto& listener = view.get<ECS::AudioListenerComponent>(entity);
                
                if (!listener.IsActive) continue;

                auto* transform = registry.try_get<ECS::TransformComponent>(entity);
                
                activeListeners.push_back({
                    entity,
                    &listener,
                    transform,
                    listener.Priority
                });
            }

            // Sort by priority (highest first)
            std::sort(activeListeners.begin(), activeListeners.end(),
                [](const ListenerData& a, const ListenerData& b) {
                    return a.Priority > b.Priority;
                });

            // Update each active listener
            for (size_t i = 0; i < activeListeners.size(); ++i) {
                auto& data = activeListeners[i];
                UpdateListener(data.Entity, *data.Listener, data.Transform, 
                              static_cast<uint32_t>(i), deltaTime, audioSystem);
            }

            // Store active listener count for diagnostics
            m_ActiveListenerCount = static_cast<uint32_t>(activeListeners.size());
        }

        // Get the number of active listeners
        uint32_t GetActiveListenerCount() const { return m_ActiveListenerCount; }

        // Enable/disable auto-binding to active cameras
        void SetAutoBindToCameras(bool enable) { m_AutoBindToCameras = enable; }
        bool IsAutoBindToCamerasEnabled() const { return m_AutoBindToCameras; }

        // Manually set an entity as the primary audio listener
        void SetPrimaryListener(ECS::Scene* scene, entt::entity entity) {
            if (!scene) return;

            auto& registry = scene->GetRegistry();

            // Deactivate all other listeners
            auto view = registry.view<ECS::AudioListenerComponent>();
            for (auto e : view) {
                auto& listener = view.get<ECS::AudioListenerComponent>(e);
                if (e == entity) {
                    listener.IsActive = true;
                    listener.ListenerIndex = 0;
                    listener.Priority = 100;
                } else {
                    listener.Priority = 0;
                }
            }

            ENGINE_CORE_INFO("AudioListenerSystem: Set primary listener to entity {}", 
                            static_cast<uint32_t>(entity));
        }

        // Add an AudioListenerComponent to the active camera if it doesn't have one
        void EnsureActiveCameraHasListener(ECS::Scene* scene) {
            if (!scene) return;

            auto& registry = scene->GetRegistry();

            // Find active camera
            auto cameraView = registry.view<ECS::CameraComponent, ECS::TransformComponent>();
            for (auto entity : cameraView) {
                auto& camera = cameraView.get<ECS::CameraComponent>(entity);
                
                if (camera.IsActive) {
                    // Check if it already has a listener
                    if (!registry.all_of<ECS::AudioListenerComponent>(entity)) {
                        // Add a listener component
                        auto& listener = registry.emplace<ECS::AudioListenerComponent>(entity);
                        listener = ECS::AudioListenerComponent::CreatePrimary();
                        ENGINE_CORE_INFO("AudioListenerSystem: Added AudioListener to active camera entity {}",
                                        static_cast<uint32_t>(entity));
                    }
                    break;
                }
            }
        }

    private:
        bool m_Initialized = false;
        bool m_AutoBindToCameras = true;
        uint32_t m_ActiveListenerCount = 0;

        // Update a single listener
        void UpdateListener(entt::entity entity,
                           ECS::AudioListenerComponent& listener,
                           ECS::TransformComponent* transform,
                           uint32_t listenerIndex,
                           float deltaTime,
                           AudioSystem& audioSystem) 
        {
            Math::Vec3 position{0.0f};
            Math::Quat orientation{1.0f, 0.0f, 0.0f, 0.0f};

            // Get position and orientation from transform
            if (transform && listener.AutoUpdateFromTransform) {
                position = transform->Position;
                orientation = transform->Rotation;
            }

            // Apply orientation override if set
            if (listener.UseOrientationOverride) {
                orientation = listener.OrientationOverride;
            }

            // Calculate velocity for Doppler effect
            Math::Vec3 velocity = listener.Velocity;
            if (listener.AutoCalculateVelocity && deltaTime > 0.0f) {
                velocity = (position - listener.LastPosition) / deltaTime;
                listener.Velocity = velocity;
            }
            listener.LastPosition = position;

            // Update the listener index in case it changed
            listener.ListenerIndex = listenerIndex;

            // Build listener config
            ListenerConfig config;
            config.Position = position;
            config.Orientation = orientation;
            config.Velocity = velocity;
            config.ListenerIndex = listenerIndex;

            // Update audio system
            audioSystem.SetListener(config);

            // Apply master gain for this listener
            // Note: In a multi-listener scenario, you might want separate gain control
            // For now, the primary listener (index 0) controls master volume
            if (listenerIndex == 0) {
                // Don't override user's master volume setting, just track listener gain
                // audioSystem.SetMasterVolume(listener.MasterGain);
            }
        }

        // Auto-bind listeners to active cameras
        void AutoBindListenersToActiveCameras(entt::registry& registry) {
            // Find active camera(s)
            auto cameraView = registry.view<ECS::CameraComponent, ECS::TransformComponent>();
            
            for (auto entity : cameraView) {
                auto& camera = cameraView.get<ECS::CameraComponent>(entity);
                
                // Check for listener on this camera entity
                auto* listener = registry.try_get<ECS::AudioListenerComponent>(entity);
                
                if (listener && listener->AutoBindToActiveCamera) {
                    // Activate listener if camera is active, deactivate if not
                    if (camera.IsActive && !listener->IsActive) {
                        listener->IsActive = true;
                        listener->Priority = 100;  // Primary listener priority
                        ENGINE_CORE_TRACE("AudioListenerSystem: Activated listener on active camera");
                    } else if (!camera.IsActive && listener->IsActive && listener->AutoBindToActiveCamera) {
                        // Only auto-deactivate if this listener is set to auto-bind
                        // This allows manual control of some listeners
                        listener->Priority = 0;
                    }
                }
            }
        }
    };

    // ============================================================================
    // Global Audio Listener System Instance
    // ============================================================================

    inline AudioListenerSystem& GetAudioListenerSystem() {
        static AudioListenerSystem instance;
        return instance;
    }

} // namespace Audio
} // namespace Core
