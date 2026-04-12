#include "Core/Network/ClientReplicationSystem.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include <algorithm>
#include <chrono>
#include <cstring>

// Use engine core log macros
#define LOG_INFO    ENGINE_CORE_INFO
#define LOG_WARN    ENGINE_CORE_WARN
#define LOG_DEBUG   ENGINE_CORE_TRACE
#define LOG_ERROR   ENGINE_CORE_ERROR

namespace Core {
namespace Network {

    ClientReplicationSystem::ClientReplicationSystem()
    {
    }

    ClientReplicationSystem::~ClientReplicationSystem()
    {
        Shutdown();
    }

    //==========================================================================
    // Initialization
    //==========================================================================

    void ClientReplicationSystem::Initialize(NetworkClient* client, const ClientReplicationConfig& config)
    {
        if (m_Client) {
            LOG_WARN("ClientReplicationSystem already initialized");
            return;
        }

        m_Client = client;
        m_Config = config;
        m_LocalClientId = 0;
        m_LastServerTick = 0;
        m_TotalBytesReceived = 0;
        m_TotalPacketsReceived = 0;
        m_AverageLatency = 0.0f;
        m_ServerWorldOriginOffset = Math::Vec3(0.0f);
        m_ServerWorldOriginSequence = 0;

        LOG_INFO("ClientReplicationSystem initialized (interpolation delay: {}ms)", 
                m_Config.InterpolationDelay * 1000.0f);
    }

    void ClientReplicationSystem::Shutdown()
    {
        if (!m_Client) {
            return;
        }

        m_ProxyEntities.clear();
        m_EntityToNetworkId.clear();
        m_LocalEntities.clear();
        m_Client = nullptr;

        LOG_INFO("ClientReplicationSystem shutdown");
    }

    //==========================================================================
    // Main Update Loop
    //==========================================================================

    void ClientReplicationSystem::Update(ECS::Scene& /*scene*/, float /*deltaTime*/)
    {
        PROFILE_FUNCTION();

        if (!m_Client) {
            return;
        }

        // Update local client ID if connected
        if (m_Client->IsConnected()) {
            m_LocalClientId = m_Client->GetClientId();
        }

        // Update time estimate
        // The server timestamp from packets helps us estimate server time
        if (m_LastServerTimestamp > 0) {
            uint64_t localTime = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            
            // Simple time sync: assume server time is local time + offset
            // In a real implementation, you'd use a more sophisticated algorithm
            m_ServerTimeOffset = m_LastServerTimestamp - localTime + 
                                static_cast<uint64_t>(m_AverageLatency * 1000000.0f);
        }
    }

    void ClientReplicationSystem::ApplyInterpolation(ECS::Scene& scene, float /*deltaTime*/)
    {
        PROFILE_FUNCTION();

        if (!m_Config.EnableInterpolation) {
            return;
        }

        auto& registry = scene.GetRegistry();
        (void)GetRenderTime(); // For future use in extrapolation

        // Iterate over all proxy entities and interpolate their transforms
        for (auto& [networkId, proxyState] : m_ProxyEntities) {
            if (!proxyState.IsSpawned || proxyState.LocalEntity == entt::null) {
                continue;
            }

            // Skip locally owned entities (handled by prediction)
            if (proxyState.IsLocallyOwned) {
                continue;
            }

            // Get the entity's components
            auto* transform = registry.try_get<ECS::TransformComponent>(proxyState.LocalEntity);
            auto* netTransform = registry.try_get<ECS::NetworkTransformComponent>(proxyState.LocalEntity);

            if (!transform || !netTransform) {
                continue;
            }

            // Get interpolated transform
            Math::Vec3 interpPosition;
            Math::Quat interpRotation;
            Math::Vec3 interpScale;

            if (GetInterpolatedTransform(proxyState, *netTransform, interpPosition, interpRotation, interpScale)) {
                // Check if we should snap or interpolate
                float distance = glm::length(interpPosition - transform->Position);
                
                if (distance > m_Config.SnapThreshold) {
                    // Snap to position (too far, likely a teleport)
                    transform->Position = interpPosition;
                    transform->Rotation = glm::eulerAngles(interpRotation);
                    transform->Scale = interpScale;
                } else {
                    // Apply interpolated values
                    transform->Position = interpPosition;
                    transform->Rotation = glm::eulerAngles(interpRotation);
                    transform->Scale = interpScale;
                }

                transform->IsDirty = true;
            }
        }
    }

    //==========================================================================
    // Packet Processing
    //==========================================================================

    void ClientReplicationSystem::ProcessPacket(ECS::Scene& scene, const void* data, uint32_t size)
    {
        if (size < sizeof(PacketHeader)) {
            LOG_WARN("Received packet too small for header ({} bytes)", size);
            return;
        }

        const PacketHeader* header = static_cast<const PacketHeader*>(data);

        // SECURITY: Validate PayloadSize matches actual data
        if (header->PayloadSize > MAX_PAYLOAD_SIZE) {
            LOG_WARN("Packet payload size exceeds maximum ({} > {})", 
                     header->PayloadSize, MAX_PAYLOAD_SIZE);
            return;
        }
        uint32_t expectedTotalSize = sizeof(PacketHeader) + header->PayloadSize;
        if (size < expectedTotalSize) {
            LOG_WARN("Packet size mismatch (claimed: {}, actual: {})", expectedTotalSize, size);
            return;
        }

        m_TotalBytesReceived += size;
        m_TotalPacketsReceived++;

        // Update server timestamp for time sync
        if (header->Timestamp > m_LastServerTimestamp) {
            m_LastServerTimestamp = header->Timestamp;
        }

        // Route to appropriate handler
        switch (header->Type) {
            case PacketType::TransformSync:
                HandleTransformSync(scene, data, size);
                break;

            case PacketType::EntitySpawn:
                HandleEntitySpawn(scene, data, size);
                break;

            case PacketType::EntityDestroy:
                HandleEntityDestroy(scene, data, size);
                break;

            case PacketType::WorldSnapshot:
                HandleWorldSnapshot(scene, data, size);
                break;

            default:
                // SECURITY: Log unknown packet types for monitoring
                LOG_WARN("Unknown packet type received: {}", static_cast<int>(header->Type));
                break;
        }
    }

    //==========================================================================
    // Packet Handlers
    //==========================================================================

    void ClientReplicationSystem::HandleTransformSync(ECS::Scene& scene, const void* data, uint32_t size)
    {
        if (size < sizeof(TransformSyncPacket)) {
            LOG_WARN("TransformSync packet too small");
            return;
        }

        const TransformSyncPacket* packet = static_cast<const TransformSyncPacket*>(data);
        
        // SECURITY: Validate TransformCount against protocol limits
        if (packet->TransformCount > MAX_ENTITIES_PER_SNAPSHOT) {
            LOG_WARN("TransformSync has invalid TransformCount: {} (max: {})", 
                     packet->TransformCount, MAX_ENTITIES_PER_SNAPSHOT);
            return;
        }

        // Update server tick
        if (packet->ServerTick > m_LastServerTick) {
            m_LastServerTick = packet->ServerTick;
        }
        m_ServerWorldOriginOffset = Math::Vec3(
            packet->WorldOriginOffset.X,
            packet->WorldOriginOffset.Y,
            packet->WorldOriginOffset.Z);
        m_ServerWorldOriginSequence = packet->WorldOriginSequence;

        // Calculate expected size with overflow check
        size_t transformDataSize = static_cast<size_t>(packet->TransformCount) * sizeof(NetTransform);
        size_t expectedSize = sizeof(TransformSyncPacket) + transformDataSize;
        if (size < expectedSize || size > MAX_PAYLOAD_SIZE + sizeof(PacketHeader)) {
            LOG_WARN("TransformSync packet size mismatch (expected {}, got {})", expectedSize, size);
            return;
        }

        // Process each transform
        const NetTransform* transforms = reinterpret_cast<const NetTransform*>(
            static_cast<const uint8_t*>(data) + sizeof(TransformSyncPacket));

        for (uint8_t i = 0; i < packet->TransformCount; i++) {
            const NetTransform& netTrans = transforms[i];
            uint32_t networkId = netTrans.EntityId.Id;

            UpdateProxyTransform(scene, networkId, netTrans, packet->ServerTick, packet->Header.Timestamp);
        }
    }

    void ClientReplicationSystem::HandleEntitySpawn(ECS::Scene& scene, const void* data, uint32_t size)
    {
        if (size < sizeof(EntitySpawnPacket)) {
            LOG_WARN("EntitySpawn packet too small");
            return;
        }

        const EntitySpawnPacket* packet = static_cast<const EntitySpawnPacket*>(data);
        uint32_t networkId = packet->EntityId.Id;

        // Check if we already have this entity
        auto it = m_ProxyEntities.find(networkId);
        if (it != m_ProxyEntities.end() && it->second.IsSpawned) {
            LOG_DEBUG("Entity {} already exists, updating", networkId);
            return;
        }

        // Create the proxy entity
        entt::entity localEntity = CreateProxyEntity(scene, *packet);

        if (localEntity != entt::null) {
            LOG_DEBUG("Spawned proxy entity {} (NetworkId: {}, Owner: {})", 
                     static_cast<uint32_t>(localEntity), networkId, packet->OwnerClientId);

            // Dispatch spawn event
            ClientReplicationEvent event;
            event.Type = ClientReplicationEventType::EntitySpawned;
            event.NetworkId = networkId;
            event.LocalEntity = localEntity;
            DispatchEvent(event);
        }
    }

    void ClientReplicationSystem::HandleEntityDestroy(ECS::Scene& scene, const void* data, uint32_t size)
    {
        if (size < sizeof(EntityDestroyPacket)) {
            LOG_WARN("EntityDestroy packet too small");
            return;
        }

        const EntityDestroyPacket* packet = static_cast<const EntityDestroyPacket*>(data);
        uint32_t networkId = packet->EntityId.Id;

        DestroyProxyEntity(scene, networkId, packet->Reason);
    }

    void ClientReplicationSystem::HandleWorldSnapshot(ECS::Scene& scene, const void* data, uint32_t size)
    {
        if (size < sizeof(WorldSnapshotPacket)) {
            LOG_WARN("WorldSnapshot packet too small");
            return;
        }

        const WorldSnapshotPacket* packet = static_cast<const WorldSnapshotPacket*>(data);

        // SECURITY: Validate EntityCount against protocol limits
        if (packet->EntityCount > MAX_ENTITIES_PER_SNAPSHOT) {
            LOG_WARN("WorldSnapshot has invalid EntityCount: {} (max: {})",
                     packet->EntityCount, MAX_ENTITIES_PER_SNAPSHOT);
            return;
        }

        // Update server tick
        if (packet->ServerTick > m_LastServerTick) {
            m_LastServerTick = packet->ServerTick;
        }

        // Process entity transforms
        size_t offset = sizeof(WorldSnapshotPacket);
        const uint8_t* bytes = static_cast<const uint8_t*>(data);

        // World snapshot contains NetTransform structs - calculate safe count
        uint32_t maxTransforms = (size - sizeof(WorldSnapshotPacket)) / sizeof(NetTransform);
        uint32_t transformCount = std::min(maxTransforms, packet->EntityCount);
        // SECURITY: Cap at protocol limit to prevent excessive processing
        transformCount = std::min(transformCount, MAX_ENTITIES_PER_SNAPSHOT);

        for (uint32_t i = 0; i < transformCount && offset + sizeof(NetTransform) <= size; i++) {
            const NetTransform* netTrans = reinterpret_cast<const NetTransform*>(bytes + offset);
            
            uint32_t networkId = netTrans->EntityId.Id;
            
            auto it = m_ProxyEntities.find(networkId);
            if (it == m_ProxyEntities.end()) {
                // Entity not yet spawned - this can happen if snapshot arrives before spawn
                LOG_DEBUG("Snapshot contains unknown entity, skipping");
            } else {
                // Update existing entity
                UpdateProxyTransform(scene, networkId, *netTrans, packet->ServerTick, packet->Header.Timestamp);
            }

            offset += sizeof(NetTransform);
        }

        // Dispatch full sync event
        ClientReplicationEvent event;
        event.Type = ClientReplicationEventType::FullSyncReceived;
        DispatchEvent(event);

        LOG_DEBUG("Processed world snapshot (tick: {}, entities: {})", packet->ServerTick, transformCount);
    }

    //==========================================================================
    // Entity Management
    //==========================================================================

    entt::entity ClientReplicationSystem::CreateProxyEntity(ECS::Scene& scene, const EntitySpawnPacket& packet)
    {
        uint32_t networkId = packet.EntityId.Id;

        // Convert network types to engine types
        Math::Vec3 position(packet.Position.X, packet.Position.Y, packet.Position.Z);
        Math::Quat rotation(packet.Rotation.W, packet.Rotation.X, packet.Rotation.Y, packet.Rotation.Z);
        Math::Vec3 scale(packet.Scale.X, packet.Scale.Y, packet.Scale.Z);

        entt::entity localEntity = entt::null;

        // Use spawn callback if provided
        if (m_SpawnCallback) {
            localEntity = m_SpawnCallback(scene, networkId, packet.PrefabId, position, rotation, scale);
        } else {
            // Default: create a basic entity with transform and network components
            localEntity = scene.GetRegistry().create();

            // Add TransformComponent
            auto& transform = scene.GetRegistry().emplace<ECS::TransformComponent>(localEntity);
            transform.Position = position;
            transform.Rotation = glm::eulerAngles(rotation);
            transform.Scale = scale;
            transform.IsDirty = true;
        }

        if (localEntity == entt::null) {
            LOG_ERROR("Failed to create proxy entity for NetworkId {}", networkId);
            return entt::null;
        }

        // Add or update NetworkTransformComponent
        auto& netTransform = scene.GetRegistry().emplace_or_replace<ECS::NetworkTransformComponent>(localEntity);
        netTransform.NetworkId = networkId;
        netTransform.OwnerClientId = packet.OwnerClientId;
        netTransform.PrefabId = static_cast<uint16_t>(packet.PrefabId);
        netTransform.Generation = packet.EntityId.Generation;
        netTransform.IsSpawned = true;
        netTransform.IsLocallyControlled = (packet.OwnerClientId == m_LocalClientId && m_LocalClientId != 0);
        netTransform.Ownership = (packet.OwnerClientId == 0) ? 
                                  ECS::NetworkOwnership::Server : 
                                  (packet.OwnerClientId == m_LocalClientId ? 
                                   ECS::NetworkOwnership::LocalPlayer : 
                                   ECS::NetworkOwnership::RemotePlayer);

        // Create proxy state
        ProxyEntityState& proxyState = m_ProxyEntities[networkId];
        proxyState.NetworkId = networkId;
        proxyState.LocalEntity = localEntity;
        proxyState.OwnerClientId = packet.OwnerClientId;
        proxyState.PrefabId = static_cast<uint16_t>(packet.PrefabId);
        proxyState.IsSpawned = true;
        proxyState.IsLocallyOwned = (packet.OwnerClientId == m_LocalClientId && m_LocalClientId != 0);
        proxyState.TargetPosition = position;
        proxyState.TargetRotation = rotation;
        proxyState.TargetScale = scale;

        // Add to lookup map
        m_EntityToNetworkId[localEntity] = networkId;

        return localEntity;
    }

    void ClientReplicationSystem::DestroyProxyEntity(ECS::Scene& scene, uint32_t networkId, uint32_t reason)
    {
        auto it = m_ProxyEntities.find(networkId);
        if (it == m_ProxyEntities.end()) {
            return;
        }

        entt::entity localEntity = it->second.LocalEntity;

        // Dispatch destroy event before removal
        ClientReplicationEvent event;
        event.Type = ClientReplicationEventType::EntityDestroyed;
        event.NetworkId = networkId;
        event.LocalEntity = localEntity;
        DispatchEvent(event);

        // Use destroy callback if provided
        if (m_DestroyCallback && localEntity != entt::null) {
            m_DestroyCallback(scene, localEntity, reason);
        } else if (localEntity != entt::null && scene.GetRegistry().valid(localEntity)) {
            // Default: destroy the entity
            scene.GetRegistry().destroy(localEntity);
        }

        // Remove from lookup maps
        m_EntityToNetworkId.erase(localEntity);
        m_ProxyEntities.erase(it);

        LOG_DEBUG("Destroyed proxy entity (NetworkId: {}, Reason: {})", networkId, reason);
    }

    void ClientReplicationSystem::UpdateProxyTransform(ECS::Scene& scene, uint32_t networkId, 
                                                       const NetTransform& transform,
                                                       uint32_t serverTick, uint64_t timestamp)
    {
        auto it = m_ProxyEntities.find(networkId);
        if (it == m_ProxyEntities.end()) {
            // Entity not yet spawned - this can happen if transform arrives before spawn
            return;
        }

        ProxyEntityState& proxyState = it->second;

        // Skip if this is an older update
        if (serverTick < proxyState.LastReceivedSequence) {
            return;
        }

        // Update target state
        proxyState.TargetPosition = Math::Vec3(
            transform.Position.X + m_ServerWorldOriginOffset.x,
            transform.Position.Y + m_ServerWorldOriginOffset.y,
            transform.Position.Z + m_ServerWorldOriginOffset.z);
        proxyState.TargetRotation = Math::Quat(transform.Rotation.W, transform.Rotation.X, 
                                               transform.Rotation.Y, transform.Rotation.Z);
        proxyState.TargetScale = Math::Vec3(transform.Scale.X, transform.Scale.Y, transform.Scale.Z);
        proxyState.TargetVelocity = Math::Vec3(transform.Velocity.X, transform.Velocity.Y, transform.Velocity.Z);
        proxyState.LastReceivedSequence = serverTick;
        proxyState.LastReceivedTimestamp = timestamp;

        // Update NetworkTransformComponent snapshot buffer
        if (proxyState.LocalEntity != entt::null) {
            auto* netTransform = scene.GetRegistry().try_get<ECS::NetworkTransformComponent>(proxyState.LocalEntity);
            if (netTransform && m_Config.EnableSnapshots) {
                netTransform->AddSnapshot(
                    proxyState.TargetPosition,
                    proxyState.TargetRotation,
                    proxyState.TargetScale,
                    proxyState.TargetVelocity,
                    timestamp,
                    serverTick
                );
            }
        }
    }

    //==========================================================================
    // Interpolation
    //==========================================================================

    bool ClientReplicationSystem::GetInterpolatedTransform(const ProxyEntityState& state,
                                                          const ECS::NetworkTransformComponent& netTransform,
                                                          Math::Vec3& outPosition, Math::Quat& outRotation,
                                                          Math::Vec3& outScale) const
    {
        // Try to get interpolated transform from the component's snapshot buffer
        uint64_t renderTime = GetRenderTime();
        
        if (netTransform.GetInterpolatedTransform(renderTime, outPosition, outRotation, outScale)) {
            return true;
        }

        // Fallback to target state
        outPosition = state.TargetPosition;
        outRotation = state.TargetRotation;
        outScale = state.TargetScale;
        return true;
    }

    //==========================================================================
    // Entity Queries
    //==========================================================================

    entt::entity ClientReplicationSystem::GetEntityByNetworkId(uint32_t networkId) const
    {
        auto it = m_ProxyEntities.find(networkId);
        if (it != m_ProxyEntities.end()) {
            return it->second.LocalEntity;
        }

        auto localIt = m_LocalEntities.find(networkId);
        if (localIt != m_LocalEntities.end()) {
            return localIt->second;
        }

        return entt::null;
    }

    uint32_t ClientReplicationSystem::GetNetworkIdByEntity(entt::entity entity) const
    {
        auto it = m_EntityToNetworkId.find(entity);
        if (it != m_EntityToNetworkId.end()) {
            return it->second;
        }
        return 0;
    }

    bool ClientReplicationSystem::IsLocallyOwned(uint32_t networkId) const
    {
        // Check local entities first
        if (m_LocalEntities.find(networkId) != m_LocalEntities.end()) {
            return true;
        }

        // Check proxy entities
        auto it = m_ProxyEntities.find(networkId);
        if (it != m_ProxyEntities.end()) {
            return it->second.IsLocallyOwned;
        }

        return false;
    }

    std::vector<uint32_t> ClientReplicationSystem::GetAllProxyNetworkIds() const
    {
        std::vector<uint32_t> ids;
        ids.reserve(m_ProxyEntities.size());
        for (const auto& [networkId, state] : m_ProxyEntities) {
            ids.push_back(networkId);
        }
        return ids;
    }

    //==========================================================================
    // Local Entity Registration
    //==========================================================================

    void ClientReplicationSystem::RegisterLocalEntity(entt::entity entity, uint32_t networkId)
    {
        m_LocalEntities[networkId] = entity;
        m_EntityToNetworkId[entity] = networkId;
        LOG_DEBUG("Registered local entity {} with NetworkId {}", static_cast<uint32_t>(entity), networkId);
    }

    void ClientReplicationSystem::UnregisterLocalEntity(uint32_t networkId)
    {
        auto it = m_LocalEntities.find(networkId);
        if (it != m_LocalEntities.end()) {
            m_EntityToNetworkId.erase(it->second);
            m_LocalEntities.erase(it);
            LOG_DEBUG("Unregistered local entity with NetworkId {}", networkId);
        }
    }

    //==========================================================================
    // Time Synchronization
    //==========================================================================

    uint64_t ClientReplicationSystem::GetEstimatedServerTime() const
    {
        uint64_t localTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        return localTime + m_ServerTimeOffset;
    }

    uint64_t ClientReplicationSystem::GetRenderTime() const
    {
        uint64_t serverTime = GetEstimatedServerTime();
        uint64_t delayMicros = static_cast<uint64_t>(m_Config.InterpolationDelay * 1000000.0f);
        
        if (serverTime > delayMicros) {
            return serverTime - delayMicros;
        }
        return 0;
    }

    //==========================================================================
    // Event Dispatch
    //==========================================================================

    void ClientReplicationSystem::DispatchEvent(const ClientReplicationEvent& event)
    {
        if (m_EventCallback) {
            m_EventCallback(event);
        }
    }

} // namespace Network
} // namespace Core
