#pragma once

// Physics Audio Integration
// Triggers sounds dynamically based on collision impulses and material types
// Integrates the Audio System with Jolt Physics

#include "Core/Audio/AudioSystem.h"
#include "Core/Log.h"
#include "Core/Profile.h"

#include <glm/glm.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <queue>
#include <memory>
#include <cstdint>

namespace Core {
namespace Audio {

    // ============================================================================
    // Physics Material Types
    // ============================================================================
    // Defines different surface materials for physics-based audio

    enum class PhysicsMaterialType : uint8_t {
        Default = 0,
        Metal,
        Wood,
        Stone,
        Concrete,
        Glass,
        Plastic,
        Rubber,
        Cloth,
        Flesh,
        Water,
        Sand,
        Gravel,
        Grass,
        Dirt,
        Ice,
        Custom
    };

    // Convert material type to string for debugging
    inline const char* PhysicsMaterialTypeToString(PhysicsMaterialType type) {
        switch (type) {
            case PhysicsMaterialType::Default:  return "Default";
            case PhysicsMaterialType::Metal:    return "Metal";
            case PhysicsMaterialType::Wood:     return "Wood";
            case PhysicsMaterialType::Stone:    return "Stone";
            case PhysicsMaterialType::Concrete: return "Concrete";
            case PhysicsMaterialType::Glass:    return "Glass";
            case PhysicsMaterialType::Plastic:  return "Plastic";
            case PhysicsMaterialType::Rubber:   return "Rubber";
            case PhysicsMaterialType::Cloth:    return "Cloth";
            case PhysicsMaterialType::Flesh:    return "Flesh";
            case PhysicsMaterialType::Water:    return "Water";
            case PhysicsMaterialType::Sand:     return "Sand";
            case PhysicsMaterialType::Gravel:   return "Gravel";
            case PhysicsMaterialType::Grass:    return "Grass";
            case PhysicsMaterialType::Dirt:     return "Dirt";
            case PhysicsMaterialType::Ice:      return "Ice";
            case PhysicsMaterialType::Custom:   return "Custom";
            default:                            return "Unknown";
        }
    }

    // ============================================================================
    // Collision Audio Event Types
    // ============================================================================

    enum class CollisionEventType : uint8_t {
        Impact,         // Initial collision
        Slide,          // Continuous sliding contact
        Roll,           // Rolling contact
        Scrape          // High-friction sliding
    };

    // ============================================================================
    // Physics Material Audio Properties
    // ============================================================================
    // Defines audio behavior for a physics material

    struct PhysicsMaterialAudioProperties {
        PhysicsMaterialType Type = PhysicsMaterialType::Default;
        std::string Name = "Default";

        // Sound file paths (can have multiple for variation)
        std::vector<std::string> ImpactSounds;
        std::vector<std::string> SlideSounds;
        std::vector<std::string> RollSounds;
        std::vector<std::string> ScrapeSounds;

        // Volume scaling based on impact intensity
        float ImpactVolumeScale = 1.0f;
        float SlideVolumeScale = 0.5f;
        float RollVolumeScale = 0.3f;
        float ScrapeVolumeScale = 0.7f;

        // Pitch variation range (random ± this value)
        float PitchVariation = 0.1f;

        // Minimum impulse to trigger sounds
        float MinImpactImpulse = 0.5f;
        float MinSlideSpeed = 0.2f;
        float MinRollSpeed = 0.3f;

        // Cooldown between repeated sounds (seconds)
        float ImpactCooldown = 0.05f;
        float SlideCooldown = 0.1f;
        float RollCooldown = 0.1f;

        // Factory methods for common materials
        static PhysicsMaterialAudioProperties CreateMetal() {
            PhysicsMaterialAudioProperties props;
            props.Type = PhysicsMaterialType::Metal;
            props.Name = "Metal";
            props.ImpactSounds = { "audio/impacts/metal_impact_01.wav", 
                                   "audio/impacts/metal_impact_02.wav",
                                   "audio/impacts/metal_impact_03.wav" };
            props.SlideSounds = { "audio/slides/metal_slide.wav" };
            props.ScrapeSounds = { "audio/scrapes/metal_scrape.wav" };
            props.ImpactVolumeScale = 1.2f;
            props.MinImpactImpulse = 0.3f;
            return props;
        }

        static PhysicsMaterialAudioProperties CreateWood() {
            PhysicsMaterialAudioProperties props;
            props.Type = PhysicsMaterialType::Wood;
            props.Name = "Wood";
            props.ImpactSounds = { "audio/impacts/wood_impact_01.wav",
                                   "audio/impacts/wood_impact_02.wav" };
            props.SlideSounds = { "audio/slides/wood_slide.wav" };
            props.RollSounds = { "audio/rolls/wood_roll.wav" };
            props.ImpactVolumeScale = 0.9f;
            props.MinImpactImpulse = 0.4f;
            return props;
        }

        static PhysicsMaterialAudioProperties CreateStone() {
            PhysicsMaterialAudioProperties props;
            props.Type = PhysicsMaterialType::Stone;
            props.Name = "Stone";
            props.ImpactSounds = { "audio/impacts/stone_impact_01.wav",
                                   "audio/impacts/stone_impact_02.wav" };
            props.SlideSounds = { "audio/slides/stone_slide.wav" };
            props.ScrapeSounds = { "audio/scrapes/stone_scrape.wav" };
            props.ImpactVolumeScale = 1.0f;
            props.MinImpactImpulse = 0.5f;
            return props;
        }

        static PhysicsMaterialAudioProperties CreateGlass() {
            PhysicsMaterialAudioProperties props;
            props.Type = PhysicsMaterialType::Glass;
            props.Name = "Glass";
            props.ImpactSounds = { "audio/impacts/glass_impact_01.wav",
                                   "audio/impacts/glass_impact_02.wav" };
            props.SlideSounds = { "audio/slides/glass_slide.wav" };
            props.ImpactVolumeScale = 1.3f;
            props.PitchVariation = 0.15f;
            props.MinImpactImpulse = 0.2f;
            return props;
        }

        static PhysicsMaterialAudioProperties CreateRubber() {
            PhysicsMaterialAudioProperties props;
            props.Type = PhysicsMaterialType::Rubber;
            props.Name = "Rubber";
            props.ImpactSounds = { "audio/impacts/rubber_impact.wav" };
            props.SlideSounds = { "audio/slides/rubber_squeak.wav" };
            props.ImpactVolumeScale = 0.6f;
            props.MinImpactImpulse = 0.6f;
            return props;
        }

        static PhysicsMaterialAudioProperties CreateConcrete() {
            PhysicsMaterialAudioProperties props;
            props.Type = PhysicsMaterialType::Concrete;
            props.Name = "Concrete";
            props.ImpactSounds = { "audio/impacts/concrete_impact_01.wav",
                                   "audio/impacts/concrete_impact_02.wav" };
            props.SlideSounds = { "audio/slides/concrete_slide.wav" };
            props.ScrapeSounds = { "audio/scrapes/concrete_scrape.wav" };
            props.ImpactVolumeScale = 1.0f;
            props.MinImpactImpulse = 0.5f;
            return props;
        }
    };

    // ============================================================================
    // Collision Audio Event
    // ============================================================================
    // Represents a pending collision audio event to be processed

    struct CollisionAudioEvent {
        CollisionEventType EventType = CollisionEventType::Impact;
        glm::vec3 Position{0.0f};
        glm::vec3 Normal{0.0f, 1.0f, 0.0f};
        glm::vec3 RelativeVelocity{0.0f};
        float Impulse = 0.0f;
        float ContactSpeed = 0.0f;
        PhysicsMaterialType Material1 = PhysicsMaterialType::Default;
        PhysicsMaterialType Material2 = PhysicsMaterialType::Default;
        JPH::BodyID Body1;
        JPH::BodyID Body2;
        uint64_t Entity1 = 0;
        uint64_t Entity2 = 0;
        float Timestamp = 0.0f;
    };

    // ============================================================================
    // Collision Audio Config
    // ============================================================================

    struct CollisionAudioConfig {
        // Global enable/disable
        bool Enabled = true;

        // Maximum simultaneous collision sounds
        uint32_t MaxSimultaneousSounds = 16;

        // Global volume scale for all collision sounds
        float GlobalVolumeScale = 1.0f;

        // Impulse thresholds
        float MinImpulseThreshold = 0.3f;   // Below this, no sound
        float MaxImpulseForVolume = 50.0f;  // Impulse that gives max volume

        // Distance culling
        float MaxAudibleDistance = 50.0f;

        // Per-body cooldown (seconds)
        float BodyCooldown = 0.02f;

        // Pitch variation range
        float BasePitchVariation = 0.1f;

        // Velocity threshold for slide/roll detection
        float SlideVelocityThreshold = 0.5f;
        float RollVelocityThreshold = 0.3f;

        // Queue size limit
        uint32_t MaxQueuedEvents = 64;
    };

    // ============================================================================
    // Physics Audio Manager
    // ============================================================================
    // Main class that manages physics-to-audio integration

    class PhysicsAudioManager {
    public:
        static PhysicsAudioManager& Get();

        // Delete copy/move
        PhysicsAudioManager(const PhysicsAudioManager&) = delete;
        PhysicsAudioManager& operator=(const PhysicsAudioManager&) = delete;

        // Lifecycle
        bool Initialize(const CollisionAudioConfig& config = CollisionAudioConfig{});
        void Shutdown();
        bool IsInitialized() const { return m_Initialized; }

        // Update - processes queued collision events
        void Update(float deltaTime);

        // Configuration
        void SetConfig(const CollisionAudioConfig& config) { m_Config = config; }
        const CollisionAudioConfig& GetConfig() const { return m_Config; }

        // Material registration
        void RegisterMaterial(const PhysicsMaterialAudioProperties& properties);
        void UnregisterMaterial(PhysicsMaterialType type);
        const PhysicsMaterialAudioProperties* GetMaterial(PhysicsMaterialType type) const;
        void RegisterDefaultMaterials();

        // Body-to-material mapping
        void SetBodyMaterial(JPH::BodyID bodyId, PhysicsMaterialType material);
        void SetEntityMaterial(uint64_t entityId, PhysicsMaterialType material);
        PhysicsMaterialType GetBodyMaterial(JPH::BodyID bodyId) const;
        PhysicsMaterialType GetEntityMaterial(uint64_t entityId) const;
        void ClearBodyMaterial(JPH::BodyID bodyId);
        void ClearEntityMaterial(uint64_t entityId);

        // Event submission (called from physics contact listener)
        void OnCollisionContact(const CollisionAudioEvent& event);
        void OnCollisionImpact(
            const glm::vec3& position,
            const glm::vec3& normal,
            float impulse,
            JPH::BodyID body1, JPH::BodyID body2,
            uint64_t entity1 = 0, uint64_t entity2 = 0
        );
        void OnCollisionSlide(
            const glm::vec3& position,
            const glm::vec3& velocity,
            float contactSpeed,
            JPH::BodyID body1, JPH::BodyID body2,
            uint64_t entity1 = 0, uint64_t entity2 = 0
        );

        // Statistics
        uint32_t GetActiveCollisionSounds() const;
        uint32_t GetQueuedEventCount() const;

        // Listener position (for distance culling)
        void SetListenerPosition(const glm::vec3& position) { m_ListenerPosition = position; }

        // Callbacks for custom audio behavior
        using CollisionSoundCallback = std::function<void(const CollisionAudioEvent&, SoundHandle)>;
        void SetCollisionSoundCallback(CollisionSoundCallback callback) { m_SoundCallback = callback; }

    private:
        PhysicsAudioManager() = default;
        ~PhysicsAudioManager();

        // Internal helpers
        void ProcessEvent(const CollisionAudioEvent& event);
        float CalculateVolume(float impulse, const PhysicsMaterialAudioProperties& material) const;
        float CalculatePitch(const PhysicsMaterialAudioProperties& material) const;
        std::string SelectSound(const std::vector<std::string>& sounds) const;
        bool CheckCooldown(JPH::BodyID bodyId, CollisionEventType eventType);
        void UpdateCooldowns(float deltaTime);
        PhysicsMaterialAudioProperties GetCombinedMaterial(
            PhysicsMaterialType mat1, PhysicsMaterialType mat2) const;

    private:
        CollisionAudioConfig m_Config;
        bool m_Initialized = false;

        // Material database
        mutable std::mutex m_MaterialsMutex;
        std::unordered_map<PhysicsMaterialType, PhysicsMaterialAudioProperties> m_Materials;

        // Body/Entity to material mapping
        mutable std::mutex m_MappingMutex;
        std::unordered_map<uint32_t, PhysicsMaterialType> m_BodyMaterials;  // BodyID index -> material
        std::unordered_map<uint64_t, PhysicsMaterialType> m_EntityMaterials;

        // Event queue
        mutable std::mutex m_EventQueueMutex;
        std::queue<CollisionAudioEvent> m_EventQueue;

        // Cooldown tracking
        struct CooldownEntry {
            float ImpactCooldown = 0.0f;
            float SlideCooldown = 0.0f;
            float RollCooldown = 0.0f;
        };
        mutable std::mutex m_CooldownMutex;
        std::unordered_map<uint32_t, CooldownEntry> m_BodyCooldowns;

        // Active sounds tracking
        std::vector<SoundHandle> m_ActiveCollisionSounds;

        // Listener position for distance culling
        glm::vec3 m_ListenerPosition{0.0f};

        // Random number generator state
        mutable uint32_t m_RandomState = 12345;

        // Callback
        CollisionSoundCallback m_SoundCallback;

        // Current time accumulator
        float m_CurrentTime = 0.0f;
    };

    // ============================================================================
    // Audio Contact Listener
    // ============================================================================
    // Jolt physics contact listener that feeds collision events to PhysicsAudioManager

    class AudioContactListener : public JPH::ContactListener {
    public:
        AudioContactListener() = default;
        ~AudioContactListener() override = default;

        // Set the physics audio manager to send events to
        void SetPhysicsAudioManager(PhysicsAudioManager* manager) { m_AudioManager = manager; }

        // JPH::ContactListener interface
        JPH::ValidateResult OnContactValidate(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            JPH::RVec3Arg inBaseOffset,
            const JPH::CollideShapeResult& inCollisionResult) override;

        void OnContactAdded(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            const JPH::ContactManifold& inManifold,
            JPH::ContactSettings& ioSettings) override;

        void OnContactPersisted(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            const JPH::ContactManifold& inManifold,
            JPH::ContactSettings& ioSettings) override;

        void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

        // Enable/disable audio processing
        void SetEnabled(bool enabled) { m_Enabled = enabled; }
        bool IsEnabled() const { return m_Enabled; }

        // Set impulse calculation method
        void SetUseLinearVelocityForImpulse(bool use) { m_UseLinearVelocity = use; }

    private:
        PhysicsAudioManager* m_AudioManager = nullptr;
        bool m_Enabled = true;
        bool m_UseLinearVelocity = true;

        // Track persistent contacts for slide/roll detection
        struct PersistentContact {
            JPH::BodyID Body1;
            JPH::BodyID Body2;
            glm::vec3 LastPosition;
            float ContactDuration = 0.0f;
        };
        std::unordered_map<uint64_t, PersistentContact> m_PersistentContacts;

        uint64_t MakeContactKey(JPH::BodyID b1, JPH::BodyID b2) const {
            uint32_t id1 = b1.GetIndex();
            uint32_t id2 = b2.GetIndex();
            if (id1 > id2) std::swap(id1, id2);
            return (static_cast<uint64_t>(id1) << 32) | static_cast<uint64_t>(id2);
        }
    };

    // ============================================================================
    // Physics Audio Bridge
    // ============================================================================
    // Utility class to connect PhysicsWorld with PhysicsAudioManager

    class PhysicsAudioBridge {
    public:
        // Initialize the bridge - creates AudioContactListener and connects to physics
        static bool Initialize();

        // Shutdown the bridge
        static void Shutdown();

        // Get the audio contact listener
        static AudioContactListener* GetContactListener();

        // Enable/disable physics audio
        static void SetEnabled(bool enabled);
        static bool IsEnabled();

    private:
        static std::unique_ptr<AudioContactListener> s_ContactListener;
        static bool s_Initialized;
    };

    // ============================================================================
    // Convenience Functions
    // ============================================================================

    // Initialize physics-audio integration with default settings
    inline bool InitializePhysicsAudio() {
        if (!PhysicsAudioManager::Get().Initialize()) {
            return false;
        }
        PhysicsAudioManager::Get().RegisterDefaultMaterials();
        return PhysicsAudioBridge::Initialize();
    }

    // Shutdown physics-audio integration
    inline void ShutdownPhysicsAudio() {
        PhysicsAudioBridge::Shutdown();
        PhysicsAudioManager::Get().Shutdown();
    }

    // Quick material assignment
    inline void SetCollisionMaterial(uint64_t entityId, PhysicsMaterialType material) {
        PhysicsAudioManager::Get().SetEntityMaterial(entityId, material);
    }

    // Get the physics audio manager
    inline PhysicsAudioManager& GetPhysicsAudioManager() {
        return PhysicsAudioManager::Get();
    }

} // namespace Audio
} // namespace Core
