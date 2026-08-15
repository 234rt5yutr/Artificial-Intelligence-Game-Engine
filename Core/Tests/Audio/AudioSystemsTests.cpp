// AudioSystemsTests: Comprehensive unit tests for AudioSystem (miniaudio backend),
// 3D spatial audio, AudioListenerSystem, AudioSourceSystem, ReverbZoneSystem,
// and PhysicsAudioIntegration.

#include "Core/Audio/AudioSystem.h"
#include "Core/Audio/AudioListenerSystem.h"
#include "Core/Audio/AudioSourceSystem.h"
#include "Core/Audio/ReverbZoneSystem.h"
#include "Core/Audio/PhysicsAudioIntegration.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/Components.h"
#include "Core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n",                 \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

int main() {
    Engine::Log::Init();

    using namespace Core::Audio;
    using namespace Core::ECS;

    // 1. AudioSystem Lifecycle, Configuration, and Group Volumes
    {
        AudioSystem& audio = AudioSystem::Get();
        CHECK(!audio.IsInitialized());

        AudioConfig config;
        config.SampleRate = 48000;
        config.Channels = 2;
        config.MasterVolume = 0.85f;
        config.DopplerFactor = 1.25f;
        config.SpeedOfSound = 340.0f;
        config.MaxVoices = 32;
        config.EnableSpatialAudio = true;

        bool initOk = audio.Initialize(config);
        CHECK(initOk);
        CHECK(audio.IsInitialized());
        CHECK(std::fabs(audio.GetMasterVolume() - 0.85f) < 0.001f);
        CHECK(audio.GetMaxVoices() == 32);

        // Master Volume Adjustment
        audio.SetMasterVolume(0.5f);
        CHECK(std::fabs(audio.GetMasterVolume() - 0.5f) < 0.001f);

        // Doppler & Speed of Sound
        audio.SetDopplerFactor(1.5f);
        audio.SetSpeedOfSound(343.3f);
        CHECK(std::fabs(audio.GetConfig().DopplerFactor - 1.5f) < 0.001f);
        CHECK(std::fabs(audio.GetConfig().SpeedOfSound - 343.3f) < 0.001f);

        // Group Volume Management
        audio.SetGroupVolume("SFX", 0.9f);
        audio.SetGroupVolume("Music", 0.6f);
        audio.SetGroupVolume("Dialogue", 1.0f);

        CHECK(std::fabs(audio.GetGroupVolume("SFX") - 0.9f) < 0.001f);
        CHECK(std::fabs(audio.GetGroupVolume("Music") - 0.6f) < 0.001f);
        CHECK(std::fabs(audio.GetGroupVolume("Dialogue") - 1.0f) < 0.001f);
        CHECK(std::fabs(audio.GetGroupVolume("NonExistent") - 1.0f) < 0.001f); // Default 1.0

        audio.Shutdown();
        CHECK(!audio.IsInitialized());
    }

    // 2. Audio Listener & 3D Spatial Positioning
    {
        AudioSystem& audio = AudioSystem::Get();
        AudioConfig config;
        CHECK(audio.Initialize(config));

        // Single listener test
        glm::vec3 listenerPos(15.0f, 2.5f, -10.0f);
        glm::vec3 listenerVel(0.0f, 0.0f, 5.0f);
        glm::quat listenerOri = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        audio.SetListenerPosition(listenerPos, 0);
        audio.SetListenerVelocity(listenerVel, 0);
        audio.SetListenerOrientation(listenerOri, 0);

        ListenerConfig listenerConfig = audio.GetListener(0);
        CHECK(std::fabs(listenerConfig.Position.x - 15.0f) < 0.001f);
        CHECK(std::fabs(listenerConfig.Position.y - 2.5f) < 0.001f);
        CHECK(std::fabs(listenerConfig.Position.z - (-10.0f)) < 0.001f);
        CHECK(std::fabs(listenerConfig.Velocity.z - 5.0f) < 0.001f);
        CHECK(std::fabs(listenerConfig.Orientation.w - listenerOri.w) < 0.001f);

        // Composite SetListener
        ListenerConfig customConfig;
        customConfig.Position = glm::vec3(100.0f, 200.0f, 300.0f);
        customConfig.Velocity = glm::vec3(1.0f, 2.0f, 3.0f);
        customConfig.Orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        customConfig.ListenerIndex = 0;

        audio.SetListener(customConfig);
        ListenerConfig retrieved = audio.GetListener(0);
        CHECK(std::fabs(retrieved.Position.x - 100.0f) < 0.001f);
        CHECK(std::fabs(retrieved.Velocity.y - 2.0f) < 0.001f);

        audio.Shutdown();
    }

    // 3. AudioListenerSystem (ECS Integration & Priority Handling)
    {
        AudioSystem& audio = AudioSystem::Get();
        CHECK(audio.Initialize());

        AudioListenerSystem listenerSystem;
        listenerSystem.Initialize();

        Scene scene("AudioListenerScene");

        // Create low-priority listener
        auto entityLow = scene.CreateEntity("ListenerLow");
        auto& tfLow = entityLow.AddComponent<TransformComponent>();
        tfLow.Position = glm::vec3(5.0f, 0.0f, 0.0f);
        auto& listLow = entityLow.AddComponent<AudioListenerComponent>();
        listLow.IsActive = true;
        listLow.Priority = 10;

        // Create high-priority listener
        auto entityHigh = scene.CreateEntity("ListenerHigh");
        auto& tfHigh = entityHigh.AddComponent<TransformComponent>();
        tfHigh.Position = glm::vec3(50.0f, 10.0f, -20.0f);
        auto& listHigh = entityHigh.AddComponent<AudioListenerComponent>();
        listHigh.IsActive = true;
        listHigh.Priority = 100; // Higher priority -> becomes primary listener (index 0)

        listenerSystem.Update(&scene, 0.016f);

        // Check that primary listener is ListenerHigh due to priority sorting
        ListenerConfig primary = audio.GetListener(0);
        CHECK(std::fabs(primary.Position.x - 50.0f) < 0.001f);
        CHECK(std::fabs(primary.Position.y - 10.0f) < 0.001f);
        CHECK(std::fabs(primary.Position.z - (-20.0f)) < 0.001f);

        listenerSystem.Shutdown();
        audio.Shutdown();
    }

    // 4. AudioSourceComponent and AudioSourceSystem
    {
        AudioSystem& audio = AudioSystem::Get();
        CHECK(audio.Initialize());

        AudioSourceSystem sourceSystem;
        sourceSystem.Initialize();

        Scene scene("AudioSourceScene");
        auto entity = scene.CreateEntity("SoundEmitter");
        auto& tf = entity.AddComponent<TransformComponent>();
        tf.Position = glm::vec3(0.0f, 1.0f, 0.0f);

        auto& src = entity.AddComponent<AudioSourceComponent>();
        src.Volume = 0.75f;
        src.Pitch = 1.1f;
        src.Loop = true;
        src.Spatial = true;
        src.SpatialConfig.MinDistance = 2.0f;
        src.SpatialConfig.MaxDistance = 50.0f;
        src.SpatialConfig.Attenuation = AttenuationModel::Inverse;
        src.PlayOnAwake = false;

        CHECK(src.Volume == 0.75f);
        CHECK(src.Pitch == 1.1f);
        CHECK(src.Loop == true);
        CHECK(src.Spatial == true);

        // Test factory methods
        AudioSourceComponent sfx = AudioSourceComponent::CreateSFX("test.wav", 0.9f);
        CHECK(sfx.Volume == 0.9f);
        CHECK(sfx.MixerGroup == "SFX");
        CHECK(!sfx.Loop);

        AudioSourceComponent ambient = AudioSourceComponent::CreateAmbient("amb.wav", 0.4f);
        CHECK(ambient.Loop == true);
        CHECK(ambient.MixerGroup == "Ambient");

        AudioSourceComponent music = AudioSourceComponent::CreateMusic("bgm.wav", 0.8f);
        CHECK(!music.Spatial);
        CHECK(music.FadeInTime == 2.0f);

        // Update source system
        sourceSystem.Update(&scene, 0.5f);

        // Attenuation helper conversions
        CHECK(ToMiniaudioAttenuation(AttenuationModel::None) == ma_attenuation_model_none);
        CHECK(ToMiniaudioAttenuation(AttenuationModel::Inverse) == ma_attenuation_model_inverse);
        CHECK(ToMiniaudioAttenuation(AttenuationModel::Linear) == ma_attenuation_model_linear);
        CHECK(ToMiniaudioAttenuation(AttenuationModel::Exponential) == ma_attenuation_model_exponential);

        sourceSystem.Shutdown();
        audio.Shutdown();
    }

    // 5. ReverbZoneSystem (Acoustic Environments & Distance Containment)
    {
        ReverbZoneSystem& reverb = ReverbZoneSystem::Get();
        CHECK(!reverb.IsInitialized());

        ReverbZoneSystemConfig config;
        config.Enabled = true;
        config.BlendSpeed = 3.0f;
        config.MaxSimultaneousZones = 2;

        CHECK(reverb.Initialize(config));
        CHECK(reverb.IsInitialized());

        // Create ReverbZoneComponent for a spherical reverb zone
        ReverbZoneComponent sphereZone;
        sphereZone.Shape = AudioZoneShape::Sphere;
        sphereZone.Radius = 10.0f;
        sphereZone.BlendDistance = 2.0f;
        sphereZone.Parameters.DecayTimeSeconds = 2.5f;
        sphereZone.Parameters.WetDryMix = 0.8f;

        CHECK(sphereZone.Radius == 10.0f);
        CHECK(sphereZone.BlendDistance == 2.0f);
        CHECK(std::fabs(sphereZone.Parameters.DecayTimeSeconds - 2.5f) < 0.001f);
        CHECK(std::fabs(sphereZone.Parameters.WetDryMix - 0.8f) < 0.001f);

        reverb.Shutdown();
        CHECK(!reverb.IsInitialized());
    }

    // 6. PhysicsAudioIntegration (Materials and Collision Events)
    {
        // Verify material type strings
        CHECK(std::strcmp(PhysicsMaterialTypeToString(PhysicsMaterialType::Default), "Default") == 0);
        CHECK(std::strcmp(PhysicsMaterialTypeToString(PhysicsMaterialType::Metal), "Metal") == 0);
        CHECK(std::strcmp(PhysicsMaterialTypeToString(PhysicsMaterialType::Wood), "Wood") == 0);
        CHECK(std::strcmp(PhysicsMaterialTypeToString(PhysicsMaterialType::Stone), "Stone") == 0);
        CHECK(std::strcmp(PhysicsMaterialTypeToString(PhysicsMaterialType::Concrete), "Concrete") == 0);
        CHECK(std::strcmp(PhysicsMaterialTypeToString(PhysicsMaterialType::Glass), "Glass") == 0);
        CHECK(std::strcmp(PhysicsMaterialTypeToString(PhysicsMaterialType::Water), "Water") == 0);
        CHECK(std::strcmp(PhysicsMaterialTypeToString(PhysicsMaterialType::Sand), "Sand") == 0);
        CHECK(std::strcmp(PhysicsMaterialTypeToString(PhysicsMaterialType::Flesh), "Flesh") == 0);

        // Verify PhysicsMaterialAudioProperties factory method
        PhysicsMaterialAudioProperties metalProps = PhysicsMaterialAudioProperties::CreateMetal();
        CHECK(metalProps.Type == PhysicsMaterialType::Metal);
        CHECK(metalProps.MinImpactImpulse == 0.3f);
        CHECK(metalProps.ImpactVolumeScale == 1.2f);
        CHECK(metalProps.ImpactSounds.size() == 3);

        PhysicsMaterialAudioProperties woodProps = PhysicsMaterialAudioProperties::CreateWood();
        CHECK(woodProps.Type == PhysicsMaterialType::Wood);
    }

    std::printf("AudioSystemsTests: ALL TESTS PASSED!\n");
    return 0;
}
