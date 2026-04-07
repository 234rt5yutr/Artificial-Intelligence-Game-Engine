#pragma once

// Common Gameplay Events
// Pre-defined event types for common gameplay scenarios

#include "Event.h"
#include <glm/glm.hpp>
#include <string>
#include <any>

namespace Core {
namespace Events {

    //=========================================================================
    // Event Categories
    //=========================================================================

    namespace Category {
        constexpr const char* Input = "Input";
        constexpr const char* Physics = "Physics";
        constexpr const char* Gameplay = "Gameplay";
        constexpr const char* AI = "AI";
        constexpr const char* UI = "UI";
        constexpr const char* Scene = "Scene";
        constexpr const char* Entity = "Entity";
        constexpr const char* Audio = "Audio";
        constexpr const char* Network = "Network";
        constexpr const char* System = "System";
    }

    //=========================================================================
    // Entity Events
    //=========================================================================

    /// Entity created event
    class EntityCreatedEvent : public Event<EntityCreatedEvent> {
    public:
        uint32_t EntityId;
        std::string EntityName;

        EntityCreatedEvent(uint32_t id, const std::string& name = "")
            : EntityId(id), EntityName(name) {}

        const char* GetTypeName() const override { return "EntityCreatedEvent"; }
        const char* GetCategory() const override { return Category::Entity; }

        Json ToJson() const override {
            Json j = Event::ToJson();
            j["entityId"] = EntityId;
            j["entityName"] = EntityName;
            return j;
        }
    };

    /// Entity destroyed event
    class EntityDestroyedEvent : public Event<EntityDestroyedEvent> {
    public:
        uint32_t EntityId;

        explicit EntityDestroyedEvent(uint32_t id) : EntityId(id) {}

        const char* GetTypeName() const override { return "EntityDestroyedEvent"; }
        const char* GetCategory() const override { return Category::Entity; }
    };

    /// Component added to entity
    class ComponentAddedEvent : public Event<ComponentAddedEvent> {
    public:
        uint32_t EntityId;
        std::string ComponentType;

        ComponentAddedEvent(uint32_t entityId, const std::string& componentType)
            : EntityId(entityId), ComponentType(componentType) {}

        const char* GetTypeName() const override { return "ComponentAddedEvent"; }
        const char* GetCategory() const override { return Category::Entity; }
    };

    /// Component removed from entity
    class ComponentRemovedEvent : public Event<ComponentRemovedEvent> {
    public:
        uint32_t EntityId;
        std::string ComponentType;

        ComponentRemovedEvent(uint32_t entityId, const std::string& componentType)
            : EntityId(entityId), ComponentType(componentType) {}

        const char* GetTypeName() const override { return "ComponentRemovedEvent"; }
        const char* GetCategory() const override { return Category::Entity; }
    };

    //=========================================================================
    // Physics Events
    //=========================================================================

    /// Collision event data
    struct CollisionData {
        uint32_t EntityA;
        uint32_t EntityB;
        glm::vec3 ContactPoint;
        glm::vec3 ContactNormal;
        float ImpactForce;
    };

    /// Collision begin event
    class CollisionBeginEvent : public Event<CollisionBeginEvent> {
    public:
        CollisionData Data;

        explicit CollisionBeginEvent(const CollisionData& data) : Data(data) {}

        const char* GetTypeName() const override { return "CollisionBeginEvent"; }
        const char* GetCategory() const override { return Category::Physics; }

        Json ToJson() const override {
            Json j = Event::ToJson();
            j["entityA"] = Data.EntityA;
            j["entityB"] = Data.EntityB;
            j["contactPoint"] = {Data.ContactPoint.x, Data.ContactPoint.y, Data.ContactPoint.z};
            j["impactForce"] = Data.ImpactForce;
            return j;
        }
    };

    /// Collision end event
    class CollisionEndEvent : public Event<CollisionEndEvent> {
    public:
        uint32_t EntityA;
        uint32_t EntityB;

        CollisionEndEvent(uint32_t a, uint32_t b) : EntityA(a), EntityB(b) {}

        const char* GetTypeName() const override { return "CollisionEndEvent"; }
        const char* GetCategory() const override { return Category::Physics; }
    };

    /// Trigger entered
    class TriggerEnterEvent : public Event<TriggerEnterEvent> {
    public:
        uint32_t TriggerEntity;
        uint32_t OtherEntity;

        TriggerEnterEvent(uint32_t trigger, uint32_t other)
            : TriggerEntity(trigger), OtherEntity(other) {}

        const char* GetTypeName() const override { return "TriggerEnterEvent"; }
        const char* GetCategory() const override { return Category::Physics; }
    };

    /// Trigger exited
    class TriggerExitEvent : public Event<TriggerExitEvent> {
    public:
        uint32_t TriggerEntity;
        uint32_t OtherEntity;

        TriggerExitEvent(uint32_t trigger, uint32_t other)
            : TriggerEntity(trigger), OtherEntity(other) {}

        const char* GetTypeName() const override { return "TriggerExitEvent"; }
        const char* GetCategory() const override { return Category::Physics; }
    };

    //=========================================================================
    // Gameplay Events
    //=========================================================================

    /// Damage taken event
    class DamageEvent : public Event<DamageEvent> {
    public:
        uint32_t TargetEntity;
        uint32_t SourceEntity;
        float Damage;
        std::string DamageType;
        glm::vec3 HitPoint;
        glm::vec3 HitDirection;

        DamageEvent(uint32_t target, uint32_t source, float damage,
                   const std::string& type = "default")
            : TargetEntity(target), SourceEntity(source), Damage(damage)
            , DamageType(type), HitPoint(0.0f), HitDirection(0.0f) {}

        const char* GetTypeName() const override { return "DamageEvent"; }
        const char* GetCategory() const override { return Category::Gameplay; }

        Json ToJson() const override {
            Json j = Event::ToJson();
            j["target"] = TargetEntity;
            j["source"] = SourceEntity;
            j["damage"] = Damage;
            j["damageType"] = DamageType;
            return j;
        }
    };

    /// Health changed event
    class HealthChangedEvent : public Event<HealthChangedEvent> {
    public:
        uint32_t EntityId;
        float OldHealth;
        float NewHealth;
        float MaxHealth;

        HealthChangedEvent(uint32_t entity, float oldHp, float newHp, float maxHp)
            : EntityId(entity), OldHealth(oldHp), NewHealth(newHp), MaxHealth(maxHp) {}

        const char* GetTypeName() const override { return "HealthChangedEvent"; }
        const char* GetCategory() const override { return Category::Gameplay; }

        float GetHealthPercentage() const { 
            return MaxHealth > 0 ? NewHealth / MaxHealth : 0.0f; 
        }
    };

    /// Entity death event
    class DeathEvent : public Event<DeathEvent> {
    public:
        uint32_t EntityId;
        uint32_t KillerEntity;

        DeathEvent(uint32_t entity, uint32_t killer = 0)
            : EntityId(entity), KillerEntity(killer) {}

        const char* GetTypeName() const override { return "DeathEvent"; }
        const char* GetCategory() const override { return Category::Gameplay; }
    };

    /// Respawn event
    class RespawnEvent : public Event<RespawnEvent> {
    public:
        uint32_t EntityId;
        glm::vec3 SpawnPosition;

        RespawnEvent(uint32_t entity, const glm::vec3& pos)
            : EntityId(entity), SpawnPosition(pos) {}

        const char* GetTypeName() const override { return "RespawnEvent"; }
        const char* GetCategory() const override { return Category::Gameplay; }
    };

    /// Level up event
    class LevelUpEvent : public Event<LevelUpEvent> {
    public:
        uint32_t EntityId;
        int32_t OldLevel;
        int32_t NewLevel;

        LevelUpEvent(uint32_t entity, int32_t oldLvl, int32_t newLvl)
            : EntityId(entity), OldLevel(oldLvl), NewLevel(newLvl) {}

        const char* GetTypeName() const override { return "LevelUpEvent"; }
        const char* GetCategory() const override { return Category::Gameplay; }
    };

    /// Score changed event
    class ScoreChangedEvent : public Event<ScoreChangedEvent> {
    public:
        uint32_t PlayerId;
        int64_t OldScore;
        int64_t NewScore;
        std::string Reason;

        ScoreChangedEvent(uint32_t player, int64_t oldScore, int64_t newScore,
                         const std::string& reason = "")
            : PlayerId(player), OldScore(oldScore), NewScore(newScore), Reason(reason) {}

        const char* GetTypeName() const override { return "ScoreChangedEvent"; }
        const char* GetCategory() const override { return Category::Gameplay; }
    };

    //=========================================================================
    // AI Events
    //=========================================================================

    /// AI state changed
    class AIStateChangedEvent : public Event<AIStateChangedEvent> {
    public:
        uint32_t EntityId;
        std::string OldState;
        std::string NewState;

        AIStateChangedEvent(uint32_t entity, const std::string& oldState, 
                           const std::string& newState)
            : EntityId(entity), OldState(oldState), NewState(newState) {}

        const char* GetTypeName() const override { return "AIStateChangedEvent"; }
        const char* GetCategory() const override { return Category::AI; }
    };

    /// AI target acquired
    class AITargetAcquiredEvent : public Event<AITargetAcquiredEvent> {
    public:
        uint32_t AIEntity;
        uint32_t TargetEntity;

        AITargetAcquiredEvent(uint32_t ai, uint32_t target)
            : AIEntity(ai), TargetEntity(target) {}

        const char* GetTypeName() const override { return "AITargetAcquiredEvent"; }
        const char* GetCategory() const override { return Category::AI; }
    };

    /// AI target lost
    class AITargetLostEvent : public Event<AITargetLostEvent> {
    public:
        uint32_t AIEntity;
        uint32_t TargetEntity;

        AITargetLostEvent(uint32_t ai, uint32_t target)
            : AIEntity(ai), TargetEntity(target) {}

        const char* GetTypeName() const override { return "AITargetLostEvent"; }
        const char* GetCategory() const override { return Category::AI; }
    };

    //=========================================================================
    // Scene Events
    //=========================================================================

    /// Scene loaded
    class SceneLoadedEvent : public Event<SceneLoadedEvent> {
    public:
        std::string SceneName;
        std::string ScenePath;

        SceneLoadedEvent(const std::string& name, const std::string& path)
            : SceneName(name), ScenePath(path) {}

        const char* GetTypeName() const override { return "SceneLoadedEvent"; }
        const char* GetCategory() const override { return Category::Scene; }
    };

    /// Scene unloaded
    class SceneUnloadedEvent : public Event<SceneUnloadedEvent> {
    public:
        std::string SceneName;

        explicit SceneUnloadedEvent(const std::string& name) : SceneName(name) {}

        const char* GetTypeName() const override { return "SceneUnloadedEvent"; }
        const char* GetCategory() const override { return Category::Scene; }
    };

    //=========================================================================
    // Audio Events
    //=========================================================================

    /// Sound played
    class SoundPlayedEvent : public Event<SoundPlayedEvent> {
    public:
        std::string SoundName;
        glm::vec3 Position;
        float Volume;

        SoundPlayedEvent(const std::string& name, const glm::vec3& pos, float volume = 1.0f)
            : SoundName(name), Position(pos), Volume(volume) {}

        const char* GetTypeName() const override { return "SoundPlayedEvent"; }
        const char* GetCategory() const override { return Category::Audio; }
    };

    //=========================================================================
    // Custom Event Helper
    //=========================================================================

    /// Generic custom event for user-defined data
    class CustomEvent : public Event<CustomEvent> {
    public:
        std::string EventName;
        std::unordered_map<std::string, std::any> Data;

        explicit CustomEvent(const std::string& name) : EventName(name) {}

        template<typename T>
        void SetData(const std::string& key, const T& value) {
            Data[key] = value;
        }

        template<typename T>
        T GetData(const std::string& key, const T& defaultValue = T{}) const {
            auto it = Data.find(key);
            if (it != Data.end()) {
                try {
                    return std::any_cast<T>(it->second);
                } catch (...) {}
            }
            return defaultValue;
        }

        bool HasData(const std::string& key) const {
            return Data.find(key) != Data.end();
        }

        const char* GetTypeName() const override { return "CustomEvent"; }
        const char* GetCategory() const override { return Category::Gameplay; }

        std::string ToString() const override {
            return "CustomEvent: " + EventName;
        }
    };

} // namespace Events
} // namespace Core
