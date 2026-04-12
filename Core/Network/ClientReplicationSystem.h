#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/NetworkTransformComponent.h"
#include "Core/Network/NetworkClient.h"
#include "Core/Network/NetworkPackets.h"
#include <unordered_map>
#include <vector>
#include <functional>
#include <queue>

namespace Core {
namespace Network {

    //==========================================================================
    // CLIENT REPLICATION CONFIGURATION
    //==========================================================================

    struct ClientReplicationConfig {
        float InterpolationDelay = 0.1f;            // Interpolation buffer delay (seconds)
        float ExtrapolationLimit = 0.25f;           // Max extrapolation time (seconds)
        float SnapThreshold = 5.0f;                 // Distance threshold for snapping vs interpolating
        bool EnableInterpolation = true;            // Enable smooth interpolation
        bool EnableExtrapolation = false;           // Enable extrapolation when packets are late
        bool EnableSnapshots = true;                // Store snapshots for interpolation
        uint32_t MaxPendingSpawns = 256;            // Max pending spawn queue size
    };

    //==========================================================================
    // PROXY ENTITY STATE
    //==========================================================================

    // State tracking for a replicated proxy entity
    struct ProxyEntityState {
        uint32_t NetworkId = 0;
        entt::entity LocalEntity = entt::null;
        uint32_t OwnerClientId = 0;
        uint16_t PrefabId = 0;
        uint32_t LastReceivedSequence = 0;
        uint64_t LastReceivedTimestamp = 0;
        bool IsSpawned = false;
        bool IsLocallyOwned = false;

        // Target state for interpolation
        Math::Vec3 TargetPosition{ 0.0f };
        Math::Quat TargetRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        Math::Vec3 TargetScale{ 1.0f };
        Math::Vec3 TargetVelocity{ 0.0f };
    };

    //==========================================================================
    // CLIENT REPLICATION EVENTS
    //==========================================================================

    enum class ClientReplicationEventType : uint8_t {
        EntitySpawned,          // New proxy entity created
        EntityDestroyed,        // Proxy entity removed
        EntityUpdated,          // Transform updated
        OwnershipGained,        // Local client gained ownership
        OwnershipLost,          // Local client lost ownership
        ConnectionLost,         // Lost connection to server
        FullSyncReceived        // Received full world state
    };

    struct ClientReplicationEvent {
        ClientReplicationEventType Type;
        uint32_t NetworkId = 0;
        entt::entity LocalEntity = entt::null;
        uint32_t OldOwnerId = 0;
        uint32_t NewOwnerId = 0;
    };

    using ClientReplicationEventCallback = std::function<void(const ClientReplicationEvent&)>;

    // Callback for spawning entities - returns the created entity
    using EntitySpawnCallback = std::function<entt::entity(ECS::Scene& scene, uint32_t networkId, 
                                                           uint32_t prefabId, const Math::Vec3& position,
                                                           const Math::Quat& rotation, const Math::Vec3& scale)>;

    // Callback for destroying entities
    using EntityDestroyCallback = std::function<void(ECS::Scene& scene, entt::entity entity, uint32_t reason)>;

    //==========================================================================
    // CLIENT REPLICATION SYSTEM
    //==========================================================================

    class ClientReplicationSystem {
    public:
        ClientReplicationSystem();
        ~ClientReplicationSystem();

        // Prevent copying
        ClientReplicationSystem(const ClientReplicationSystem&) = delete;
        ClientReplicationSystem& operator=(const ClientReplicationSystem&) = delete;

        //----------------------------------------------------------------------
        // Initialization
        //----------------------------------------------------------------------

        // Initialize with client reference
        void Initialize(NetworkClient* client, const ClientReplicationConfig& config = ClientReplicationConfig());

        // Shutdown
        void Shutdown();

        // Check if initialized
        bool IsInitialized() const { return m_Client != nullptr; }

        //----------------------------------------------------------------------
        // Main Update Loop
        //----------------------------------------------------------------------

        // Process pending packets and update interpolation (call every frame)
        void Update(ECS::Scene& scene, float deltaTime);

        // Apply interpolation to all proxy entities (call after Update)
        void ApplyInterpolation(ECS::Scene& scene, float deltaTime);

        //----------------------------------------------------------------------
        // Packet Processing
        //----------------------------------------------------------------------

        // Process a received network packet (call from message callback)
        void ProcessPacket(ECS::Scene& scene, const void* data, uint32_t size);

        //----------------------------------------------------------------------
        // Entity Queries
        //----------------------------------------------------------------------

        // Get local entity by network ID
        entt::entity GetEntityByNetworkId(uint32_t networkId) const;

        // Get network ID by local entity
        uint32_t GetNetworkIdByEntity(entt::entity entity) const;

        // Check if we own an entity
        bool IsLocallyOwned(uint32_t networkId) const;

        // Get all proxy entity network IDs
        std::vector<uint32_t> GetAllProxyNetworkIds() const;

        //----------------------------------------------------------------------
        // Local Entity Registration
        //----------------------------------------------------------------------

        // Register a locally-controlled entity (for client-side prediction)
        void RegisterLocalEntity(entt::entity entity, uint32_t networkId);

        // Unregister a local entity
        void UnregisterLocalEntity(uint32_t networkId);

        //----------------------------------------------------------------------
        // Statistics
        //----------------------------------------------------------------------

        uint32_t GetProxyEntityCount() const { return static_cast<uint32_t>(m_ProxyEntities.size()); }
        uint32_t GetLocalEntityCount() const { return static_cast<uint32_t>(m_LocalEntities.size()); }
        uint32_t GetLastReceivedServerTick() const { return m_LastServerTick; }
        uint64_t GetTotalBytesReceived() const { return m_TotalBytesReceived; }
        uint64_t GetTotalPacketsReceived() const { return m_TotalPacketsReceived; }
        float GetAverageLatency() const { return m_AverageLatency; }
        const Math::Vec3& GetServerWorldOriginOffset() const { return m_ServerWorldOriginOffset; }
        uint32_t GetServerWorldOriginSequence() const { return m_ServerWorldOriginSequence; }

        //----------------------------------------------------------------------
        // Configuration
        //----------------------------------------------------------------------

        const ClientReplicationConfig& GetConfig() const { return m_Config; }
        void SetConfig(const ClientReplicationConfig& config) { m_Config = config; }

        //----------------------------------------------------------------------
        // Callbacks
        //----------------------------------------------------------------------

        void SetEventCallback(ClientReplicationEventCallback callback) { m_EventCallback = std::move(callback); }
        void SetSpawnCallback(EntitySpawnCallback callback) { m_SpawnCallback = std::move(callback); }
        void SetDestroyCallback(EntityDestroyCallback callback) { m_DestroyCallback = std::move(callback); }

        //----------------------------------------------------------------------
        // Time Synchronization
        //----------------------------------------------------------------------

        // Get estimated server time (microseconds)
        uint64_t GetEstimatedServerTime() const;

        // Get render time for interpolation (server time - interpolation delay)
        uint64_t GetRenderTime() const;

    private:
        //----------------------------------------------------------------------
        // Packet Handlers
        //----------------------------------------------------------------------

        void HandleTransformSync(ECS::Scene& scene, const void* data, uint32_t size);
        void HandleEntitySpawn(ECS::Scene& scene, const void* data, uint32_t size);
        void HandleEntityDestroy(ECS::Scene& scene, const void* data, uint32_t size);
        void HandleWorldSnapshot(ECS::Scene& scene, const void* data, uint32_t size);

        //----------------------------------------------------------------------
        // Entity Management
        //----------------------------------------------------------------------

        // Create a proxy entity from spawn packet
        entt::entity CreateProxyEntity(ECS::Scene& scene, const EntitySpawnPacket& packet);

        // Destroy a proxy entity
        void DestroyProxyEntity(ECS::Scene& scene, uint32_t networkId, uint32_t reason);

        // Update proxy entity transform from network data
        void UpdateProxyTransform(ECS::Scene& scene, uint32_t networkId, const NetTransform& transform,
                                  uint32_t serverTick, uint64_t timestamp);

        //----------------------------------------------------------------------
        // Interpolation
        //----------------------------------------------------------------------

        // Calculate interpolated transform for a proxy entity
        bool GetInterpolatedTransform(const ProxyEntityState& state, 
                                     const ECS::NetworkTransformComponent& netTransform,
                                     Math::Vec3& outPosition, Math::Quat& outRotation, 
                                     Math::Vec3& outScale) const;

        //----------------------------------------------------------------------
        // Event Dispatch
        //----------------------------------------------------------------------

        void DispatchEvent(const ClientReplicationEvent& event);

    private:
        NetworkClient* m_Client = nullptr;
        ClientReplicationConfig m_Config;

        // Local client ID (from handshake)
        uint32_t m_LocalClientId = 0;

        // Server tick tracking
        uint32_t m_LastServerTick = 0;

        // Proxy entities (remote entities replicated from server)
        std::unordered_map<uint32_t, ProxyEntityState> m_ProxyEntities;
        std::unordered_map<entt::entity, uint32_t> m_EntityToNetworkId;

        // Local entities (client-controlled, for prediction)
        std::unordered_map<uint32_t, entt::entity> m_LocalEntities;

        // Time synchronization
        uint64_t m_ServerTimeOffset = 0;        // Offset to convert local time to server time
        uint64_t m_LastServerTimestamp = 0;     // Last received server timestamp
        float m_AverageLatency = 0.0f;          // Running average of RTT/2
        Math::Vec3 m_ServerWorldOriginOffset{0.0f};
        uint32_t m_ServerWorldOriginSequence = 0;

        // Statistics
        uint64_t m_TotalBytesReceived = 0;
        uint64_t m_TotalPacketsReceived = 0;

        // Callbacks
        ClientReplicationEventCallback m_EventCallback;
        EntitySpawnCallback m_SpawnCallback;
        EntityDestroyCallback m_DestroyCallback;
    };

} // namespace Network
} // namespace Core
