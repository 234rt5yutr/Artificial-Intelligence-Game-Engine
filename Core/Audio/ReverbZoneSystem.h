#pragma once

// ReverbZoneSystem
// Manages acoustic environments by tracking listener position relative to reverb zones
// and applying appropriate reverb/filter effects

#include "Core/ECS/Components/ReverbZoneComponent.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/Log.h"
#include "Core/Profile.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>
#include <algorithm>
#include <mutex>
#include <memory>
#include <functional>
#include <cmath>

namespace Core {
namespace Audio {

    // ============================================================================
    // Active Reverb State
    // ============================================================================
    // Represents the current blended reverb state applied to audio

    struct ActiveReverbState {
        ECS::ReverbParameters Parameters;
        ECS::AudioFilterParameters Filters;
        float BlendFactor = 0.0f;
        std::string ActiveZoneName;
        uint32_t ActiveZoneId = 0;
        bool IsActive = false;
    };

    // ============================================================================
    // Zone Containment Result
    // ============================================================================

    struct ZoneContainmentResult {
        bool Inside = false;
        float Distance = 0.0f;        // Distance from zone center/surface
        float BlendFactor = 0.0f;     // 0 = outside, 1 = fully inside
        glm::vec3 NearestPoint{0.0f}; // Nearest point on zone boundary
    };

    // ============================================================================
    // Reverb Zone System Configuration
    // ============================================================================

    struct ReverbZoneSystemConfig {
        // Enable/disable the entire system
        bool Enabled = true;

        // Maximum number of zones to process per frame
        uint32_t MaxZonesPerFrame = 32;

        // Blend transition speed (units per second)
        float BlendSpeed = 2.0f;

        // Minimum blend factor to consider a zone active
        float MinActiveBlendFactor = 0.01f;

        // Enable zone overlap blending
        bool EnableOverlapBlending = true;

        // Maximum zones to blend simultaneously
        uint32_t MaxSimultaneousZones = 4;

        // Update frequency (0 = every frame, N = every N frames)
        uint32_t UpdateFrequency = 0;

        // Debug visualization
        bool DebugVisualization = false;
    };

    // ============================================================================
    // Reverb Zone System
    // ============================================================================
    // Main system that manages reverb zones and applies audio effects

    class ReverbZoneSystem {
    public:
        static ReverbZoneSystem& Get();

        // Delete copy/move
        ReverbZoneSystem(const ReverbZoneSystem&) = delete;
        ReverbZoneSystem& operator=(const ReverbZoneSystem&) = delete;

        // Lifecycle
        bool Initialize(const ReverbZoneSystemConfig& config = ReverbZoneSystemConfig{});
        void Shutdown();
        bool IsInitialized() const { return m_Initialized; }

        // Update - processes zones and applies effects
        void Update(float deltaTime);

        // Configuration
        void SetConfig(const ReverbZoneSystemConfig& config) { m_Config = config; }
        const ReverbZoneSystemConfig& GetConfig() const { return m_Config; }

        // Listener position
        void SetListenerPosition(const glm::vec3& position);
        glm::vec3 GetListenerPosition() const { return m_ListenerPosition; }

        // Zone registration (for non-ECS usage)
        uint32_t RegisterZone(const ECS::ReverbZoneComponent& zone, const glm::vec3& position);
        void UnregisterZone(uint32_t zoneId);
        void UpdateZoneTransform(uint32_t zoneId, const glm::vec3& position, const glm::quat& rotation = glm::quat(1, 0, 0, 0));
        void UpdateZoneParameters(uint32_t zoneId, const ECS::ReverbParameters& params);

        // Query current state
        const ActiveReverbState& GetActiveReverbState() const { return m_ActiveState; }
        std::vector<uint32_t> GetActiveZoneIds() const;
        bool IsListenerInZone(uint32_t zoneId) const;
        float GetZoneBlendFactor(uint32_t zoneId) const;

        // Global reverb override
        void SetGlobalReverb(const ECS::ReverbParameters& params, float blendTime = 0.0f);
        void ClearGlobalReverb(float blendTime = 0.0f);

        // Callbacks
        using ZoneEnterCallback = std::function<void(uint32_t zoneId, const std::string& zoneName)>;
        using ZoneExitCallback = std::function<void(uint32_t zoneId, const std::string& zoneName)>;
        void SetZoneEnterCallback(ZoneEnterCallback callback) { m_OnZoneEnter = callback; }
        void SetZoneExitCallback(ZoneExitCallback callback) { m_OnZoneExit = callback; }

        // Debug/Statistics
        uint32_t GetRegisteredZoneCount() const;
        uint32_t GetActiveZoneCount() const;

    private:
        ReverbZoneSystem() = default;
        ~ReverbZoneSystem();

        // Internal zone representation
        struct RegisteredZone {
            uint32_t Id = 0;
            ECS::ReverbZoneComponent Component;
            glm::vec3 Position{0.0f};
            glm::quat Rotation{1, 0, 0, 0};
            bool WasInside = false;
            float CurrentBlend = 0.0f;
            float TargetBlend = 0.0f;
        };

        // Zone containment testing
        ZoneContainmentResult TestZoneContainment(const RegisteredZone& zone, const glm::vec3& point) const;
        ZoneContainmentResult TestBoxContainment(const glm::vec3& zonePos, const glm::quat& zoneRot,
                                                  const glm::vec3& halfExtents, float blendDist,
                                                  const glm::vec3& point) const;
        ZoneContainmentResult TestSphereContainment(const glm::vec3& zonePos, float radius,
                                                     float blendDist, const glm::vec3& point) const;
        ZoneContainmentResult TestCapsuleContainment(const glm::vec3& zonePos, const glm::quat& zoneRot,
                                                      float radius, float height, float blendDist,
                                                      const glm::vec3& point) const;
        ZoneContainmentResult TestCylinderContainment(const glm::vec3& zonePos, const glm::quat& zoneRot,
                                                       float radius, float height, float blendDist,
                                                       const glm::vec3& point) const;

        // Parameter blending
        ECS::ReverbParameters BlendParameters(const ECS::ReverbParameters& a, 
                                               const ECS::ReverbParameters& b, float t) const;
        ECS::AudioFilterParameters BlendFilters(const ECS::AudioFilterParameters& a,
                                                 const ECS::AudioFilterParameters& b, float t) const;

        // Apply effects to audio system
        void ApplyReverbState(const ActiveReverbState& state);

    private:
        ReverbZoneSystemConfig m_Config;
        bool m_Initialized = false;

        // Listener state
        glm::vec3 m_ListenerPosition{0.0f};

        // Registered zones
        mutable std::mutex m_ZonesMutex;
        std::vector<std::unique_ptr<RegisteredZone>> m_Zones;
        uint32_t m_NextZoneId = 1;

        // Active reverb state
        ActiveReverbState m_ActiveState;
        ActiveReverbState m_TargetState;

        // Global reverb override
        bool m_GlobalReverbActive = false;
        ECS::ReverbParameters m_GlobalReverbParams;
        float m_GlobalReverbBlend = 0.0f;
        float m_GlobalReverbTargetBlend = 0.0f;

        // Frame counter for update frequency
        uint32_t m_FrameCounter = 0;

        // Callbacks
        ZoneEnterCallback m_OnZoneEnter;
        ZoneExitCallback m_OnZoneExit;
    };

    // ============================================================================
    // Implementation
    // ============================================================================

    inline ReverbZoneSystem& ReverbZoneSystem::Get() {
        static ReverbZoneSystem instance;
        return instance;
    }

    inline ReverbZoneSystem::~ReverbZoneSystem() {
        if (m_Initialized) {
            Shutdown();
        }
    }

    inline bool ReverbZoneSystem::Initialize(const ReverbZoneSystemConfig& config) {
        PROFILE_FUNCTION();

        if (m_Initialized) {
            ENGINE_CORE_WARN("ReverbZoneSystem already initialized");
            return true;
        }

        m_Config = config;
        m_Initialized = true;

        ENGINE_CORE_INFO("ReverbZoneSystem initialized");
        return true;
    }

    inline void ReverbZoneSystem::Shutdown() {
        PROFILE_FUNCTION();

        if (!m_Initialized) {
            return;
        }

        ENGINE_CORE_INFO("Shutting down ReverbZoneSystem...");

        {
            std::lock_guard<std::mutex> lock(m_ZonesMutex);
            m_Zones.clear();
        }

        m_Initialized = false;
        ENGINE_CORE_INFO("ReverbZoneSystem shutdown complete");
    }

    inline void ReverbZoneSystem::Update(float deltaTime) {
        PROFILE_FUNCTION();

        if (!m_Initialized || !m_Config.Enabled) {
            return;
        }

        // Check update frequency
        m_FrameCounter++;
        if (m_Config.UpdateFrequency > 0 && (m_FrameCounter % (m_Config.UpdateFrequency + 1)) != 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_ZonesMutex);

        // Collect active zones with blend factors
        struct ActiveZoneInfo {
            RegisteredZone* Zone;
            float BlendFactor;
            float Priority;
        };
        std::vector<ActiveZoneInfo> activeZones;

        // Test each zone for containment
        for (auto& zone : m_Zones) {
            if (!zone || !zone->Component.IsEnabled) {
                continue;
            }

            ZoneContainmentResult result = TestZoneContainment(*zone, m_ListenerPosition);

            // Update target blend
            zone->TargetBlend = result.BlendFactor;

            // Smoothly transition current blend
            float blendDelta = m_Config.BlendSpeed * deltaTime;
            if (zone->CurrentBlend < zone->TargetBlend) {
                zone->CurrentBlend = std::min(zone->CurrentBlend + blendDelta, zone->TargetBlend);
            } else if (zone->CurrentBlend > zone->TargetBlend) {
                zone->CurrentBlend = std::max(zone->CurrentBlend - blendDelta, zone->TargetBlend);
            }

            // Check for enter/exit events
            bool isInside = zone->CurrentBlend > m_Config.MinActiveBlendFactor;
            if (isInside && !zone->WasInside) {
                // Entered zone
                if (m_OnZoneEnter) {
                    m_OnZoneEnter(zone->Id, zone->Component.ZoneName);
                }
                ENGINE_CORE_DEBUG("Entered reverb zone: {} (ID: {})", zone->Component.ZoneName, zone->Id);
            } else if (!isInside && zone->WasInside) {
                // Exited zone
                if (m_OnZoneExit) {
                    m_OnZoneExit(zone->Id, zone->Component.ZoneName);
                }
                ENGINE_CORE_DEBUG("Exited reverb zone: {} (ID: {})", zone->Component.ZoneName, zone->Id);
            }
            zone->WasInside = isInside;

            // Add to active zones if blend is significant
            if (zone->CurrentBlend > m_Config.MinActiveBlendFactor) {
                activeZones.push_back({ zone.get(), zone->CurrentBlend, zone->Component.Priority });
            }
        }

        // Sort by priority (higher priority first)
        std::sort(activeZones.begin(), activeZones.end(),
                  [](const ActiveZoneInfo& a, const ActiveZoneInfo& b) {
                      return a.Priority > b.Priority;
                  });

        // Limit number of simultaneous zones
        if (activeZones.size() > m_Config.MaxSimultaneousZones) {
            activeZones.resize(m_Config.MaxSimultaneousZones);
        }

        // Blend active zones together
        if (activeZones.empty()) {
            // No active zones - use default (no reverb)
            m_TargetState.IsActive = false;
            m_TargetState.BlendFactor = 0.0f;
            m_TargetState.ActiveZoneName = "";
            m_TargetState.ActiveZoneId = 0;
            m_TargetState.Parameters = ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::None);
        } else if (activeZones.size() == 1 || !m_Config.EnableOverlapBlending) {
            // Single zone or no blending - use highest priority
            auto& topZone = activeZones[0];
            m_TargetState.IsActive = true;
            m_TargetState.BlendFactor = topZone.BlendFactor;
            m_TargetState.ActiveZoneName = topZone.Zone->Component.ZoneName;
            m_TargetState.ActiveZoneId = topZone.Zone->Id;
            m_TargetState.Parameters = topZone.Zone->Component.Parameters;
            m_TargetState.Filters = topZone.Zone->Component.Filters;
        } else {
            // Blend multiple zones
            float totalWeight = 0.0f;
            for (const auto& info : activeZones) {
                totalWeight += info.BlendFactor;
            }

            if (totalWeight > 0.0f) {
                // Initialize with first zone
                m_TargetState.Parameters = activeZones[0].Zone->Component.Parameters;
                m_TargetState.Filters = activeZones[0].Zone->Component.Filters;
                float firstWeight = activeZones[0].BlendFactor / totalWeight;

                // Blend in other zones
                float accumulatedWeight = firstWeight;
                for (size_t i = 1; i < activeZones.size(); ++i) {
                    float weight = activeZones[i].BlendFactor / totalWeight;
                    float blendT = weight / (accumulatedWeight + weight);

                    m_TargetState.Parameters = BlendParameters(
                        m_TargetState.Parameters,
                        activeZones[i].Zone->Component.Parameters,
                        blendT
                    );
                    m_TargetState.Filters = BlendFilters(
                        m_TargetState.Filters,
                        activeZones[i].Zone->Component.Filters,
                        blendT
                    );

                    accumulatedWeight += weight;
                }

                m_TargetState.IsActive = true;
                m_TargetState.BlendFactor = std::min(1.0f, totalWeight);
                m_TargetState.ActiveZoneName = activeZones[0].Zone->Component.ZoneName + " (blended)";
                m_TargetState.ActiveZoneId = activeZones[0].Zone->Id;
            }
        }

        // Apply global reverb override if active
        if (m_GlobalReverbActive) {
            float blendDelta = m_Config.BlendSpeed * deltaTime;
            if (m_GlobalReverbBlend < m_GlobalReverbTargetBlend) {
                m_GlobalReverbBlend = std::min(m_GlobalReverbBlend + blendDelta, m_GlobalReverbTargetBlend);
            } else if (m_GlobalReverbBlend > m_GlobalReverbTargetBlend) {
                m_GlobalReverbBlend = std::max(m_GlobalReverbBlend - blendDelta, m_GlobalReverbTargetBlend);
            }

            if (m_GlobalReverbBlend > 0.0f) {
                m_TargetState.Parameters = BlendParameters(
                    m_TargetState.Parameters,
                    m_GlobalReverbParams,
                    m_GlobalReverbBlend
                );
                m_TargetState.IsActive = true;
            }

            // Disable global if fully blended out
            if (m_GlobalReverbTargetBlend <= 0.0f && m_GlobalReverbBlend <= 0.0f) {
                m_GlobalReverbActive = false;
            }
        }

        // Update active state (smooth transition)
        m_ActiveState = m_TargetState;

        // Apply to audio system
        ApplyReverbState(m_ActiveState);
    }

    inline void ReverbZoneSystem::SetListenerPosition(const glm::vec3& position) {
        m_ListenerPosition = position;
    }

    inline uint32_t ReverbZoneSystem::RegisterZone(const ECS::ReverbZoneComponent& zone, const glm::vec3& position) {
        std::lock_guard<std::mutex> lock(m_ZonesMutex);

        auto registeredZone = std::make_unique<RegisteredZone>();
        registeredZone->Id = m_NextZoneId++;
        registeredZone->Component = zone;
        registeredZone->Component.ZoneId = registeredZone->Id;
        registeredZone->Position = position;

        uint32_t id = registeredZone->Id;
        m_Zones.push_back(std::move(registeredZone));

        ENGINE_CORE_DEBUG("Registered reverb zone: {} (ID: {})", zone.ZoneName, id);
        return id;
    }

    inline void ReverbZoneSystem::UnregisterZone(uint32_t zoneId) {
        std::lock_guard<std::mutex> lock(m_ZonesMutex);

        auto it = std::find_if(m_Zones.begin(), m_Zones.end(),
                               [zoneId](const auto& z) { return z && z->Id == zoneId; });

        if (it != m_Zones.end()) {
            ENGINE_CORE_DEBUG("Unregistered reverb zone: {} (ID: {})", (*it)->Component.ZoneName, zoneId);
            m_Zones.erase(it);
        }
    }

    inline void ReverbZoneSystem::UpdateZoneTransform(uint32_t zoneId, const glm::vec3& position, const glm::quat& rotation) {
        std::lock_guard<std::mutex> lock(m_ZonesMutex);

        auto it = std::find_if(m_Zones.begin(), m_Zones.end(),
                               [zoneId](const auto& z) { return z && z->Id == zoneId; });

        if (it != m_Zones.end()) {
            (*it)->Position = position;
            (*it)->Rotation = rotation;
        }
    }

    inline void ReverbZoneSystem::UpdateZoneParameters(uint32_t zoneId, const ECS::ReverbParameters& params) {
        std::lock_guard<std::mutex> lock(m_ZonesMutex);

        auto it = std::find_if(m_Zones.begin(), m_Zones.end(),
                               [zoneId](const auto& z) { return z && z->Id == zoneId; });

        if (it != m_Zones.end()) {
            (*it)->Component.Parameters = params;
        }
    }

    inline std::vector<uint32_t> ReverbZoneSystem::GetActiveZoneIds() const {
        std::lock_guard<std::mutex> lock(m_ZonesMutex);
        std::vector<uint32_t> ids;

        for (const auto& zone : m_Zones) {
            if (zone && zone->CurrentBlend > m_Config.MinActiveBlendFactor) {
                ids.push_back(zone->Id);
            }
        }

        return ids;
    }

    inline bool ReverbZoneSystem::IsListenerInZone(uint32_t zoneId) const {
        std::lock_guard<std::mutex> lock(m_ZonesMutex);

        auto it = std::find_if(m_Zones.begin(), m_Zones.end(),
                               [zoneId](const auto& z) { return z && z->Id == zoneId; });

        return (it != m_Zones.end()) ? (*it)->WasInside : false;
    }

    inline float ReverbZoneSystem::GetZoneBlendFactor(uint32_t zoneId) const {
        std::lock_guard<std::mutex> lock(m_ZonesMutex);

        auto it = std::find_if(m_Zones.begin(), m_Zones.end(),
                               [zoneId](const auto& z) { return z && z->Id == zoneId; });

        return (it != m_Zones.end()) ? (*it)->CurrentBlend : 0.0f;
    }

    inline void ReverbZoneSystem::SetGlobalReverb(const ECS::ReverbParameters& params, float blendTime) {
        m_GlobalReverbParams = params;
        m_GlobalReverbActive = true;
        m_GlobalReverbTargetBlend = 1.0f;

        if (blendTime <= 0.0f) {
            m_GlobalReverbBlend = 1.0f;
        }
    }

    inline void ReverbZoneSystem::ClearGlobalReverb(float blendTime) {
        m_GlobalReverbTargetBlend = 0.0f;

        if (blendTime <= 0.0f) {
            m_GlobalReverbBlend = 0.0f;
            m_GlobalReverbActive = false;
        }
    }

    inline uint32_t ReverbZoneSystem::GetRegisteredZoneCount() const {
        std::lock_guard<std::mutex> lock(m_ZonesMutex);
        return static_cast<uint32_t>(m_Zones.size());
    }

    inline uint32_t ReverbZoneSystem::GetActiveZoneCount() const {
        std::lock_guard<std::mutex> lock(m_ZonesMutex);
        uint32_t count = 0;

        for (const auto& zone : m_Zones) {
            if (zone && zone->CurrentBlend > m_Config.MinActiveBlendFactor) {
                ++count;
            }
        }

        return count;
    }

    inline ZoneContainmentResult ReverbZoneSystem::TestZoneContainment(
        const RegisteredZone& zone, const glm::vec3& point) const
    {
        // Global zones always contain the listener
        if (zone.Component.IsGlobal) {
            ZoneContainmentResult result;
            result.Inside = true;
            result.Distance = 0.0f;
            result.BlendFactor = 1.0f;
            result.NearestPoint = point;
            return result;
        }

        switch (zone.Component.Shape) {
            case ECS::AudioZoneShape::Box:
                return TestBoxContainment(zone.Position, zone.Rotation,
                                          zone.Component.BoxHalfExtents,
                                          zone.Component.BlendDistance, point);

            case ECS::AudioZoneShape::Sphere:
                return TestSphereContainment(zone.Position, zone.Component.Radius,
                                             zone.Component.BlendDistance, point);

            case ECS::AudioZoneShape::Capsule:
                return TestCapsuleContainment(zone.Position, zone.Rotation,
                                              zone.Component.Radius, zone.Component.Height,
                                              zone.Component.BlendDistance, point);

            case ECS::AudioZoneShape::Cylinder:
                return TestCylinderContainment(zone.Position, zone.Rotation,
                                               zone.Component.Radius, zone.Component.Height,
                                               zone.Component.BlendDistance, point);

            default:
                return ZoneContainmentResult{};
        }
    }

    inline ZoneContainmentResult ReverbZoneSystem::TestBoxContainment(
        const glm::vec3& zonePos, const glm::quat& zoneRot,
        const glm::vec3& halfExtents, float blendDist,
        const glm::vec3& point) const
    {
        ZoneContainmentResult result;

        // Transform point to local space
        glm::vec3 localPoint = glm::inverse(zoneRot) * (point - zonePos);

        // Extended half extents (including blend distance)
        glm::vec3 extendedHalf = halfExtents + glm::vec3(blendDist);

        // Check if outside extended box
        if (std::abs(localPoint.x) > extendedHalf.x ||
            std::abs(localPoint.y) > extendedHalf.y ||
            std::abs(localPoint.z) > extendedHalf.z) {
            result.Inside = false;
            result.BlendFactor = 0.0f;
            return result;
        }

        // Check if inside inner box
        if (std::abs(localPoint.x) <= halfExtents.x &&
            std::abs(localPoint.y) <= halfExtents.y &&
            std::abs(localPoint.z) <= halfExtents.z) {
            result.Inside = true;
            result.Distance = 0.0f;
            result.BlendFactor = 1.0f;
            result.NearestPoint = point;
            return result;
        }

        // In blend zone - calculate distance to inner box
        glm::vec3 clamped = glm::clamp(localPoint, -halfExtents, halfExtents);
        float distance = glm::length(localPoint - clamped);

        result.Inside = true;  // Inside outer bounds
        result.Distance = distance;
        result.BlendFactor = 1.0f - (distance / blendDist);
        result.BlendFactor = std::clamp(result.BlendFactor, 0.0f, 1.0f);
        result.NearestPoint = zonePos + zoneRot * clamped;

        return result;
    }

    inline ZoneContainmentResult ReverbZoneSystem::TestSphereContainment(
        const glm::vec3& zonePos, float radius,
        float blendDist, const glm::vec3& point) const
    {
        ZoneContainmentResult result;

        float distance = glm::length(point - zonePos);
        float outerRadius = radius + blendDist;

        if (distance > outerRadius) {
            result.Inside = false;
            result.BlendFactor = 0.0f;
            return result;
        }

        if (distance <= radius) {
            result.Inside = true;
            result.Distance = 0.0f;
            result.BlendFactor = 1.0f;
            result.NearestPoint = point;
            return result;
        }

        // In blend zone
        result.Inside = true;
        result.Distance = distance - radius;
        result.BlendFactor = 1.0f - ((distance - radius) / blendDist);
        result.BlendFactor = std::clamp(result.BlendFactor, 0.0f, 1.0f);

        glm::vec3 dir = (distance > 0.001f) ? (point - zonePos) / distance : glm::vec3(0, 1, 0);
        result.NearestPoint = zonePos + dir * radius;

        return result;
    }

    inline ZoneContainmentResult ReverbZoneSystem::TestCapsuleContainment(
        const glm::vec3& zonePos, const glm::quat& zoneRot,
        float radius, float height, float blendDist,
        const glm::vec3& point) const
    {
        ZoneContainmentResult result;

        // Capsule axis is Y-up in local space
        glm::vec3 up = zoneRot * glm::vec3(0, 1, 0);
        float halfHeight = height * 0.5f;

        // Get closest point on capsule axis
        glm::vec3 lineStart = zonePos - up * halfHeight;
        glm::vec3 lineEnd = zonePos + up * halfHeight;

        glm::vec3 lineDir = lineEnd - lineStart;
        float lineLen = glm::length(lineDir);
        if (lineLen > 0.001f) lineDir /= lineLen;

        float t = glm::dot(point - lineStart, lineDir);
        t = std::clamp(t, 0.0f, lineLen);
        glm::vec3 closestOnAxis = lineStart + lineDir * t;

        // Distance from axis
        float distance = glm::length(point - closestOnAxis);
        float outerRadius = radius + blendDist;

        if (distance > outerRadius) {
            result.Inside = false;
            result.BlendFactor = 0.0f;
            return result;
        }

        if (distance <= radius) {
            result.Inside = true;
            result.Distance = 0.0f;
            result.BlendFactor = 1.0f;
            result.NearestPoint = point;
            return result;
        }

        // In blend zone
        result.Inside = true;
        result.Distance = distance - radius;
        result.BlendFactor = 1.0f - ((distance - radius) / blendDist);
        result.BlendFactor = std::clamp(result.BlendFactor, 0.0f, 1.0f);

        glm::vec3 dir = (distance > 0.001f) ? (point - closestOnAxis) / distance : glm::vec3(1, 0, 0);
        result.NearestPoint = closestOnAxis + dir * radius;

        return result;
    }

    inline ZoneContainmentResult ReverbZoneSystem::TestCylinderContainment(
        const glm::vec3& zonePos, const glm::quat& zoneRot,
        float radius, float height, float blendDist,
        const glm::vec3& point) const
    {
        ZoneContainmentResult result;

        // Transform point to local space
        glm::vec3 localPoint = glm::inverse(zoneRot) * (point - zonePos);
        float halfHeight = height * 0.5f;

        // Check height bounds
        float outerHalfHeight = halfHeight + blendDist;
        if (std::abs(localPoint.y) > outerHalfHeight) {
            result.Inside = false;
            result.BlendFactor = 0.0f;
            return result;
        }

        // Check radial distance (XZ plane)
        float radialDist = std::sqrt(localPoint.x * localPoint.x + localPoint.z * localPoint.z);
        float outerRadius = radius + blendDist;

        if (radialDist > outerRadius) {
            result.Inside = false;
            result.BlendFactor = 0.0f;
            return result;
        }

        // Calculate blend factors for height and radius
        float heightBlend = 1.0f;
        if (std::abs(localPoint.y) > halfHeight) {
            heightBlend = 1.0f - ((std::abs(localPoint.y) - halfHeight) / blendDist);
        }

        float radialBlend = 1.0f;
        if (radialDist > radius) {
            radialBlend = 1.0f - ((radialDist - radius) / blendDist);
        }

        result.Inside = true;
        result.BlendFactor = std::min(heightBlend, radialBlend);
        result.BlendFactor = std::clamp(result.BlendFactor, 0.0f, 1.0f);
        result.Distance = std::max(0.0f, radialDist - radius) + 
                          std::max(0.0f, std::abs(localPoint.y) - halfHeight);

        // Nearest point on cylinder surface
        glm::vec3 nearestLocal = localPoint;
        if (radialDist > 0.001f) {
            nearestLocal.x = (localPoint.x / radialDist) * std::min(radialDist, radius);
            nearestLocal.z = (localPoint.z / radialDist) * std::min(radialDist, radius);
        }
        nearestLocal.y = std::clamp(localPoint.y, -halfHeight, halfHeight);
        result.NearestPoint = zonePos + zoneRot * nearestLocal;

        return result;
    }

    inline ECS::ReverbParameters ReverbZoneSystem::BlendParameters(
        const ECS::ReverbParameters& a, const ECS::ReverbParameters& b, float t) const
    {
        ECS::ReverbParameters result;

        auto lerp = [](float x, float y, float t) { return x + t * (y - x); };

        result.PreDelayMs = lerp(a.PreDelayMs, b.PreDelayMs, t);
        result.RoomSize = lerp(a.RoomSize, b.RoomSize, t);
        result.DecayTimeSeconds = lerp(a.DecayTimeSeconds, b.DecayTimeSeconds, t);
        result.HighFrequencyDamping = lerp(a.HighFrequencyDamping, b.HighFrequencyDamping, t);
        result.Diffusion = lerp(a.Diffusion, b.Diffusion, t);
        result.EarlyReflectionsGainDb = lerp(a.EarlyReflectionsGainDb, b.EarlyReflectionsGainDb, t);
        result.LateReverbGainDb = lerp(a.LateReverbGainDb, b.LateReverbGainDb, t);
        result.WetDryMix = lerp(a.WetDryMix, b.WetDryMix, t);
        result.Density = lerp(a.Density, b.Density, t);
        result.LowFrequencyRatio = lerp(a.LowFrequencyRatio, b.LowFrequencyRatio, t);
        result.HighFrequencyRatio = lerp(a.HighFrequencyRatio, b.HighFrequencyRatio, t);
        result.ModulationDepth = lerp(a.ModulationDepth, b.ModulationDepth, t);
        result.ModulationRateHz = lerp(a.ModulationRateHz, b.ModulationRateHz, t);

        return result;
    }

    inline ECS::AudioFilterParameters ReverbZoneSystem::BlendFilters(
        const ECS::AudioFilterParameters& a, const ECS::AudioFilterParameters& b, float t) const
    {
        ECS::AudioFilterParameters result;

        auto lerp = [](float x, float y, float t) { return x + t * (y - x); };

        result.LowPassCutoffHz = lerp(a.LowPassCutoffHz, b.LowPassCutoffHz, t);
        result.HighPassCutoffHz = lerp(a.HighPassCutoffHz, b.HighPassCutoffHz, t);
        result.FilterResonance = lerp(a.FilterResonance, b.FilterResonance, t);
        result.VolumeAdjustmentDb = lerp(a.VolumeAdjustmentDb, b.VolumeAdjustmentDb, t);
        result.PitchShift = lerp(a.PitchShift, b.PitchShift, t);

        // Echo settings - blend if either has echo enabled
        result.EnableEcho = a.EnableEcho || b.EnableEcho;
        result.EchoDelayMs = lerp(a.EchoDelayMs, b.EchoDelayMs, t);
        result.EchoDecay = lerp(a.EchoDecay, b.EchoDecay, t);
        result.EchoWetMix = lerp(a.EchoWetMix, b.EchoWetMix, t);

        return result;
    }

    inline void ReverbZoneSystem::ApplyReverbState(const ActiveReverbState& state) {
        // Note: The actual application of reverb effects would depend on the audio backend
        // Miniaudio has limited built-in reverb support, so this would typically:
        // 1. Use miniaudio's node graph with custom DSP nodes
        // 2. Use a third-party reverb library
        // 3. Apply simplified filter effects as a fallback

        // For now, we store the state and make it available for custom implementations
        // A real implementation would configure the audio engine's reverb/filter nodes here

        if (state.IsActive) {
            ENGINE_CORE_TRACE("Reverb active: {} (blend: {:.2f}, decay: {:.2f}s, wet: {:.2f})",
                             state.ActiveZoneName, state.BlendFactor,
                             state.Parameters.DecayTimeSeconds, state.Parameters.WetDryMix);
        }
    }

    // ============================================================================
    // Convenience Functions
    // ============================================================================

    inline ReverbZoneSystem& GetReverbZoneSystem() {
        return ReverbZoneSystem::Get();
    }

    inline bool InitializeReverbZones(const ReverbZoneSystemConfig& config = ReverbZoneSystemConfig{}) {
        return ReverbZoneSystem::Get().Initialize(config);
    }

    inline void ShutdownReverbZones() {
        ReverbZoneSystem::Get().Shutdown();
    }

} // namespace Audio
} // namespace Core
