#include "Core/Audio/PhysicsAudioIntegration.h"
#include <cmath>
#include <algorithm>
#include <random>

namespace Core {
namespace Audio {

    // ============================================================================
    // PhysicsAudioManager Implementation
    // ============================================================================

    PhysicsAudioManager& PhysicsAudioManager::Get() {
        static PhysicsAudioManager instance;
        return instance;
    }

    PhysicsAudioManager::~PhysicsAudioManager() {
        if (m_Initialized) {
            Shutdown();
        }
    }

    bool PhysicsAudioManager::Initialize(const CollisionAudioConfig& config) {
        PROFILE_FUNCTION();

        if (m_Initialized) {
            ENGINE_CORE_WARN("PhysicsAudioManager already initialized");
            return true;
        }

        m_Config = config;

        // Ensure audio system is available
        if (!AudioSystem::Get().IsInitialized()) {
            ENGINE_CORE_ERROR("PhysicsAudioManager: AudioSystem must be initialized first");
            return false;
        }

        // Initialize random state with time-based seed
        m_RandomState = static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFF
        );

        m_Initialized = true;
        ENGINE_CORE_INFO("PhysicsAudioManager initialized (MaxSounds: {}, MaxDistance: {})",
                         config.MaxSimultaneousSounds, config.MaxAudibleDistance);

        return true;
    }

    void PhysicsAudioManager::Shutdown() {
        PROFILE_FUNCTION();

        if (!m_Initialized) {
            return;
        }

        ENGINE_CORE_INFO("Shutting down PhysicsAudioManager...");

        // Stop all active collision sounds
        for (auto handle : m_ActiveCollisionSounds) {
            AudioSystem::Get().StopSound(handle);
        }
        m_ActiveCollisionSounds.clear();

        // Clear all data
        {
            std::lock_guard<std::mutex> lock(m_EventQueueMutex);
            while (!m_EventQueue.empty()) {
                m_EventQueue.pop();
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_MaterialsMutex);
            m_Materials.clear();
        }

        {
            std::lock_guard<std::mutex> lock(m_MappingMutex);
            m_BodyMaterials.clear();
            m_EntityMaterials.clear();
        }

        {
            std::lock_guard<std::mutex> lock(m_CooldownMutex);
            m_BodyCooldowns.clear();
        }

        m_Initialized = false;
        ENGINE_CORE_INFO("PhysicsAudioManager shutdown complete");
    }

    void PhysicsAudioManager::Update(float deltaTime) {
        PROFILE_FUNCTION();

        if (!m_Initialized || !m_Config.Enabled) {
            return;
        }

        m_CurrentTime += deltaTime;

        // Update cooldowns
        UpdateCooldowns(deltaTime);

        // Clean up finished sounds
        m_ActiveCollisionSounds.erase(
            std::remove_if(m_ActiveCollisionSounds.begin(), m_ActiveCollisionSounds.end(),
                [](SoundHandle handle) {
                    return !AudioSystem::Get().IsSoundPlaying(handle);
                }),
            m_ActiveCollisionSounds.end()
        );

        // Process queued events
        std::queue<CollisionAudioEvent> eventsToProcess;
        {
            std::lock_guard<std::mutex> lock(m_EventQueueMutex);
            std::swap(eventsToProcess, m_EventQueue);
        }

        while (!eventsToProcess.empty()) {
            const auto& event = eventsToProcess.front();
            ProcessEvent(event);
            eventsToProcess.pop();
        }
    }

    void PhysicsAudioManager::RegisterMaterial(const PhysicsMaterialAudioProperties& properties) {
        std::lock_guard<std::mutex> lock(m_MaterialsMutex);
        m_Materials[properties.Type] = properties;
        ENGINE_CORE_TRACE("Registered physics audio material: {}", properties.Name);
    }

    void PhysicsAudioManager::UnregisterMaterial(PhysicsMaterialType type) {
        std::lock_guard<std::mutex> lock(m_MaterialsMutex);
        m_Materials.erase(type);
    }

    const PhysicsMaterialAudioProperties* PhysicsAudioManager::GetMaterial(PhysicsMaterialType type) const {
        std::lock_guard<std::mutex> lock(m_MaterialsMutex);
        auto it = m_Materials.find(type);
        return (it != m_Materials.end()) ? &it->second : nullptr;
    }

    void PhysicsAudioManager::RegisterDefaultMaterials() {
        RegisterMaterial(PhysicsMaterialAudioProperties::CreateMetal());
        RegisterMaterial(PhysicsMaterialAudioProperties::CreateWood());
        RegisterMaterial(PhysicsMaterialAudioProperties::CreateStone());
        RegisterMaterial(PhysicsMaterialAudioProperties::CreateGlass());
        RegisterMaterial(PhysicsMaterialAudioProperties::CreateRubber());
        RegisterMaterial(PhysicsMaterialAudioProperties::CreateConcrete());

        // Register default material
        PhysicsMaterialAudioProperties defaultMat;
        defaultMat.Type = PhysicsMaterialType::Default;
        defaultMat.Name = "Default";
        defaultMat.ImpactSounds = { "audio/impacts/generic_impact.wav" };
        RegisterMaterial(defaultMat);

        ENGINE_CORE_INFO("Registered {} default physics audio materials", m_Materials.size());
    }

    void PhysicsAudioManager::SetBodyMaterial(JPH::BodyID bodyId, PhysicsMaterialType material) {
        std::lock_guard<std::mutex> lock(m_MappingMutex);
        m_BodyMaterials[bodyId.GetIndex()] = material;
    }

    void PhysicsAudioManager::SetEntityMaterial(uint64_t entityId, PhysicsMaterialType material) {
        std::lock_guard<std::mutex> lock(m_MappingMutex);
        m_EntityMaterials[entityId] = material;
    }

    PhysicsMaterialType PhysicsAudioManager::GetBodyMaterial(JPH::BodyID bodyId) const {
        std::lock_guard<std::mutex> lock(m_MappingMutex);
        auto it = m_BodyMaterials.find(bodyId.GetIndex());
        return (it != m_BodyMaterials.end()) ? it->second : PhysicsMaterialType::Default;
    }

    PhysicsMaterialType PhysicsAudioManager::GetEntityMaterial(uint64_t entityId) const {
        std::lock_guard<std::mutex> lock(m_MappingMutex);
        auto it = m_EntityMaterials.find(entityId);
        return (it != m_EntityMaterials.end()) ? it->second : PhysicsMaterialType::Default;
    }

    void PhysicsAudioManager::ClearBodyMaterial(JPH::BodyID bodyId) {
        std::lock_guard<std::mutex> lock(m_MappingMutex);
        m_BodyMaterials.erase(bodyId.GetIndex());
    }

    void PhysicsAudioManager::ClearEntityMaterial(uint64_t entityId) {
        std::lock_guard<std::mutex> lock(m_MappingMutex);
        m_EntityMaterials.erase(entityId);
    }

    void PhysicsAudioManager::OnCollisionContact(const CollisionAudioEvent& event) {
        if (!m_Initialized || !m_Config.Enabled) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_EventQueueMutex);
        if (m_EventQueue.size() < m_Config.MaxQueuedEvents) {
            m_EventQueue.push(event);
        }
    }

    void PhysicsAudioManager::OnCollisionImpact(
        const glm::vec3& position,
        const glm::vec3& normal,
        float impulse,
        JPH::BodyID body1, JPH::BodyID body2,
        uint64_t entity1, uint64_t entity2)
    {
        CollisionAudioEvent event;
        event.EventType = CollisionEventType::Impact;
        event.Position = position;
        event.Normal = normal;
        event.Impulse = impulse;
        event.Body1 = body1;
        event.Body2 = body2;
        event.Entity1 = entity1;
        event.Entity2 = entity2;
        event.Material1 = GetBodyMaterial(body1);
        event.Material2 = GetBodyMaterial(body2);
        event.Timestamp = m_CurrentTime;

        OnCollisionContact(event);
    }

    void PhysicsAudioManager::OnCollisionSlide(
        const glm::vec3& position,
        const glm::vec3& velocity,
        float contactSpeed,
        JPH::BodyID body1, JPH::BodyID body2,
        uint64_t entity1, uint64_t entity2)
    {
        CollisionAudioEvent event;
        event.EventType = CollisionEventType::Slide;
        event.Position = position;
        event.RelativeVelocity = velocity;
        event.ContactSpeed = contactSpeed;
        event.Body1 = body1;
        event.Body2 = body2;
        event.Entity1 = entity1;
        event.Entity2 = entity2;
        event.Material1 = GetBodyMaterial(body1);
        event.Material2 = GetBodyMaterial(body2);
        event.Timestamp = m_CurrentTime;

        OnCollisionContact(event);
    }

    uint32_t PhysicsAudioManager::GetActiveCollisionSounds() const {
        return static_cast<uint32_t>(m_ActiveCollisionSounds.size());
    }

    uint32_t PhysicsAudioManager::GetQueuedEventCount() const {
        std::lock_guard<std::mutex> lock(m_EventQueueMutex);
        return static_cast<uint32_t>(m_EventQueue.size());
    }

    void PhysicsAudioManager::ProcessEvent(const CollisionAudioEvent& event) {
        PROFILE_FUNCTION();

        // Distance culling
        float distance = glm::length(event.Position - m_ListenerPosition);
        if (distance > m_Config.MaxAudibleDistance) {
            return;
        }

        // Check if we have room for more sounds
        if (m_ActiveCollisionSounds.size() >= m_Config.MaxSimultaneousSounds) {
            return;
        }

        // Get material properties
        auto combinedMaterial = GetCombinedMaterial(event.Material1, event.Material2);

        // Check cooldown
        if (!CheckCooldown(event.Body1, event.EventType)) {
            return;
        }

        // Select sound based on event type
        std::vector<std::string> const* soundList = nullptr;
        float volumeScale = 1.0f;

        switch (event.EventType) {
            case CollisionEventType::Impact:
                if (event.Impulse < combinedMaterial.MinImpactImpulse) {
                    return;
                }
                soundList = &combinedMaterial.ImpactSounds;
                volumeScale = combinedMaterial.ImpactVolumeScale;
                break;

            case CollisionEventType::Slide:
                if (event.ContactSpeed < combinedMaterial.MinSlideSpeed) {
                    return;
                }
                soundList = &combinedMaterial.SlideSounds;
                volumeScale = combinedMaterial.SlideVolumeScale;
                break;

            case CollisionEventType::Roll:
                if (event.ContactSpeed < combinedMaterial.MinRollSpeed) {
                    return;
                }
                soundList = &combinedMaterial.RollSounds;
                volumeScale = combinedMaterial.RollVolumeScale;
                break;

            case CollisionEventType::Scrape:
                soundList = &combinedMaterial.ScrapeSounds;
                volumeScale = combinedMaterial.ScrapeVolumeScale;
                break;
        }

        if (!soundList || soundList->empty()) {
            // Fall back to impact sounds
            soundList = &combinedMaterial.ImpactSounds;
            volumeScale = combinedMaterial.ImpactVolumeScale;
        }

        if (soundList->empty()) {
            return;
        }

        // Select random sound from list
        std::string soundPath = SelectSound(*soundList);
        if (soundPath.empty()) {
            return;
        }

        // Calculate volume based on impulse
        float volume = CalculateVolume(event.Impulse, combinedMaterial) * volumeScale * m_Config.GlobalVolumeScale;

        // Apply distance attenuation manually for volume calculation
        float distanceFactor = 1.0f - (distance / m_Config.MaxAudibleDistance);
        distanceFactor = std::max(0.0f, distanceFactor);
        volume *= distanceFactor;

        if (volume < 0.01f) {
            return;
        }

        // Calculate pitch with variation
        float pitch = CalculatePitch(combinedMaterial);

        // Play the sound
        SoundHandle handle = AudioSystem::Get().PlaySoundOneShotAt(soundPath, event.Position, volume);

        if (handle != InvalidSoundHandle) {
            AudioSystem::Get().SetSoundPitch(handle, pitch);
            m_ActiveCollisionSounds.push_back(handle);

            // Notify callback if registered
            if (m_SoundCallback) {
                m_SoundCallback(event, handle);
            }

            ENGINE_CORE_TRACE("Collision sound: {} at ({:.1f}, {:.1f}, {:.1f}) vol={:.2f} pitch={:.2f}",
                             soundPath, event.Position.x, event.Position.y, event.Position.z,
                             volume, pitch);
        }
    }

    float PhysicsAudioManager::CalculateVolume(float impulse, const PhysicsMaterialAudioProperties& material) const {
        // Normalize impulse to 0-1 range
        float normalizedImpulse = (impulse - material.MinImpactImpulse) / 
                                   (m_Config.MaxImpulseForVolume - material.MinImpactImpulse);
        normalizedImpulse = std::clamp(normalizedImpulse, 0.0f, 1.0f);

        // Apply logarithmic scaling for more natural feel
        float volume = std::sqrt(normalizedImpulse);
        return volume;
    }

    float PhysicsAudioManager::CalculatePitch(const PhysicsMaterialAudioProperties& material) const {
        // Generate random pitch variation
        // Simple xorshift random
        m_RandomState ^= m_RandomState << 13;
        m_RandomState ^= m_RandomState >> 17;
        m_RandomState ^= m_RandomState << 5;

        float random = static_cast<float>(m_RandomState) / static_cast<float>(0xFFFFFFFF);
        float variation = (random * 2.0f - 1.0f) * material.PitchVariation;

        return 1.0f + variation;
    }

    std::string PhysicsAudioManager::SelectSound(const std::vector<std::string>& sounds) const {
        if (sounds.empty()) {
            return "";
        }

        if (sounds.size() == 1) {
            return sounds[0];
        }

        // Random selection using xorshift
        m_RandomState ^= m_RandomState << 13;
        m_RandomState ^= m_RandomState >> 17;
        m_RandomState ^= m_RandomState << 5;

        size_t index = m_RandomState % sounds.size();
        return sounds[index];
    }

    bool PhysicsAudioManager::CheckCooldown(JPH::BodyID bodyId, CollisionEventType eventType) {
        std::lock_guard<std::mutex> lock(m_CooldownMutex);

        auto it = m_BodyCooldowns.find(bodyId.GetIndex());
        if (it == m_BodyCooldowns.end()) {
            // No cooldown entry, create one and allow sound
            CooldownEntry entry;
            switch (eventType) {
                case CollisionEventType::Impact:
                    entry.ImpactCooldown = m_Config.BodyCooldown;
                    break;
                case CollisionEventType::Slide:
                case CollisionEventType::Scrape:
                    entry.SlideCooldown = m_Config.BodyCooldown;
                    break;
                case CollisionEventType::Roll:
                    entry.RollCooldown = m_Config.BodyCooldown;
                    break;
            }
            m_BodyCooldowns[bodyId.GetIndex()] = entry;
            return true;
        }

        // Check and update cooldown
        CooldownEntry& entry = it->second;
        bool allowed = false;

        switch (eventType) {
            case CollisionEventType::Impact:
                if (entry.ImpactCooldown <= 0.0f) {
                    entry.ImpactCooldown = m_Config.BodyCooldown;
                    allowed = true;
                }
                break;
            case CollisionEventType::Slide:
            case CollisionEventType::Scrape:
                if (entry.SlideCooldown <= 0.0f) {
                    entry.SlideCooldown = m_Config.BodyCooldown;
                    allowed = true;
                }
                break;
            case CollisionEventType::Roll:
                if (entry.RollCooldown <= 0.0f) {
                    entry.RollCooldown = m_Config.BodyCooldown;
                    allowed = true;
                }
                break;
        }

        return allowed;
    }

    void PhysicsAudioManager::UpdateCooldowns(float deltaTime) {
        std::lock_guard<std::mutex> lock(m_CooldownMutex);

        auto it = m_BodyCooldowns.begin();
        while (it != m_BodyCooldowns.end()) {
            CooldownEntry& entry = it->second;
            entry.ImpactCooldown = std::max(0.0f, entry.ImpactCooldown - deltaTime);
            entry.SlideCooldown = std::max(0.0f, entry.SlideCooldown - deltaTime);
            entry.RollCooldown = std::max(0.0f, entry.RollCooldown - deltaTime);

            // Remove entries that are fully cooled down (cleanup)
            if (entry.ImpactCooldown <= 0.0f && 
                entry.SlideCooldown <= 0.0f && 
                entry.RollCooldown <= 0.0f) {
                it = m_BodyCooldowns.erase(it);
            } else {
                ++it;
            }
        }
    }

    PhysicsMaterialAudioProperties PhysicsAudioManager::GetCombinedMaterial(
        PhysicsMaterialType mat1, PhysicsMaterialType mat2) const
    {
        // Get both materials
        const PhysicsMaterialAudioProperties* props1 = GetMaterial(mat1);
        const PhysicsMaterialAudioProperties* props2 = GetMaterial(mat2);

        // If neither exists, return default
        if (!props1 && !props2) {
            PhysicsMaterialAudioProperties defaultProps;
            return defaultProps;
        }

        // If only one exists, use it
        if (!props1) return *props2;
        if (!props2) return *props1;

        // Combine materials - use the "louder" one (higher volume scale) for impacts
        // but blend other properties
        PhysicsMaterialAudioProperties combined;

        if (props1->ImpactVolumeScale >= props2->ImpactVolumeScale) {
            combined = *props1;
        } else {
            combined = *props2;
        }

        // Average the thresholds
        combined.MinImpactImpulse = (props1->MinImpactImpulse + props2->MinImpactImpulse) * 0.5f;
        combined.MinSlideSpeed = (props1->MinSlideSpeed + props2->MinSlideSpeed) * 0.5f;
        combined.MinRollSpeed = (props1->MinRollSpeed + props2->MinRollSpeed) * 0.5f;
        combined.PitchVariation = (props1->PitchVariation + props2->PitchVariation) * 0.5f;

        return combined;
    }

    // ============================================================================
    // AudioContactListener Implementation
    // ============================================================================

    JPH::ValidateResult AudioContactListener::OnContactValidate(
        const JPH::Body& /*inBody1*/,
        const JPH::Body& /*inBody2*/,
        JPH::RVec3Arg /*inBaseOffset*/,
        const JPH::CollideShapeResult& /*inCollisionResult*/)
    {
        // Accept all contacts - we're just listening for audio
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void AudioContactListener::OnContactAdded(
        const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        const JPH::ContactManifold& inManifold,
        JPH::ContactSettings& /*ioSettings*/)
    {
        if (!m_Enabled || !m_AudioManager) {
            return;
        }

        // Get contact point (use first contact point)
        JPH::RVec3 contactPoint = inManifold.GetWorldSpaceContactPointOn1(0);
        glm::vec3 position(
            static_cast<float>(contactPoint.GetX()),
            static_cast<float>(contactPoint.GetY()),
            static_cast<float>(contactPoint.GetZ())
        );

        // Get contact normal
        JPH::Vec3 normal = inManifold.mWorldSpaceNormal;
        glm::vec3 normalVec(normal.GetX(), normal.GetY(), normal.GetZ());

        // Calculate impulse from relative velocity
        float impulse = 0.0f;
        if (m_UseLinearVelocity) {
            JPH::Vec3 vel1 = inBody1.GetLinearVelocity();
            JPH::Vec3 vel2 = inBody2.GetLinearVelocity();
            JPH::Vec3 relVel = vel1 - vel2;
            impulse = relVel.Length();

            // Weight by mass (heavier objects = louder impacts)
            float mass1 = 1.0f / std::max(0.001f, inBody1.GetMotionProperties() ? 
                          inBody1.GetMotionProperties()->GetInverseMass() : 1.0f);
            float mass2 = 1.0f / std::max(0.001f, inBody2.GetMotionProperties() ? 
                          inBody2.GetMotionProperties()->GetInverseMass() : 1.0f);
            float combinedMass = (mass1 + mass2) * 0.5f;
            impulse *= std::min(1.0f, combinedMass / 10.0f);  // Normalize to ~10kg
        }

        // Get entity user data if available
        uint64_t entity1 = inBody1.GetUserData();
        uint64_t entity2 = inBody2.GetUserData();

        // Report impact
        m_AudioManager->OnCollisionImpact(
            position, normalVec, impulse,
            inBody1.GetID(), inBody2.GetID(),
            entity1, entity2
        );

        // Track for persistent contact
        uint64_t key = MakeContactKey(inBody1.GetID(), inBody2.GetID());
        PersistentContact contact;
        contact.Body1 = inBody1.GetID();
        contact.Body2 = inBody2.GetID();
        contact.LastPosition = position;
        contact.ContactDuration = 0.0f;
        m_PersistentContacts[key] = contact;
    }

    void AudioContactListener::OnContactPersisted(
        const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        const JPH::ContactManifold& inManifold,
        JPH::ContactSettings& /*ioSettings*/)
    {
        if (!m_Enabled || !m_AudioManager) {
            return;
        }

        uint64_t key = MakeContactKey(inBody1.GetID(), inBody2.GetID());
        auto it = m_PersistentContacts.find(key);

        if (it == m_PersistentContacts.end()) {
            return;
        }

        PersistentContact& contact = it->second;

        // Get current contact position
        JPH::RVec3 contactPoint = inManifold.GetWorldSpaceContactPointOn1(0);
        glm::vec3 position(
            static_cast<float>(contactPoint.GetX()),
            static_cast<float>(contactPoint.GetY()),
            static_cast<float>(contactPoint.GetZ())
        );

        // Calculate sliding velocity from position change
        glm::vec3 velocity = position - contact.LastPosition;
        float contactSpeed = glm::length(velocity);

        // Update tracking
        contact.LastPosition = position;
        contact.ContactDuration += 0.016f;  // Approximate fixed timestep

        // If moving fast enough, report as slide
        if (contactSpeed > 0.01f) {
            uint64_t entity1 = inBody1.GetUserData();
            uint64_t entity2 = inBody2.GetUserData();

            m_AudioManager->OnCollisionSlide(
                position, velocity, contactSpeed,
                inBody1.GetID(), inBody2.GetID(),
                entity1, entity2
            );
        }
    }

    void AudioContactListener::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair)
    {
        // Remove from persistent contacts tracking
        uint64_t key = MakeContactKey(inSubShapePair.GetBody1ID(), inSubShapePair.GetBody2ID());
        m_PersistentContacts.erase(key);
    }

    // ============================================================================
    // PhysicsAudioBridge Implementation
    // ============================================================================

    std::unique_ptr<AudioContactListener> PhysicsAudioBridge::s_ContactListener;
    bool PhysicsAudioBridge::s_Initialized = false;

    bool PhysicsAudioBridge::Initialize() {
        if (s_Initialized) {
            ENGINE_CORE_WARN("PhysicsAudioBridge already initialized");
            return true;
        }

        // Create audio contact listener
        s_ContactListener = std::make_unique<AudioContactListener>();
        s_ContactListener->SetPhysicsAudioManager(&PhysicsAudioManager::Get());

        s_Initialized = true;
        ENGINE_CORE_INFO("PhysicsAudioBridge initialized");

        return true;
    }

    void PhysicsAudioBridge::Shutdown() {
        if (!s_Initialized) {
            return;
        }

        s_ContactListener.reset();
        s_Initialized = false;

        ENGINE_CORE_INFO("PhysicsAudioBridge shutdown");
    }

    AudioContactListener* PhysicsAudioBridge::GetContactListener() {
        return s_ContactListener.get();
    }

    void PhysicsAudioBridge::SetEnabled(bool enabled) {
        if (s_ContactListener) {
            s_ContactListener->SetEnabled(enabled);
        }
    }

    bool PhysicsAudioBridge::IsEnabled() {
        return s_ContactListener ? s_ContactListener->IsEnabled() : false;
    }

} // namespace Audio
} // namespace Core
