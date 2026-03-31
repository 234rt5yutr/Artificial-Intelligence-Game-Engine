#pragma once

#include <cstdint>
#include "Core/Math/Math.h"

namespace Core {
namespace ECS {

    //==========================================================================
    // REPLICATION SETTINGS
    //==========================================================================

    // Network ownership determines who is authoritative over this entity
    enum class NetworkOwnership : uint8_t {
        Server = 0,         // Server owns and replicates to all clients
        LocalPlayer = 1,    // Local player owns (client-side predicted)
        RemotePlayer = 2,   // Another player owns (interpolated locally)
        Shared = 3          // Shared ownership (rare, for collaborative objects)
    };

    // Replication priority affects how often updates are sent
    enum class ReplicationPriority : uint8_t {
        Low = 0,            // Infrequent updates (scenery, distant objects)
        Normal = 1,         // Standard update rate
        High = 2,           // Frequent updates (nearby objects)
        Critical = 3        // Every tick (player, important gameplay objects)
    };

    // Interpolation mode for remote entities
    enum class NetworkInterpolationMode : uint8_t {
        None = 0,           // No interpolation (snap to values)
        Linear = 1,         // Linear interpolation between snapshots
        Hermite = 2,        // Hermite spline interpolation (smoother)
        Extrapolate = 3     // Extrapolate beyond last known state
    };

    //==========================================================================
    // COMPONENT FLAGS
    //==========================================================================

    // Flags controlling what data is replicated
    enum class ReplicationFlags : uint16_t {
        None = 0,
        Position = 1 << 0,      // Replicate position
        Rotation = 1 << 1,      // Replicate rotation
        Scale = 1 << 2,         // Replicate scale
        Velocity = 1 << 3,      // Replicate linear velocity
        AngularVelocity = 1 << 4,   // Replicate angular velocity
        CompressRotation = 1 << 5,  // Use compressed quaternion format
        DeltaCompress = 1 << 6,     // Use delta compression
        LossyCompress = 1 << 7,     // Allow lossy compression for bandwidth

        // Common presets
        PositionOnly = Position,
        PositionRotation = Position | Rotation,
        FullTransform = Position | Rotation | Scale,
        FullWithVelocity = Position | Rotation | Scale | Velocity | AngularVelocity,
        DefaultFlags = Position | Rotation | Velocity | CompressRotation
    };

    inline ReplicationFlags operator|(ReplicationFlags a, ReplicationFlags b) {
        return static_cast<ReplicationFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
    }

    inline ReplicationFlags operator&(ReplicationFlags a, ReplicationFlags b) {
        return static_cast<ReplicationFlags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
    }

    inline bool HasFlag(ReplicationFlags flags, ReplicationFlags flag) {
        return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
    }

    //==========================================================================
    // NETWORK TRANSFORM COMPONENT
    //==========================================================================

    struct NetworkTransformComponent {
        //----------------------------------------------------------------------
        // Network Identity
        //----------------------------------------------------------------------
        
        uint32_t NetworkId = 0;         // Unique network ID (assigned by server)
        uint32_t OwnerClientId = 0;     // Client who owns this entity (0 = server)
        uint16_t PrefabId = 0;          // Prefab/archetype ID for spawning
        uint16_t Generation = 0;        // Generation counter for ID reuse

        //----------------------------------------------------------------------
        // Replication Settings
        //----------------------------------------------------------------------

        NetworkOwnership Ownership = NetworkOwnership::Server;
        ReplicationPriority Priority = ReplicationPriority::Normal;
        ReplicationFlags Flags = ReplicationFlags::DefaultFlags;
        NetworkInterpolationMode InterpolationMode = NetworkInterpolationMode::Linear;

        //----------------------------------------------------------------------
        // Update Thresholds (to avoid sending tiny changes)
        //----------------------------------------------------------------------

        float PositionThreshold = 0.001f;       // Min position delta to replicate
        float RotationThreshold = 0.001f;       // Min rotation delta (radians)
        float ScaleThreshold = 0.001f;          // Min scale delta to replicate
        float VelocityThreshold = 0.01f;        // Min velocity delta to replicate

        //----------------------------------------------------------------------
        // Timing
        //----------------------------------------------------------------------

        float SendRate = 20.0f;                 // Desired updates per second
        float AccumulatedTime = 0.0f;           // Time since last send
        float InterpolationDelay = 0.1f;        // Interpolation buffer time (100ms)

        //----------------------------------------------------------------------
        // State Tracking
        //----------------------------------------------------------------------

        uint32_t LastSentSequence = 0;          // Last sequence number sent
        uint32_t LastReceivedSequence = 0;      // Last sequence number received
        uint64_t LastSendTimestamp = 0;         // When we last sent an update
        uint64_t LastReceiveTimestamp = 0;      // When we last received an update

        //----------------------------------------------------------------------
        // Cached Previous State (for delta compression)
        //----------------------------------------------------------------------

        Math::Vec3 LastSentPosition{ 0.0f };
        Math::Quat LastSentRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        Math::Vec3 LastSentScale{ 1.0f };
        Math::Vec3 LastSentVelocity{ 0.0f };
        Math::Vec3 LastSentAngularVelocity{ 0.0f };

        //----------------------------------------------------------------------
        // Interpolation Buffer (for remote entities)
        //----------------------------------------------------------------------

        static constexpr size_t MAX_SNAPSHOTS = 4;

        struct Snapshot {
            Math::Vec3 Position{ 0.0f };
            Math::Quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
            Math::Vec3 Scale{ 1.0f };
            Math::Vec3 Velocity{ 0.0f };
            uint64_t Timestamp = 0;
            uint32_t Sequence = 0;
            bool Valid = false;
        };

        Snapshot Snapshots[MAX_SNAPSHOTS] = {};
        uint8_t SnapshotHead = 0;               // Circular buffer head
        uint8_t SnapshotCount = 0;              // Number of valid snapshots

        //----------------------------------------------------------------------
        // Status Flags
        //----------------------------------------------------------------------

        bool IsDirty = true;                    // Transform changed since last send
        bool IsSpawned = false;                 // Entity has been spawned on network
        bool IsLocallyControlled = false;       // This client controls this entity
        bool NeedsFullSync = true;              // Next update should be full sync

        //----------------------------------------------------------------------
        // Methods
        //----------------------------------------------------------------------

        NetworkTransformComponent() = default;

        explicit NetworkTransformComponent(uint32_t networkId)
            : NetworkId(networkId) {}

        NetworkTransformComponent(uint32_t networkId, NetworkOwnership ownership)
            : NetworkId(networkId), Ownership(ownership) {}

        // Check if this entity should be replicated
        bool ShouldReplicate() const {
            return NetworkId != 0 && IsSpawned;
        }

        // Check if local machine is authoritative
        bool IsAuthoritative(uint32_t localClientId) const {
            if (Ownership == NetworkOwnership::Server) {
                return localClientId == 0; // Server has clientId 0
            }
            return OwnerClientId == localClientId;
        }

        // Add a received snapshot to the interpolation buffer
        void AddSnapshot(const Math::Vec3& position, const Math::Quat& rotation,
                        const Math::Vec3& scale, const Math::Vec3& velocity,
                        uint64_t timestamp, uint32_t sequence) {
            // Only add if sequence is newer
            if (SnapshotCount > 0) {
                uint8_t lastIdx = (SnapshotHead + SnapshotCount - 1) % MAX_SNAPSHOTS;
                if (sequence <= Snapshots[lastIdx].Sequence) {
                    return; // Out of order, ignore
                }
            }

            // Insert at end of buffer
            uint8_t idx = (SnapshotHead + SnapshotCount) % MAX_SNAPSHOTS;
            if (SnapshotCount == MAX_SNAPSHOTS) {
                // Buffer full, overwrite oldest
                SnapshotHead = (SnapshotHead + 1) % MAX_SNAPSHOTS;
            } else {
                SnapshotCount++;
            }

            Snapshots[idx].Position = position;
            Snapshots[idx].Rotation = rotation;
            Snapshots[idx].Scale = scale;
            Snapshots[idx].Velocity = velocity;
            Snapshots[idx].Timestamp = timestamp;
            Snapshots[idx].Sequence = sequence;
            Snapshots[idx].Valid = true;

            LastReceivedSequence = sequence;
            LastReceiveTimestamp = timestamp;
        }

        // Get interpolated transform at given timestamp
        bool GetInterpolatedTransform(uint64_t currentTime, Math::Vec3& outPosition,
                                     Math::Quat& outRotation, Math::Vec3& outScale) const {
            if (SnapshotCount < 2) {
                if (SnapshotCount == 1) {
                    const Snapshot& s = Snapshots[SnapshotHead];
                    outPosition = s.Position;
                    outRotation = s.Rotation;
                    outScale = s.Scale;
                    return true;
                }
                return false;
            }

            // Target time is current time minus interpolation delay
            uint64_t targetTime = currentTime - static_cast<uint64_t>(InterpolationDelay * 1000000.0f);

            // Find two snapshots to interpolate between
            const Snapshot* s0 = nullptr;
            const Snapshot* s1 = nullptr;

            for (uint8_t i = 0; i < SnapshotCount - 1; i++) {
                uint8_t idx0 = (SnapshotHead + i) % MAX_SNAPSHOTS;
                uint8_t idx1 = (SnapshotHead + i + 1) % MAX_SNAPSHOTS;

                if (Snapshots[idx0].Timestamp <= targetTime && 
                    Snapshots[idx1].Timestamp >= targetTime) {
                    s0 = &Snapshots[idx0];
                    s1 = &Snapshots[idx1];
                    break;
                }
            }

            // If no suitable pair found, use latest
            if (!s0 || !s1) {
                uint8_t latest = (SnapshotHead + SnapshotCount - 1) % MAX_SNAPSHOTS;
                outPosition = Snapshots[latest].Position;
                outRotation = Snapshots[latest].Rotation;
                outScale = Snapshots[latest].Scale;
                return true;
            }

            // Calculate interpolation factor
            float duration = static_cast<float>(s1->Timestamp - s0->Timestamp);
            float elapsed = static_cast<float>(targetTime - s0->Timestamp);
            float t = (duration > 0.0f) ? glm::clamp(elapsed / duration, 0.0f, 1.0f) : 1.0f;

            // Interpolate
            outPosition = glm::mix(s0->Position, s1->Position, t);
            outRotation = glm::slerp(s0->Rotation, s1->Rotation, t);
            outScale = glm::mix(s0->Scale, s1->Scale, t);

            return true;
        }

        // Check if transform delta exceeds thresholds
        bool HasSignificantChange(const Math::Vec3& position, const Math::Quat& rotation,
                                 const Math::Vec3& scale, const Math::Vec3& velocity) const {
            if (NeedsFullSync) return true;

            if (HasFlag(Flags, ReplicationFlags::Position)) {
                if (glm::length(position - LastSentPosition) > PositionThreshold)
                    return true;
            }

            if (HasFlag(Flags, ReplicationFlags::Rotation)) {
                float dotProduct = glm::abs(glm::dot(rotation, LastSentRotation));
                if (1.0f - dotProduct > RotationThreshold)
                    return true;
            }

            if (HasFlag(Flags, ReplicationFlags::Scale)) {
                if (glm::length(scale - LastSentScale) > ScaleThreshold)
                    return true;
            }

            if (HasFlag(Flags, ReplicationFlags::Velocity)) {
                if (glm::length(velocity - LastSentVelocity) > VelocityThreshold)
                    return true;
            }

            return false;
        }

        // Mark as sent with current values
        void MarkSent(const Math::Vec3& position, const Math::Quat& rotation,
                     const Math::Vec3& scale, const Math::Vec3& velocity,
                     uint32_t sequence, uint64_t timestamp) {
            LastSentPosition = position;
            LastSentRotation = rotation;
            LastSentScale = scale;
            LastSentVelocity = velocity;
            LastSentSequence = sequence;
            LastSendTimestamp = timestamp;
            IsDirty = false;
            NeedsFullSync = false;
        }

        // Reset interpolation buffer
        void ClearSnapshots() {
            for (auto& s : Snapshots) {
                s.Valid = false;
            }
            SnapshotHead = 0;
            SnapshotCount = 0;
        }

        // Calculate update interval based on send rate
        float GetSendInterval() const {
            return (SendRate > 0.0f) ? (1.0f / SendRate) : 0.05f;
        }

        // Check if enough time has passed for next update
        bool ShouldSendUpdate(float deltaTime) {
            AccumulatedTime += deltaTime;
            float interval = GetSendInterval();
            if (AccumulatedTime >= interval) {
                AccumulatedTime -= interval;
                return true;
            }
            return false;
        }
    };

} // namespace ECS
} // namespace Core
