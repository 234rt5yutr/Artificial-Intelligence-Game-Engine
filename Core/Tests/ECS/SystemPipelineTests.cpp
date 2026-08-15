// Checks the SystemPipeline frame-gating logic: pause, single-step, time scale,
// fixed delta, and delta clamping. These are the branches an external driver
// (editor, MCP ControlSimulation tool) depends on, and they are easy to get
// subtly wrong.

#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/SystemPipeline.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/AudioListenerComponent.h"
#include "Core/ECS/Components/AudioSourceComponent.h"
#include "Core/ECS/Components/BehaviorTreeComponent.h"
#include "Core/ECS/Components/FSMComponent.h"
#include "Core/Log.h"

#include <cstdio>
#include <cstdlib>

// ctest runs these with -C Release, where NDEBUG turns assert() into a no-op:
// the condition is not evaluated, so any call inside one silently disappears and
// nothing is actually verified. CHECK always evaluates and always reports.
#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n",                 \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

namespace {

Core::ECS::SystemPipelineConfig HeadlessConfig() {
    Core::ECS::SystemPipelineConfig config{};
    // Keep the test independent of Jolt, Vulkan, and SDL: only the frame-gating
    // logic is under test here.
    config.EnablePhysics = false;
    config.EnableInput = false;
    config.EnableCharacterController = false;
    config.EnableAnimation = false;
    config.EnableInverseKinematics = false;
    config.EnableCameras = false;
    config.EnableLighting = false;
    config.EnableRenderCollection = false;
    return config;
}

} // namespace

int main() {
    using namespace Core::ECS;

    Engine::Log::Init();

    Scene scene("PipelineTestScene");
    CHECK(scene.InitializeSystems(HeadlessConfig()));

    SystemPipeline* pipeline = scene.GetSystemPipeline();
    CHECK(pipeline != nullptr);
    CHECK(pipeline->IsInitialized());
    CHECK(pipeline->GetFrameCount() == 0);

    // Running unpaused advances the frame counter.
    scene.OnUpdate(1.0f / 60.0f);
    CHECK(pipeline->GetFrameCount() == 1);

    // Pausing freezes the simulation entirely.
    pipeline->SetPaused(true);
    scene.OnUpdate(1.0f / 60.0f);
    scene.OnUpdate(1.0f / 60.0f);
    CHECK(pipeline->GetFrameCount() == 1);

    // A step request advances exactly that many frames, then re-freezes.
    pipeline->RequestStepFrames(2);
    CHECK(pipeline->GetPendingStepFrames() == 2);
    scene.OnUpdate(1.0f / 60.0f);
    CHECK(pipeline->GetFrameCount() == 2);
    scene.OnUpdate(1.0f / 60.0f);
    CHECK(pipeline->GetFrameCount() == 3);
    CHECK(pipeline->GetPendingStepFrames() == 0);
    scene.OnUpdate(1.0f / 60.0f);
    CHECK(pipeline->GetFrameCount() == 3);

    // Resuming runs again.
    pipeline->SetPaused(false);
    scene.OnUpdate(1.0f / 60.0f);
    CHECK(pipeline->GetFrameCount() == 4);

    // Time scale is clamped to a non-negative range: running integrators
    // backwards is not supported by anything downstream.
    pipeline->SetTimeScale(-5.0f);
    CHECK(pipeline->GetTimeScale() == 0.0f);
    pipeline->SetTimeScale(1000.0f);
    CHECK(pipeline->GetTimeScale() == 100.0f);
    pipeline->SetTimeScale(0.5f);
    CHECK(pipeline->GetTimeScale() == 0.5f);

    // Fixed delta round-trips, and 0 restores wall-clock timing.
    pipeline->SetFixedFrameDelta(1.0f / 30.0f);
    CHECK(pipeline->GetFixedFrameDelta() > 0.0f);
    pipeline->SetFixedFrameDelta(0.0f);
    CHECK(pipeline->GetFixedFrameDelta() == 0.0f);

    // A hostile delta (negative, or a multi-second stall) must not abort or hang.
    scene.OnUpdate(-1.0f);
    scene.OnUpdate(1000.0f);
    CHECK(pipeline->GetFrameCount() == 6);

    // Screen dimension changes are accepted even with no camera system present.
    pipeline->SetScreenDimensions(1920, 1080);
    CHECK(pipeline->GetConfig().ScreenWidth == 1920);
    CHECK(pipeline->GetConfig().ScreenHeight == 1080);

    // Shutdown is idempotent.
    scene.ShutdownSystems();
    scene.ShutdownSystems();
    CHECK(scene.GetSystemPipeline() == nullptr);

    // AI & Audio Pipeline Stage Integration Test
    {
        Scene aiAudioScene("AIAudioPipelineScene");
        SystemPipelineConfig config = HeadlessConfig();
        config.EnableAI = true;
        config.EnableAudio = true;
        config.EnableCameras = true;

        CHECK(aiAudioScene.InitializeSystems(config));
        SystemPipeline* p = aiAudioScene.GetSystemPipeline();
        CHECK(p != nullptr);
        CHECK(p->GetBehaviorTreeSystem() != nullptr);
        CHECK(p->GetFSMSystem() != nullptr);
        CHECK(p->GetCameraSystem() != nullptr);

        // Spawn entity with AudioListener and Camera
        auto listenerEntity = aiAudioScene.CreateEntity("AudioListener");
        auto& listener = listenerEntity.AddComponent<AudioListenerComponent>();
        listener.IsActive = true;
        auto& listenerTrans = listenerEntity.AddComponent<TransformComponent>();
        listenerTrans.Position = glm::vec3(10.0f, 2.0f, 5.0f);

        // Spawn entity with AudioSource
        auto sourceEntity = aiAudioScene.CreateEntity("AudioEmitter");
        auto& source = sourceEntity.AddComponent<AudioSourceComponent>();
        source.Volume = 0.8f;
        auto& sourceTrans = sourceEntity.AddComponent<TransformComponent>();
        sourceTrans.Position = glm::vec3(12.0f, 2.0f, 5.0f);

        // Spawn entity with AI components
        auto aiEntity = aiAudioScene.CreateEntity("AIAgent");
        auto& btComp = aiEntity.AddComponent<BehaviorTreeComponent>();
        btComp.TemplateId = "TestGuardAI";
        auto& fsmComp = aiEntity.AddComponent<FSMComponent>();
        fsmComp.TemplateId = "TestPatrolFSM";

        // Step simulation
        for (int i = 0; i < 5; ++i) {
            aiAudioScene.OnUpdate(1.0f / 60.0f);
        }
        CHECK(p->GetFrameCount() == 5);

        aiAudioScene.ShutdownSystems();
        CHECK(aiAudioScene.GetSystemPipeline() == nullptr);
    }

    return 0;
}
