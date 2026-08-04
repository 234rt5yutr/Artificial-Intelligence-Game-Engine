#pragma once

// Scene system pipeline.
//
// The engine had a full set of ECS systems but nothing that ran them: Scene::OnUpdate
// only ticked the UI. This owns the per-frame ordering (input -> character -> physics
// -> animation -> camera -> transform hierarchy -> render collection) plus the shared
// dependencies those systems need (physics world, input mapper).
//
// Every stage is optional. A stage whose dependency is unavailable (headless run, no
// physics) is skipped rather than disabled at compile time.

#include <memory>
#include <cstdint>

namespace Core {

class InputMapper;

namespace Physics { class PhysicsWorld; }
namespace RHI { class RHIDevice; }

namespace ECS {

    class Scene;
    class TransformSystem;
    class PhysicsSystem;
    class PhysicsSyncSystem;
    class CameraSystem;
    class CharacterControllerSystem;
    class PlayerControllerSystem;
    class FirstPersonCameraSystem;
    class ThirdPersonCameraSystem;
    class CameraViewInterpolatorSystem;
    class AnimatorSystem;
    class IKSystem;
    class LightSystem;
    class RenderSystem;
    class TerrainSystem;
    class FoliageSystem;
    class SkyboxSystem;

    struct SystemPipelineConfig {
        // Simulation
        bool EnablePhysics = true;
        float PhysicsFixedTimestep = 1.0f / 60.0f;
        int PhysicsCollisionSteps = 1;

        // Gameplay
        bool EnableInput = true;
        bool EnableCharacterController = true;
        bool EnableAnimation = true;
        bool EnableInverseKinematics = true;
        // Recast/Detour navigation: crowd agents, patrol routes, dynamic obstacles.
        bool EnableNavigation = true;
        // World rendering systems. These need an RHI device; when none is
        // supplied they are skipped rather than half-initialized.
        bool EnableTerrain = true;
        bool EnableFoliage = true;
        bool EnableSkybox = true;

        // Rendering-side collection (safe headless: produces draw lists only)
        bool EnableCameras = true;
        bool EnableLighting = true;
        bool EnableRenderCollection = true;

        // Clamp for a single frame's delta so a stall (breakpoint, load hitch) cannot
        // push physics or animation through a huge integration step.
        float MaxFrameDeltaSeconds = 0.25f;

        uint32_t ScreenWidth = 1280;
        uint32_t ScreenHeight = 720;
    };

    class SystemPipeline {
    public:
        SystemPipeline();
        ~SystemPipeline();

        SystemPipeline(const SystemPipeline&) = delete;
        SystemPipeline& operator=(const SystemPipeline&) = delete;

        // `device` is optional. Terrain, foliage, and sky need it for GPU uploads;
        // pass nullptr (headless, or before the renderer exists) and those three
        // are skipped while everything else still runs.
        bool Initialize(const SystemPipelineConfig& config,
                        std::shared_ptr<RHI::RHIDevice> device = nullptr);
        void Shutdown();
        bool IsInitialized() const { return m_Initialized; }

        // Runs one frame of simulation over the scene.
        void Update(Scene& scene, float deltaTime);

        void SetScreenDimensions(uint32_t width, uint32_t height);

        const SystemPipelineConfig& GetConfig() const { return m_Config; }

        // --- Simulation control ---------------------------------------------
        // Lets an external driver (editor, MCP agent) freeze the world, advance it
        // a deterministic number of frames, or run it in slow motion. Rendering and
        // UI keep running while paused so the frozen frame stays inspectable.
        void SetPaused(bool paused) { m_Paused = paused; }
        bool IsPaused() const { return m_Paused; }

        // Queues N frames to run even while paused, then re-freezes.
        void RequestStepFrames(uint32_t frames) { m_PendingStepFrames += frames; }
        uint32_t GetPendingStepFrames() const { return m_PendingStepFrames; }

        // Multiplies the delta handed to every system. 0 freezes, 1 is realtime.
        void SetTimeScale(float scale);
        float GetTimeScale() const { return m_TimeScale; }

        // Forces a constant delta instead of wall-clock time; required for
        // reproducible play-mode runs. Zero disables.
        void SetFixedFrameDelta(float seconds) { m_FixedFrameDelta = seconds; }
        float GetFixedFrameDelta() const { return m_FixedFrameDelta; }

        // Shared dependencies, exposed so tools and gameplay code can reach them.
        Physics::PhysicsWorld* GetPhysicsWorld() const { return m_PhysicsWorld.get(); }
        InputMapper* GetInputMapper() const { return m_InputMapper.get(); }

        // Individual systems, for callers that need their results (draw commands,
        // light lists, camera matrices).
        CameraSystem* GetCameraSystem() const { return m_CameraSystem.get(); }
        LightSystem* GetLightSystem() const { return m_LightSystem.get(); }
        RenderSystem* GetRenderSystem() const { return m_RenderSystem.get(); }
        AnimatorSystem* GetAnimatorSystem() const { return m_AnimatorSystem.get(); }
        PhysicsSystem* GetPhysicsSystem() const { return m_PhysicsSystem.get(); }
        TerrainSystem* GetTerrainSystem() const { return m_TerrainSystem.get(); }
        FoliageSystem* GetFoliageSystem() const { return m_FoliageSystem.get(); }
        SkyboxSystem* GetSkyboxSystem() const { return m_SkyboxSystem.get(); }

        // Frame statistics
        float GetLastUpdateSeconds() const { return m_LastUpdateSeconds; }
        uint64_t GetFrameCount() const { return m_FrameCount; }

    private:
        bool m_Initialized = false;
        SystemPipelineConfig m_Config{};

        std::unique_ptr<Physics::PhysicsWorld> m_PhysicsWorld;
        std::unique_ptr<InputMapper> m_InputMapper;

        std::unique_ptr<TransformSystem> m_TransformSystem;
        std::unique_ptr<PhysicsSystem> m_PhysicsSystem;
        std::unique_ptr<PhysicsSyncSystem> m_PhysicsSyncSystem;
        std::unique_ptr<CameraSystem> m_CameraSystem;
        std::unique_ptr<CharacterControllerSystem> m_CharacterControllerSystem;
        std::unique_ptr<PlayerControllerSystem> m_PlayerControllerSystem;
        std::unique_ptr<FirstPersonCameraSystem> m_FirstPersonCameraSystem;
        std::unique_ptr<ThirdPersonCameraSystem> m_ThirdPersonCameraSystem;
        std::unique_ptr<CameraViewInterpolatorSystem> m_CameraViewInterpolatorSystem;
        std::unique_ptr<AnimatorSystem> m_AnimatorSystem;
        std::unique_ptr<IKSystem> m_IKSystem;
        std::unique_ptr<LightSystem> m_LightSystem;
        std::unique_ptr<RenderSystem> m_RenderSystem;
        std::unique_ptr<TerrainSystem> m_TerrainSystem;
        std::unique_ptr<FoliageSystem> m_FoliageSystem;
        std::unique_ptr<SkyboxSystem> m_SkyboxSystem;
        std::shared_ptr<RHI::RHIDevice> m_Device;

        // NavigationSystem is a singleton, so the pipeline only tracks whether it
        // owns the lifetime rather than holding a pointer.
        bool m_NavigationActive = false;

        float m_LastUpdateSeconds = 0.0f;
        uint64_t m_FrameCount = 0;

        bool m_Paused = false;
        uint32_t m_PendingStepFrames = 0;
        float m_TimeScale = 1.0f;
        float m_FixedFrameDelta = 0.0f;
    };

} // namespace ECS
} // namespace Core
