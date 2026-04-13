#include "Core/Network/ServerReplicationSystem.h"
#include "Core/Log.h"
#include "Core/Network/Policies/ReplicationPolicyRegistry.h"
#include "Core/Profile.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>

// Use engine core log macros
#define LOG_INFO    ENGINE_CORE_INFO
#define LOG_WARN    ENGINE_CORE_WARN
#define LOG_DEBUG   ENGINE_CORE_TRACE
#define LOG_ERROR   ENGINE_CORE_ERROR

namespace Core {
namespace Network {

    ServerReplicationSystem::ServerReplicationSystem()
    {
        m_PacketBuffer.reserve(2048);
    }

    ServerReplicationSystem::~ServerReplicationSystem()
    {
        Shutdown();
    }

    //==========================================================================
    // Initialization
    //==========================================================================

    void ServerReplicationSystem::Initialize(NetworkServer* server, const ReplicationConfig& config)
    {
        if (m_Server) {
            LOG_WARN("ServerReplicationSystem already initialized");
            return;
        }

        m_Server = server;
        m_Config = config;
        m_CurrentTick = 0;
        m_NextNetworkId = 1;
        m_TotalBytesSent = 0;
        m_TotalPacketsSent = 0;
        m_WorldOriginOffset = Math::Vec3(0.0f);
        m_WorldOriginSequence = 0;

        LOG_INFO("ServerReplicationSystem initialized (tick rate: {} Hz)", m_Config.TickRate);
    }

    void ServerReplicationSystem::Shutdown()
    {
        if (!m_Server) {
            return;
        }

        m_NetworkIdToEntity.clear();
        m_EntityToNetworkId.clear();
        m_ClientStates.clear();
        m_Server = nullptr;

        LOG_INFO("ServerReplicationSystem shutdown");
    }

    //==========================================================================
    // Entity Registration
    //==========================================================================

    uint32_t ServerReplicationSystem::RegisterEntity(entt::entity entity, ECS::Scene& scene, uint32_t ownerClientId)
    {
        // Check if already registered
        auto it = m_EntityToNetworkId.find(entity);
        if (it != m_EntityToNetworkId.end()) {
            LOG_WARN("Entity already registered with NetworkId {}", it->second);
            return it->second;
        }

        // Generate network ID
        uint32_t networkId = GenerateNetworkId();

        // Register mappings
        m_NetworkIdToEntity[networkId] = entity;
        m_EntityToNetworkId[entity] = networkId;

        // Update the NetworkTransformComponent if present
        auto& registry = scene.GetRegistry();
        auto* netTransform = registry.try_get<ECS::NetworkTransformComponent>(entity);
        if (netTransform) {
            netTransform->NetworkId = networkId;
            netTransform->OwnerClientId = ownerClientId;
            netTransform->IsSpawned = true;
            netTransform->NeedsFullSync = true;
        }

        // Queue spawn for all ready clients
        for (auto& [clientId, clientState] : m_ClientStates) {
            if (clientState.IsReady) {
                clientState.PendingSpawns.push_back(networkId);

                // Initialize entity state for this client
                EntityReplicationState& entityState = clientState.EntityStates[networkId];
                entityState.NetworkId = networkId;
                entityState.NeedsFullSync = true;
                entityState.IsSpawned = false;
            }
        }

        LOG_DEBUG("Registered entity for replication (NetworkId: {}, Owner: {})", networkId, ownerClientId);

        // Dispatch event
        ReplicationEvent event;
        event.Type = ReplicationEventType::EntitySpawned;
        event.NetworkId = networkId;
        event.ClientId = 0; // All clients
        DispatchEvent(event);

        return networkId;
    }

    void ServerReplicationSystem::UnregisterEntity(uint32_t networkId)
    {
        auto it = m_NetworkIdToEntity.find(networkId);
        if (it == m_NetworkIdToEntity.end()) {
            LOG_WARN("Attempted to unregister unknown NetworkId {}", networkId);
            return;
        }

        entt::entity entity = it->second;

        // Queue destroy for all clients
        for (auto& [clientId, clientState] : m_ClientStates) {
            clientState.PendingDestroys.push_back(networkId);
            clientState.EntityStates.erase(networkId);
        }

        // Remove mappings
        m_EntityToNetworkId.erase(entity);
        m_NetworkIdToEntity.erase(it);

        LOG_DEBUG("Unregistered entity from replication (NetworkId: {})", networkId);

        // Dispatch event
        ReplicationEvent event;
        event.Type = ReplicationEventType::EntityDestroyed;
        event.NetworkId = networkId;
        event.ClientId = 0;
        DispatchEvent(event);
    }

    entt::entity ServerReplicationSystem::GetEntityByNetworkId(uint32_t networkId) const
    {
        auto it = m_NetworkIdToEntity.find(networkId);
        if (it != m_NetworkIdToEntity.end()) {
            return it->second;
        }
        return entt::null;
    }

    uint32_t ServerReplicationSystem::GetNetworkIdByEntity(entt::entity entity) const
    {
        auto it = m_EntityToNetworkId.find(entity);
        if (it != m_EntityToNetworkId.end()) {
            return it->second;
        }
        return 0;
    }

    bool ServerReplicationSystem::IsEntityRegistered(uint32_t networkId) const
    {
        return m_NetworkIdToEntity.find(networkId) != m_NetworkIdToEntity.end();
    }

    //==========================================================================
    // Client Management
    //==========================================================================

    void ServerReplicationSystem::RegisterClient(uint32_t clientId)
    {
        if (m_ClientStates.find(clientId) != m_ClientStates.end()) {
            LOG_WARN("Client {} already registered for replication", clientId);
            return;
        }

        ClientReplicationState& state = m_ClientStates[clientId];
        state.ClientId = clientId;
        state.IsReady = false;

        LOG_DEBUG("Registered client {} for replication", clientId);
    }

    void ServerReplicationSystem::UnregisterClient(uint32_t clientId)
    {
        auto it = m_ClientStates.find(clientId);
        if (it == m_ClientStates.end()) {
            return;
        }

        m_ClientStates.erase(it);
        LOG_DEBUG("Unregistered client {} from replication", clientId);
    }

    void ServerReplicationSystem::SetClientReady(uint32_t clientId, bool ready)
    {
        auto it = m_ClientStates.find(clientId);
        if (it == m_ClientStates.end()) {
            LOG_WARN("Cannot set ready state for unknown client {}", clientId);
            return;
        }

        it->second.IsReady = ready;

        if (ready) {
            // Queue all existing entities for spawn
            for (const auto& [networkId, entity] : m_NetworkIdToEntity) {
                it->second.PendingSpawns.push_back(networkId);

                EntityReplicationState& entityState = it->second.EntityStates[networkId];
                entityState.NetworkId = networkId;
                entityState.NeedsFullSync = true;
                entityState.IsSpawned = false;
            }
            LOG_DEBUG("Client {} marked ready, queued {} entities for spawn", 
                     clientId, it->second.PendingSpawns.size());
        }
    }

    void ServerReplicationSystem::UpdateClientPosition(uint32_t clientId, const Math::Vec3& position)
    {
        auto it = m_ClientStates.find(clientId);
        if (it != m_ClientStates.end()) {
            it->second.LastKnownPosition = position;
        }
    }

    //==========================================================================
    // Main Update Loop
    //==========================================================================

    void ServerReplicationSystem::Update(ECS::Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_Server || !m_Server->IsRunning()) {
            return;
        }

        // Increment tick
        m_CurrentTick++;

        // Process pending spawns
        ProcessPendingSpawns(scene);

        // Process pending destroys
        ProcessPendingDestroys();

        // Process transform updates
        ProcessTransformUpdates(scene, deltaTime);
    }

    //==========================================================================
    // Ownership
    //==========================================================================

    void ServerReplicationSystem::TransferOwnership(uint32_t networkId, uint32_t newOwnerId)
    {
        auto it = m_NetworkIdToEntity.find(networkId);
        if (it == m_NetworkIdToEntity.end()) {
            LOG_WARN("Cannot transfer ownership of unknown NetworkId {}", networkId);
            return;
        }

        // Force full sync to ensure all clients have latest state
        ForceFullSync(networkId);

        LOG_DEBUG("Transferred ownership of NetworkId {} to client {}", networkId, newOwnerId);

        // Dispatch event
        ReplicationEvent event;
        event.Type = ReplicationEventType::OwnershipChanged;
        event.NetworkId = networkId;
        event.NewOwnerId = newOwnerId;
        DispatchEvent(event);
    }

    uint32_t ServerReplicationSystem::GetEntityOwner(uint32_t networkId) const
    {
        auto it = m_NetworkIdToEntity.find(networkId);
        if (it == m_NetworkIdToEntity.end()) {
            return 0;
        }
        // Note: actual owner stored in NetworkTransformComponent
        return 0;
    }

    //==========================================================================
    // Forced Sync
    //==========================================================================

    void ServerReplicationSystem::ForceFullSync(uint32_t networkId)
    {
        for (auto& [clientId, clientState] : m_ClientStates) {
            auto it = clientState.EntityStates.find(networkId);
            if (it != clientState.EntityStates.end()) {
                it->second.NeedsFullSync = true;
            }
        }
    }

    void ServerReplicationSystem::ForceFullSyncToClient(uint32_t networkId, uint32_t clientId)
    {
        auto clientIt = m_ClientStates.find(clientId);
        if (clientIt == m_ClientStates.end()) {
            return;
        }

        auto entityIt = clientIt->second.EntityStates.find(networkId);
        if (entityIt != clientIt->second.EntityStates.end()) {
            entityIt->second.NeedsFullSync = true;
        }
    }

    void ServerReplicationSystem::RequestFullWorldSync(uint32_t clientId)
    {
        auto it = m_ClientStates.find(clientId);
        if (it == m_ClientStates.end()) {
            return;
        }

        for (auto& [networkId, entityState] : it->second.EntityStates) {
            entityState.NeedsFullSync = true;
        }

        // Dispatch event
        ReplicationEvent event;
        event.Type = ReplicationEventType::FullSyncRequested;
        event.ClientId = clientId;
        DispatchEvent(event);

        LOG_DEBUG("Full world sync requested for client {}", clientId);
    }

    void ServerReplicationSystem::SetWorldOriginOffset(const Math::Vec3& offset)
    {
        m_WorldOriginOffset = offset;
        ++m_WorldOriginSequence;
    }

    //==========================================================================
    // Internal Methods
    //==========================================================================

    uint32_t ServerReplicationSystem::GenerateNetworkId()
    {
        return m_NextNetworkId++;
    }

    float ServerReplicationSystem::GetSendRateForPriority(ECS::ReplicationPriority priority) const
    {
        switch (priority) {
            case ECS::ReplicationPriority::Low:      return m_Config.LowPrioritySendRate;
            case ECS::ReplicationPriority::Normal:   return m_Config.NormalPrioritySendRate;
            case ECS::ReplicationPriority::High:     return m_Config.HighPrioritySendRate;
            case ECS::ReplicationPriority::Critical: return m_Config.CriticalPrioritySendRate;
            default: return m_Config.NormalPrioritySendRate;
        }
    }

    bool ServerReplicationSystem::IsInClientInterest(uint32_t clientId, const Math::Vec3& entityPosition) const
    {
        if (!m_Config.EnableInterestManagement) {
            return true; // No interest management, everything is relevant
        }

        auto it = m_ClientStates.find(clientId);
        if (it == m_ClientStates.end()) {
            return false;
        }

        const Math::Vec3 absoluteEntityPosition = entityPosition + m_WorldOriginOffset;
        float distanceSq = glm::length2(absoluteEntityPosition - it->second.LastKnownPosition);
        return distanceSq <= (m_Config.InterestRadius * m_Config.InterestRadius);
    }

    void ServerReplicationSystem::SerializeEntityTransform(
        const ECS::TransformComponent& transform,
        const ECS::NetworkTransformComponent& netTransform,
        const ECS::RigidBodyComponent* rigidBody,
        NetTransform& outNetTransform)
    {
        // Entity ID
        outNetTransform.EntityId.Id = netTransform.NetworkId;
        outNetTransform.EntityId.Generation = netTransform.Generation;
        outNetTransform.EntityId.TypeId = netTransform.PrefabId;

        // Position
        outNetTransform.Position.X = transform.Position.x;
        outNetTransform.Position.Y = transform.Position.y;
        outNetTransform.Position.Z = transform.Position.z;

        // Rotation (convert Euler to quaternion)
        Math::Quat rotation = Math::Quat(transform.Rotation);
        outNetTransform.Rotation.X = rotation.x;
        outNetTransform.Rotation.Y = rotation.y;
        outNetTransform.Rotation.Z = rotation.z;
        outNetTransform.Rotation.W = rotation.w;

        // Scale
        outNetTransform.Scale.X = transform.Scale.x;
        outNetTransform.Scale.Y = transform.Scale.y;
        outNetTransform.Scale.Z = transform.Scale.z;

        // Velocity from rigid body if available
        if (rigidBody) {
            outNetTransform.Velocity.X = rigidBody->LinearVelocity.x;
            outNetTransform.Velocity.Y = rigidBody->LinearVelocity.y;
            outNetTransform.Velocity.Z = rigidBody->LinearVelocity.z;
            outNetTransform.AngularVelocity.X = rigidBody->AngularVelocity.x;
            outNetTransform.AngularVelocity.Y = rigidBody->AngularVelocity.y;
            outNetTransform.AngularVelocity.Z = rigidBody->AngularVelocity.z;
        } else {
            outNetTransform.Velocity = { 0.0f, 0.0f, 0.0f };
            outNetTransform.AngularVelocity = { 0.0f, 0.0f, 0.0f };
        }
    }

    //==========================================================================
    // Packet Building
    //==========================================================================

    void ServerReplicationSystem::ProcessPendingSpawns(ECS::Scene& scene)
    {
        for (auto& [clientId, clientState] : m_ClientStates) {
            if (!clientState.IsReady || clientState.PendingSpawns.empty()) {
                continue;
            }

            for (uint32_t networkId : clientState.PendingSpawns) {
                SendEntitySpawnPacket(clientId, networkId, scene);

                // Mark as spawned in client state
                auto& entityState = clientState.EntityStates[networkId];
                entityState.IsSpawned = true;
            }

            clientState.PendingSpawns.clear();
        }
    }

    void ServerReplicationSystem::ProcessPendingDestroys()
    {
        for (auto& [clientId, clientState] : m_ClientStates) {
            if (!clientState.IsReady || clientState.PendingDestroys.empty()) {
                continue;
            }

            for (uint32_t networkId : clientState.PendingDestroys) {
                SendEntityDestroyPacket(clientId, networkId, 0);
            }

            clientState.PendingDestroys.clear();
        }
    }

    void ServerReplicationSystem::ProcessTransformUpdates(ECS::Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        auto& registry = scene.GetRegistry();
        const std::optional<ReplicatedPropertyPolicy> transformPolicy =
            ReplicationPolicyRegistry::Get().FindPolicy("NetworkTransformComponent", "Transform");
        const int policySendFlags = transformPolicy.has_value()
            ? ToSteamSendFlags(transformPolicy->ReliabilityClass)
            : k_nSteamNetworkingSend_UnreliableNoDelay;

        // Collect transforms to send per client
        std::unordered_map<uint32_t, std::vector<NetTransform>> clientTransforms;

        // Iterate over all networked entities
        auto view = registry.view<ECS::TransformComponent, ECS::NetworkTransformComponent>();

        for (auto entity : view) {
            auto& transform = view.get<ECS::TransformComponent>(entity);
            auto& netTransform = view.get<ECS::NetworkTransformComponent>(entity);

            // Skip if not registered or not spawned
            if (netTransform.NetworkId == 0 || !netTransform.IsSpawned) {
                continue;
            }

            // Get optional rigid body for velocity
            auto* rigidBody = registry.try_get<ECS::RigidBodyComponent>(entity);

            // Get send rate for this entity's priority
            float sendRate = transformPolicy.has_value()
                ? transformPolicy->SendRateHz
                : GetSendRateForPriority(netTransform.Priority);
            float sendInterval = (sendRate > 0.0f) ? (1.0f / sendRate) : 0.05f;

            // Check each client
            for (auto& [clientId, clientState] : m_ClientStates) {
                if (!clientState.IsReady) {
                    continue;
                }

                // Skip entities owned by this client (they send us updates)
                if (netTransform.OwnerClientId == clientId && 
                    netTransform.Ownership != ECS::NetworkOwnership::Server) {
                    continue;
                }

                // Get/create entity state for this client
                auto& entityState = clientState.EntityStates[netTransform.NetworkId];

                // Skip if not spawned yet
                if (!entityState.IsSpawned) {
                    continue;
                }

                // Check interest management
                if (!IsInClientInterest(clientId, transform.Position)) {
                    continue;
                }

                // Accumulate time
                entityState.AccumulatedTime += deltaTime;

                // Check if it's time to send
                bool shouldSend = false;

                if (entityState.NeedsFullSync) {
                    shouldSend = true;
                } else if (entityState.AccumulatedTime >= sendInterval) {
                    // Check if significant change
                    Math::Quat currentRotation(transform.Rotation);
                    Math::Vec3 velocity = rigidBody ? rigidBody->LinearVelocity : Math::Vec3(0.0f);

                    if (netTransform.HasSignificantChange(
                            transform.Position, currentRotation, transform.Scale, velocity)) {
                        shouldSend = true;
                    }
                }

                if (shouldSend) {
                    // Serialize transform
                    NetTransform netTrans;
                    SerializeEntityTransform(transform, netTransform, rigidBody, netTrans);

                    clientTransforms[clientId].push_back(netTrans);

                    // Update state
                    entityState.LastPosition = transform.Position;
                    entityState.LastRotation = Math::Quat(transform.Rotation);
                    entityState.LastScale = transform.Scale;
                    entityState.LastVelocity = rigidBody ? rigidBody->LinearVelocity : Math::Vec3(0.0f);
                    entityState.LastSentTick = m_CurrentTick;
                    entityState.AccumulatedTime = 0.0f;
                    entityState.NeedsFullSync = false;
                }
            }
        }

        // Send transform packets to each client
        for (auto& [clientId, transforms] : clientTransforms) {
            if (!transforms.empty()) {
                SendTransformPacket(clientId, transforms, policySendFlags);
            }
        }
    }

    void ServerReplicationSystem::SendTransformPacket(
        uint32_t clientId,
        const std::vector<NetTransform>& transforms,
        int sendFlags)
    {
        if (transforms.empty()) {
            return;
        }

        // Split into multiple packets if needed
        size_t transformsPerPacket = (m_Config.MaxPacketSize - sizeof(TransformSyncPacket)) / sizeof(NetTransform);
        transformsPerPacket = std::min(transformsPerPacket, static_cast<size_t>(m_Config.MaxEntitiesPerPacket));

        size_t offset = 0;
        while (offset < transforms.size()) {
            size_t count = std::min(transformsPerPacket, transforms.size() - offset);

            // Build packet
            size_t packetSize = sizeof(TransformSyncPacket) + count * sizeof(NetTransform);
            m_PacketBuffer.resize(packetSize);

            // Header
            TransformSyncPacket* packet = reinterpret_cast<TransformSyncPacket*>(m_PacketBuffer.data());
            packet->Header.Type = PacketType::TransformSync;
            packet->Header.Flags = 0;
            packet->Header.PayloadSize = static_cast<uint16_t>(count * sizeof(NetTransform));
            packet->Header.SequenceNumber = m_CurrentTick;
            packet->Header.Timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            packet->ServerTick = m_CurrentTick;
            packet->TransformCount = static_cast<uint8_t>(count);
            packet->WorldOriginOffset = {
                m_WorldOriginOffset.x,
                m_WorldOriginOffset.y,
                m_WorldOriginOffset.z
            };
            packet->WorldOriginSequence = m_WorldOriginSequence;

            // Copy transforms
            std::memcpy(m_PacketBuffer.data() + sizeof(TransformSyncPacket),
                       &transforms[offset], count * sizeof(NetTransform));

            // Send
            m_Server->SendToClient(clientId, m_PacketBuffer.data(), 
                                  static_cast<uint32_t>(packetSize),
                                  sendFlags);

            m_TotalBytesSent += packetSize;
            m_TotalPacketsSent++;

            offset += count;
        }
    }

    void ServerReplicationSystem::SendEntitySpawnPacket(uint32_t clientId, uint32_t networkId, ECS::Scene& scene)
    {
        auto entityIt = m_NetworkIdToEntity.find(networkId);
        if (entityIt == m_NetworkIdToEntity.end()) {
            return;
        }

        entt::entity entity = entityIt->second;
        auto& registry = scene.GetRegistry();

        auto* transform = registry.try_get<ECS::TransformComponent>(entity);
        auto* netTransform = registry.try_get<ECS::NetworkTransformComponent>(entity);

        if (!transform || !netTransform) {
            return;
        }

        EntitySpawnPacket packet;
        packet.Header.Type = PacketType::EntitySpawn;
        packet.Header.Flags = static_cast<uint8_t>(PacketFlags::Reliable);
        packet.Header.PayloadSize = sizeof(EntitySpawnPacket) - sizeof(PacketHeader);
        packet.Header.SequenceNumber = m_CurrentTick;
        packet.Header.Timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        packet.EntityId.Id = netTransform->NetworkId;
        packet.EntityId.Generation = netTransform->Generation;
        packet.EntityId.TypeId = netTransform->PrefabId;

        packet.OwnerClientId = netTransform->OwnerClientId;
        packet.PrefabId = netTransform->PrefabId;

        packet.Position = { transform->Position.x, transform->Position.y, transform->Position.z };

        Math::Quat rotation(transform->Rotation);
        packet.Rotation = { rotation.x, rotation.y, rotation.z, rotation.w };

        packet.Scale = { transform->Scale.x, transform->Scale.y, transform->Scale.z };

        m_Server->SendToClient(clientId, &packet, sizeof(packet), k_nSteamNetworkingSend_Reliable);

        m_TotalBytesSent += sizeof(packet);
        m_TotalPacketsSent++;

        LOG_DEBUG("Sent spawn packet for NetworkId {} to client {}", networkId, clientId);
    }

    void ServerReplicationSystem::SendEntityDestroyPacket(uint32_t clientId, uint32_t networkId, uint32_t reason)
    {
        EntityDestroyPacket packet;
        packet.Header.Type = PacketType::EntityDestroy;
        packet.Header.Flags = static_cast<uint8_t>(PacketFlags::Reliable);
        packet.Header.PayloadSize = sizeof(EntityDestroyPacket) - sizeof(PacketHeader);
        packet.Header.SequenceNumber = m_CurrentTick;
        packet.Header.Timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        packet.EntityId.Id = networkId;
        packet.EntityId.Generation = 0;
        packet.EntityId.TypeId = 0;
        packet.Reason = reason;

        m_Server->SendToClient(clientId, &packet, sizeof(packet), k_nSteamNetworkingSend_Reliable);

        m_TotalBytesSent += sizeof(packet);
        m_TotalPacketsSent++;

        LOG_DEBUG("Sent destroy packet for NetworkId {} to client {}", networkId, clientId);
    }

    //==========================================================================
    // Priority Calculation
    //==========================================================================

    float ServerReplicationSystem::CalculateUpdatePriority(
        const ECS::NetworkTransformComponent& netTransform,
        const EntityReplicationState& state,
        float accumulatedTime) const
    {
        float priority = 0.0f;

        // Base priority from component setting
        switch (netTransform.Priority) {
            case ECS::ReplicationPriority::Low:      priority = 1.0f; break;
            case ECS::ReplicationPriority::Normal:   priority = 2.0f; break;
            case ECS::ReplicationPriority::High:     priority = 3.0f; break;
            case ECS::ReplicationPriority::Critical: priority = 4.0f; break;
        }

        // Boost priority based on time since last update
        priority += accumulatedTime * 10.0f;

        // Boost if needs full sync
        if (state.NeedsFullSync) {
            priority += 10.0f;
        }

        return priority;
    }

    //==========================================================================
    // Event Dispatch
    //==========================================================================

    void ServerReplicationSystem::DispatchEvent(const ReplicationEvent& event)
    {
        if (m_EventCallback) {
            m_EventCallback(event);
        }
    }

} // namespace Network
} // namespace Core
