#include "Core/ECS/SystemPipeline.h"

#include "Core/ECS/Scene.h"
#include "Core/ECS/Systems/TransformSystem.h"
#include "Core/ECS/Systems/PhysicsSystem.h"
#include "Core/ECS/Systems/PhysicsSyncSystem.h"
#include "Core/ECS/Systems/CameraSystem.h"
#include "Core/ECS/Systems/CharacterControllerSystem.h"
#include "Core/ECS/Systems/PlayerControllerSystem.h"
#include "Core/ECS/Systems/FirstPersonCameraSystem.h"
#include "Core/ECS/Systems/ThirdPersonCameraSystem.h"
#include "Core/ECS/Systems/CameraViewInterpolatorSystem.h"
#include "Core/ECS/Systems/AnimatorSystem.h"
#include "Core/ECS/Systems/IKSystem.h"
#include "Core/Navigation/NavigationSystem.h"
#include "Core/ECS/Systems/LightSystem.h"
#include "Core/ECS/Systems/RenderSystem.h"
#include "Core/ECS/Systems/SkeletalRenderSystem.h"
#include "Core/ECS/Systems/TerrainSystem.h"
#include "Core/ECS/Systems/FoliageSystem.h"
#include "Core/ECS/Systems/SkyboxSystem.h"
#include "Core/ECS/Systems/BehaviorTreeSystem.h"
#include "Core/ECS/Systems/FSMSystem.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/Audio/AudioSourceSystem.h"
#include "Core/Audio/PhysicsAudioIntegration.h"
#include "Core/ECS/Components/AudioListenerComponent.h"
#include "Core/ECS/Components/AudioSourceComponent.h"
#include "Core/RHI/RHIDevice.h"
#include "Core/Physics/PhysicsWorld.h"
#include "Core/InputMapper.h"
#include "Core/JobSystem/JobSystem.h"
#include "Core/Log.h"
#include "Core/Profile.h"

#include <algorithm>
#include <chrono>

namespace Core {
namespace ECS {

    SystemPipeline::SystemPipeline() = default;

    SystemPipeline::~SystemPipeline() {
        Shutdown();
    }

    bool SystemPipeline::Initialize(const SystemPipelineConfig& config,
                                    std::shared_ptr<RHI::RHIDevice> device) {
        PROFILE_FUNCTION();

        if (m_Initialized) {
            ENGINE_CORE_WARN("SystemPipeline already initialized");
            return true;
        }

        m_Config = config;
        m_Device = std::move(device);

        // Worker threads back the parallel systems and Jolt's job adapter. Without
        // this the job system runs everything inline, which is correct but serial.
        JobSystem::Initialize();

        if (m_Config.EnablePhysics) {
            m_PhysicsWorld = std::make_unique<Physics::PhysicsWorld>();
            if (!m_PhysicsWorld->Initialize()) {
                ENGINE_CORE_ERROR("SystemPipeline: physics world failed to initialize; continuing without physics");
                m_PhysicsWorld.reset();
            }
        }

        if (m_Config.EnableInput) {
            m_InputMapper = std::make_unique<InputMapper>();
            m_InputMapper->SetupDefaultBindings();
        }

        m_TransformSystem = std::make_unique<TransformSystem>();

        if (m_PhysicsWorld) {
            m_PhysicsSystem = std::make_unique<PhysicsSystem>();
            m_PhysicsSystem->Initialize(m_PhysicsWorld.get());
            m_PhysicsSystem->SetFixedTimestep(m_Config.PhysicsFixedTimestep);
            m_PhysicsSystem->SetCollisionSteps(m_Config.PhysicsCollisionSteps);

            m_PhysicsSyncSystem = std::make_unique<PhysicsSyncSystem>();
            m_PhysicsSyncSystem->Initialize(m_PhysicsWorld.get());

            if (m_Config.EnableCharacterController) {
                m_CharacterControllerSystem = std::make_unique<CharacterControllerSystem>();
                m_CharacterControllerSystem->Initialize(m_PhysicsWorld.get());
            }

            if (m_Config.EnableInverseKinematics) {
                m_IKSystem = std::make_unique<IKSystem>();
                m_IKSystem->Initialize(m_PhysicsWorld.get());
            }

            m_ThirdPersonCameraSystem = std::make_unique<ThirdPersonCameraSystem>();
            m_ThirdPersonCameraSystem->Initialize(m_PhysicsWorld.get());
        }

        if (m_InputMapper) {
            m_PlayerControllerSystem = std::make_unique<PlayerControllerSystem>();
            m_PlayerControllerSystem->Initialize(m_InputMapper.get());
        }

        if (m_Config.EnableAnimation) {
            m_AnimatorSystem = std::make_unique<AnimatorSystem>();
            m_AnimatorSystem->Initialize();
        }

        if (m_Config.EnableCameras) {
            m_FirstPersonCameraSystem = std::make_unique<FirstPersonCameraSystem>();

            m_CameraViewInterpolatorSystem = std::make_unique<CameraViewInterpolatorSystem>();
            m_CameraViewInterpolatorSystem->Initialize();

            m_CameraSystem = std::make_unique<CameraSystem>();
            m_CameraSystem->SetScreenDimensions(m_Config.ScreenWidth, m_Config.ScreenHeight);
        }

        if (m_Config.EnableNavigation) {
            // NavigationSystem is a singleton; the pipeline owns its lifetime for
            // the duration of the scene. A NavMesh still has to be built before
            // agents can path - see the RebuildNavMesh MCP tool.
            if (Navigation::NavigationSystem::Get().Initialize()) {
                m_NavigationActive = true;
            } else {
                ENGINE_CORE_WARN("SystemPipeline: navigation failed to initialize; continuing without it");
            }
        }

        if (m_Config.EnableAI) {
            m_BehaviorTreeSystem = std::make_unique<BehaviorTreeSystem>();
            m_BehaviorTreeSystem->Initialize();

            m_FSMSystem = std::make_unique<FSMSystem>();
            m_FSMSystem->Initialize();
        }

        if (m_Config.EnableAudio) {
            Audio::GetAudioSourceSystem().Initialize();
            m_AudioActive = true;
        }

        if (m_Config.EnableLighting) {
            m_LightSystem = std::make_unique<LightSystem>();
        }

        if (m_Config.EnableRenderCollection) {
            // Nothing advanced an animation clip into a pose: AnimatorSystem only
            // runs for entities with an AnimatorComponent and a graph, and this
            // system was never constructed. An imported character therefore
            // played nothing.
            m_SkeletalRenderSystem = std::make_unique<SkeletalRenderSystem>();

            m_RenderSystem = std::make_unique<RenderSystem>();
        }

        // Terrain, foliage, and sky upload geometry through the RHI. Without a
        // device they cannot do anything useful, so they are skipped entirely
        // rather than constructed in a half-working state.
        if (m_Device) {
            if (m_Config.EnableTerrain) {
                m_TerrainSystem = std::make_unique<TerrainSystem>();
                m_TerrainSystem->Initialize(m_Device);
            }
            if (m_Config.EnableFoliage) {
                m_FoliageSystem = std::make_unique<FoliageSystem>();
                m_FoliageSystem->Initialize(m_Device);
            }
            if (m_Config.EnableSkybox) {
                m_SkyboxSystem = std::make_unique<SkyboxSystem>();
                m_SkyboxSystem->Initialize(m_Device);
            }
        } else if (m_Config.EnableTerrain || m_Config.EnableFoliage || m_Config.EnableSkybox) {
            ENGINE_CORE_INFO("SystemPipeline: no RHI device supplied; terrain, foliage and sky are disabled");
        }

        m_Initialized = true;
        ENGINE_CORE_INFO("SystemPipeline initialized (physics={}, input={}, animation={}, ai={}, audio={}, navigation={}, cameras={})",
                         m_PhysicsWorld != nullptr, m_InputMapper != nullptr,
                         m_AnimatorSystem != nullptr, m_BehaviorTreeSystem != nullptr,
                         m_AudioActive, m_NavigationActive,
                         m_CameraSystem != nullptr);
        ENGINE_CORE_INFO("SystemPipeline world systems (terrain={}, foliage={}, skybox={})",
                         m_TerrainSystem != nullptr, m_FoliageSystem != nullptr,
                         m_SkyboxSystem != nullptr);
        return true;
    }

    void SystemPipeline::Shutdown() {
        if (!m_Initialized) {
            return;
        }

        // Reverse of construction: systems first, then the resources they point at.
        if (m_AudioActive) {
            Audio::GetAudioSourceSystem().Shutdown();
            m_AudioActive = false;
        }
        if (m_SkyboxSystem) { m_SkyboxSystem->Shutdown(); m_SkyboxSystem.reset(); }
        if (m_FoliageSystem) { m_FoliageSystem->Shutdown(); m_FoliageSystem.reset(); }
        if (m_TerrainSystem) { m_TerrainSystem->Shutdown(); m_TerrainSystem.reset(); }
        m_RenderSystem.reset();
        m_SkeletalRenderSystem.reset();
        m_LightSystem.reset();
        m_CameraSystem.reset();
        m_CameraViewInterpolatorSystem.reset();
        m_FirstPersonCameraSystem.reset();

        if (m_ThirdPersonCameraSystem) {
            m_ThirdPersonCameraSystem.reset();
        }
        if (m_IKSystem) {
            m_IKSystem->Shutdown();
            m_IKSystem.reset();
        }
        if (m_AnimatorSystem) {
            m_AnimatorSystem->Shutdown();
            m_AnimatorSystem.reset();
        }
        if (m_FSMSystem) {
            m_FSMSystem->Shutdown();
            m_FSMSystem.reset();
        }
        if (m_BehaviorTreeSystem) {
            m_BehaviorTreeSystem->Shutdown();
            m_BehaviorTreeSystem.reset();
        }
        if (m_PlayerControllerSystem) {
            m_PlayerControllerSystem.reset();
        }
        if (m_CharacterControllerSystem) {
            m_CharacterControllerSystem->Shutdown();
            m_CharacterControllerSystem.reset();
        }
        if (m_PhysicsSyncSystem) {
            m_PhysicsSyncSystem->Shutdown();
            m_PhysicsSyncSystem.reset();
        }
        if (m_PhysicsSystem) {
            m_PhysicsSystem->Shutdown();
            m_PhysicsSystem.reset();
        }
        m_TransformSystem.reset();

        if (m_NavigationActive) {
            Navigation::NavigationSystem::Get().Shutdown();
            m_NavigationActive = false;
        }

        m_InputMapper.reset();

        if (m_PhysicsWorld) {
            m_PhysicsWorld->Shutdown();
            m_PhysicsWorld.reset();
        }

        m_Device.reset();

        // Workers must stop before the systems they captured work for are gone.
        JobSystem::Shutdown();

        m_Initialized = false;
        ENGINE_CORE_INFO("SystemPipeline shutdown");
    }

    void SystemPipeline::SetTimeScale(float scale) {
        // Negative time would run integrators backwards; nothing downstream is
        // written to survive that.
        m_TimeScale = std::clamp(scale, 0.0f, 100.0f);
    }

    void SystemPipeline::SetScreenDimensions(uint32_t width, uint32_t height) {
        m_Config.ScreenWidth = width;
        m_Config.ScreenHeight = height;
        if (m_CameraSystem) {
            m_CameraSystem->SetScreenDimensions(width, height);
        }
    }

    void SystemPipeline::Update(Scene& scene, float deltaTime) {
        PROFILE_FUNCTION();

        if (!m_Initialized) {
            return;
        }

        // Paused: only run if frames were explicitly requested, and consume one.
        if (m_Paused) {
            if (m_PendingStepFrames == 0) {
                return;
            }
            --m_PendingStepFrames;
        }

        const auto frameStart = std::chrono::high_resolution_clock::now();

        // A negative or absurd delta (clock jump, resumed breakpoint) would blow up
        // every integrator downstream.
        float dt = std::clamp(deltaTime, 0.0f, m_Config.MaxFrameDeltaSeconds);
        if (m_FixedFrameDelta > 0.0f) {
            dt = m_FixedFrameDelta;
        }
        dt *= m_TimeScale;

        // 1. Input sampling. Must precede anything that reads action state so that
        //    JustPressed/JustReleased refer to this frame.
        if (m_InputMapper) {
            m_InputMapper->Update(dt);
        }

        // 1b. AI decision making: behavior trees and finite state machines.
        if (m_BehaviorTreeSystem) {
            m_BehaviorTreeSystem->Update(scene, dt);
        }
        if (m_FSMSystem) {
            m_FSMSystem->Update(scene, dt);
        }

        // 2. Gameplay intent -> character motion.
        if (m_PlayerControllerSystem) {
            m_PlayerControllerSystem->Update(scene, dt);
        }
        if (m_CharacterControllerSystem) {
            m_CharacterControllerSystem->Update(scene, dt);
        }

        // 3. Physics: publish ECS changes, capture pre-step state for interpolation,
        //    step, then read results back into transforms.
        if (m_PhysicsSystem) {
            m_PhysicsSystem->PreUpdate(scene);
            if (m_PhysicsSyncSystem) {
                m_PhysicsSyncSystem->StorePreviousState(scene);
            }
            m_PhysicsSystem->Update(dt);
        }
        if (m_PhysicsSyncSystem) {
            m_PhysicsSyncSystem->Update(scene);
        }
        if (m_AudioActive && Audio::PhysicsAudioManager::Get().IsInitialized()) {
            Audio::PhysicsAudioManager::Get().Update(dt);
        }

        // 4. Animation, then IK on top of the animated pose.
        if (m_AnimatorSystem) {
            m_AnimatorSystem->Update(scene, dt);
        }
        // After the animator and before IK, so a graph-driven pose wins and IK
        // still gets the last word on the result.
        if (m_SkeletalRenderSystem) {
            m_SkeletalRenderSystem->UpdateAnimations(scene, dt);
        }
        if (m_IKSystem) {
            m_IKSystem->Update(scene, dt);
        }

        // 4b. Navigation: crowd steering and patrol advance run after physics has
        //     settled positions but before cameras read them.
        if (m_NavigationActive) {
            Navigation::NavigationSystem::Get().Update(dt);
        }

        // 5. Cameras follow the final character/bone positions.
        if (m_FirstPersonCameraSystem) {
            m_FirstPersonCameraSystem->Update(scene, dt);
        }
        if (m_ThirdPersonCameraSystem) {
            m_ThirdPersonCameraSystem->Update(scene, dt);
        }
        if (m_CameraViewInterpolatorSystem) {
            m_CameraViewInterpolatorSystem->Update(scene, dt);
        }

        // 5b. World streaming: terrain chunks and foliage follow the camera, sky
        //     advances its time of day. These run before the transform resolve so
        //     anything they spawn is included this frame.
        if (m_TerrainSystem) {
            m_TerrainSystem->Update(scene, dt);
        }
        if (m_FoliageSystem) {
            m_FoliageSystem->Update(scene, dt);
        }
        if (m_SkyboxSystem) {
            m_SkyboxSystem->Update(scene, dt);
        }

        // 6. Resolve the hierarchy once, after every writer of local transforms and
        //    before any consumer of world transforms.
        if (m_TransformSystem) {
            m_TransformSystem->Update(scene);
        }

        // 7. Consumers of world transforms.
        if (m_CameraSystem) {
            m_CameraSystem->Update(scene);
        }
        if (m_LightSystem) {
            m_LightSystem->Update(scene);
        }
        if (m_RenderSystem) {
            m_RenderSystem->Update(scene);
        }

        // 7b. Audio listener orientation and spatial source updates.
        if (m_AudioActive) {
            if (Audio::AudioSystem::Get().IsInitialized()) {
                auto listenerView = scene.GetRegistry().view<AudioListenerComponent, TransformComponent>();
                bool listenerFound = false;
                for (auto entity : listenerView) {
                    const auto& [listener, transform] = listenerView.get<AudioListenerComponent, TransformComponent>(entity);
                    if (listener.IsActive) {
                        Audio::AudioSystem::Get().SetListenerPosition(transform.Position, listener.ListenerIndex);
                        Audio::AudioSystem::Get().SetListenerOrientation(Math::Quat(transform.Rotation), listener.ListenerIndex);
                        listenerFound = true;
                        break;
                    }
                }
                if (!listenerFound && m_CameraSystem) {
                    const auto activeCameraEntity = m_CameraSystem->GetActiveCameraEntity();
                    if (activeCameraEntity != entt::null && scene.GetRegistry().all_of<TransformComponent>(activeCameraEntity)) {
                        const auto& cameraTransform = scene.GetRegistry().get<TransformComponent>(activeCameraEntity);
                        Audio::AudioSystem::Get().SetListenerPosition(cameraTransform.Position, 0);
                        Audio::AudioSystem::Get().SetListenerOrientation(Math::Quat(cameraTransform.Rotation), 0);
                    }
                }
            }
            Audio::GetAudioSourceSystem().Update(&scene, dt);
        }

        const auto frameEnd = std::chrono::high_resolution_clock::now();
        m_LastUpdateSeconds = std::chrono::duration<float>(frameEnd - frameStart).count();
        ++m_FrameCount;
    }

} // namespace ECS
} // namespace Core
