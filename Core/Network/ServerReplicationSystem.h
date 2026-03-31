#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/NetworkTransformComponent.h"
#include "Core/ECS/Components/RigidBodyComponent.h"
#include "Core/Network/NetworkServer.h"
#include "Core/Network/NetworkPackets.h"
#include <unordered_map>
#include <vector>
#include <queue>
#include <functional>

namespace Core {
namespace Network {

    //==========================================================================
    // REPLICATION CONFIGURATION
    //==========================================================================

    struct ReplicationConfig {
        uint32_t TickRate = 60;                     // Server tick rate (Hz)
        uint32_t MaxEntitiesPerPacket = 32;         // Max entities per snapshot packet
        uint32_t MaxPacketSize = 1200;              // Max packet size (MTU-safe)
        float LowPrioritySendRate = 10.0f;          // Low priority updates per second
        float NormalPrioritySendRate = 20.0f;       // Normal priority updates per second
        float HighPrioritySendRate = 40.0f;         // High priority updates per second
        float CriticalPrioritySendRate = 60.0f;     // Critical priority updates per second
        bool EnableDeltaCompression = true;         // Use delta compression
        bool EnableInterestManagement = false;      // Enable area-of-interest filtering
        float InterestRadius = 100.0f;              // Radius for interest management
    };

    //==========================================================================
    // ENTITY REPLICATION STATE
    //==========================================================================

    // Per-entity replication tracking
    struct EntityReplicationState {
        uint32_t NetworkId = 0;
        uint32_t LastSentTick = 0;
        uint32_t LastAckedTick = 0;
        float AccumulatedTime = 0.0f;
        bool NeedsFullSync = true;
        bool IsSpawned = false;

        // Last sent state for delta compression
        Math::Vec3 LastPosition{ 0.0f };
        Math::Quat LastRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        Math::Vec3 LastScale{ 1.0f };
        Math::Vec3 LastVelocity{ 0.0f };
    };

    // Per-client replication state
    struct ClientReplicationState {
        uint32_t ClientId = 0;
        uint32_t LastAckedTick = 0;
        std::unordered_map<uint32_t, EntityReplicationState> EntityStates;
        std::vector<uint32_t> PendingSpawns;    // Entities to spawn for this client
        std::vector<uint32_t> PendingDestroys;  // Entities to destroy for this client
        Math::Vec3 LastKnownPosition{ 0.0f };   // For interest management
        bool IsReady = false;
    };

    //==========================================================================
    // REPLICATION EVENTS
    //==========================================================================

    // Event types for replication callbacks
    enum class ReplicationEventType : uint8_t {
        EntitySpawned,
        EntityDestroyed,
        OwnershipChanged,
        FullSyncRequested
    };

    struct ReplicationEvent {
        ReplicationEventType Type;
        uint32_t NetworkId = 0;
        uint32_t ClientId = 0;          // Affected client (0 = all)
        uint32_t NewOwnerId = 0;        // For ownership changes
    };

    using ReplicationEventCallback = std::function<void(const ReplicationEvent&)>;

    //==========================================================================
    // SERVER REPLICATION SYSTEM
    //==========================================================================

    class ServerReplicationSystem {
    public:
        ServerReplicationSystem();
        ~ServerReplicationSystem();

        // Prevent copying
        ServerReplicationSystem(const ServerReplicationSystem&) = delete;
        ServerReplicationSystem& operator=(const ServerReplicationSystem&) = delete;

        //----------------------------------------------------------------------
        // Initialization
        //----------------------------------------------------------------------

        // Initialize with server reference
        void Initialize(NetworkServer* server, const ReplicationConfig& config = ReplicationConfig());

        // Shutdown
        void Shutdown();

        // Check if initialized
        bool IsInitialized() const { return m_Server != nullptr; }

        //----------------------------------------------------------------------
        // Entity Registration
        //----------------------------------------------------------------------

        // Register an entity for network replication (returns NetworkId)
        uint32_t RegisterEntity(entt::entity entity, ECS::Scene& scene, uint32_t ownerClientId = 0);

        // Unregister an entity from replication
        void UnregisterEntity(uint32_t networkId);

        // Get entity handle by network ID
        entt::entity GetEntityByNetworkId(uint32_t networkId) const;

        // Get network ID by entity handle
        uint32_t GetNetworkIdByEntity(entt::entity entity) const;

        // Check if entity is registered
        bool IsEntityRegistered(uint32_t networkId) const;

        //----------------------------------------------------------------------
        // Client Management
        //----------------------------------------------------------------------

        // Register a client for replication
        void RegisterClient(uint32_t clientId);

        // Unregister a client
        void UnregisterClient(uint32_t clientId);

        // Mark client as ready to receive replication
        void SetClientReady(uint32_t clientId, bool ready);

        // Update client position for interest management
        void UpdateClientPosition(uint32_t clientId, const Math::Vec3& position);

        //----------------------------------------------------------------------
        // Main Update Loop
        //----------------------------------------------------------------------

        // Main replication tick - call this every server tick
        void Update(ECS::Scene& scene, float deltaTime);

        //----------------------------------------------------------------------
        // Ownership
        //----------------------------------------------------------------------

        // Transfer ownership of an entity to a client
        void TransferOwnership(uint32_t networkId, uint32_t newOwnerId);

        // Get owner of an entity
        uint32_t GetEntityOwner(uint32_t networkId) const;

        //----------------------------------------------------------------------
        // Forced Sync
        //----------------------------------------------------------------------

        // Force a full sync of an entity to all clients
        void ForceFullSync(uint32_t networkId);

        // Force a full sync of an entity to a specific client
        void ForceFullSyncToClient(uint32_t networkId, uint32_t clientId);

        // Request full world state sync for a client
        void RequestFullWorldSync(uint32_t clientId);

        //----------------------------------------------------------------------
        // Statistics
        //----------------------------------------------------------------------

        uint32_t GetCurrentTick() const { return m_CurrentTick; }
        uint32_t GetRegisteredEntityCount() const { return static_cast<uint32_t>(m_NetworkIdToEntity.size()); }
        uint32_t GetRegisteredClientCount() const { return static_cast<uint32_t>(m_ClientStates.size()); }
        uint64_t GetTotalBytesSent() const { return m_TotalBytesSent; }
        uint64_t GetTotalPacketsSent() const { return m_TotalPacketsSent; }

        //----------------------------------------------------------------------
        // Configuration
        //----------------------------------------------------------------------

        const ReplicationConfig& GetConfig() const { return m_Config; }
        void SetConfig(const ReplicationConfig& config) { m_Config = config; }

        //----------------------------------------------------------------------
        // Callbacks
        //----------------------------------------------------------------------

        void SetEventCallback(ReplicationEventCallback callback) { m_EventCallback = std::move(callback); }

    private:
        //----------------------------------------------------------------------
        // Internal Methods
        //----------------------------------------------------------------------

        // Generate unique network ID
        uint32_t GenerateNetworkId();

        // Get send rate for priority level
        float GetSendRateForPriority(ECS::ReplicationPriority priority) const;

        // Check if entity is in client's area of interest
        bool IsInClientInterest(uint32_t clientId, const Math::Vec3& entityPosition) const;

        // Serialize entity transform to packet data
        void SerializeEntityTransform(const ECS::TransformComponent& transform,
                                     const ECS::NetworkTransformComponent& netTransform,
                                     const ECS::RigidBodyComponent* rigidBody,
                                     NetTransform& outNetTransform);

        //----------------------------------------------------------------------
        // Packet Building
        //----------------------------------------------------------------------

        // Build and send spawn packets for pending spawns
        void ProcessPendingSpawns(ECS::Scene& scene);

        // Build and send destroy packets for pending destroys
        void ProcessPendingDestroys();

        // Build and send transform sync packets
        void ProcessTransformUpdates(ECS::Scene& scene, float deltaTime);

        // Send a transform sync packet to a client
        void SendTransformPacket(uint32_t clientId, const std::vector<NetTransform>& transforms);

        // Send entity spawn packet
        void SendEntitySpawnPacket(uint32_t clientId, uint32_t networkId, ECS::Scene& scene);

        // Send entity destroy packet
        void SendEntityDestroyPacket(uint32_t clientId, uint32_t networkId, uint32_t reason);

        //----------------------------------------------------------------------
        // Priority Queue
        //----------------------------------------------------------------------

        struct PendingUpdate {
            uint32_t NetworkId = 0;
            uint32_t ClientId = 0;
            float Priority = 0.0f;

            bool operator<(const PendingUpdate& other) const {
                return Priority < other.Priority; // Higher priority first
            }
        };

        // Calculate update priority for an entity
        float CalculateUpdatePriority(const ECS::NetworkTransformComponent& netTransform,
                                     const EntityReplicationState& state,
                                     float accumulatedTime) const;

        //----------------------------------------------------------------------
        // Event Dispatch
        //----------------------------------------------------------------------

        void DispatchEvent(const ReplicationEvent& event);

    private:
        NetworkServer* m_Server = nullptr;
        ReplicationConfig m_Config;

        // Tick counter
        uint32_t m_CurrentTick = 0;

        // Network ID management
        uint32_t m_NextNetworkId = 1;
        std::unordered_map<uint32_t, entt::entity> m_NetworkIdToEntity;
        std::unordered_map<entt::entity, uint32_t> m_EntityToNetworkId;

        // Per-client replication state
        std::unordered_map<uint32_t, ClientReplicationState> m_ClientStates;

        // Statistics
        uint64_t m_TotalBytesSent = 0;
        uint64_t m_TotalPacketsSent = 0;

        // Packet buffer for building outgoing packets
        std::vector<uint8_t> m_PacketBuffer;

        // Callback
        ReplicationEventCallback m_EventCallback;
    };

} // namespace Network
} // namespace Core
