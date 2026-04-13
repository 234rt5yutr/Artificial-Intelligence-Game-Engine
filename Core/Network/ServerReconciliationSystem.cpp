#include "Core/Network/ServerReconciliationSystem.h"
#include "Core/Log.h"
#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"
#include "Core/Network/NetworkHash.h"
#include "Core/Network/Replay/NetworkReplayRecorder.h"
#include "Core/Network/Rollback/RollbackCoordinator.h"
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

    ServerReconciliationSystem::ServerReconciliationSystem()
    {
        m_PacketBuffer.reserve(256);
    }

    ServerReconciliationSystem::~ServerReconciliationSystem()
    {
        Shutdown();
    }

    //==========================================================================
    // Initialization
    //==========================================================================

    void ServerReconciliationSystem::Initialize(NetworkServer* server, const ReconciliationConfig& config)
    {
        if (m_Server) {
            LOG_WARN("ServerReconciliationSystem already initialized");
            return;
        }

        m_Server = server;
        m_Config = config;
        m_CurrentTick = 0;
        m_TotalCorrections = 0;
        m_TotalCheatDetections = 0;
        m_AveragePositionError = 0.0f;

        LOG_INFO("ServerReconciliationSystem initialized (tolerance: {:.3f}m, blend rate: {:.2f})",
                m_Config.PositionTolerance, m_Config.CorrectionBlendRate);
    }

    void ServerReconciliationSystem::Shutdown()
    {
        if (!m_Server) {
            return;
        }

        m_ClientStates.clear();
        m_Server = nullptr;

        LOG_INFO("ServerReconciliationSystem shutdown");
    }

    //==========================================================================
    // Client Registration
    //==========================================================================

    void ServerReconciliationSystem::RegisterClient(uint32_t clientId, entt::entity playerEntity, uint32_t networkId)
    {
        if (m_ClientStates.find(clientId) != m_ClientStates.end()) {
            LOG_WARN("Client {} already registered for reconciliation", clientId);
            return;
        }

        ClientReconciliationState& state = m_ClientStates[clientId];
        state.ClientId = clientId;
        state.PlayerEntity = playerEntity;
        state.NetworkId = networkId;
        state.LastProcessedSequence = 0;
        state.ExpectedNextSequence = 1;

        LOG_DEBUG("Registered client {} (entity: {}, networkId: {}) for reconciliation",
                 clientId, static_cast<uint32_t>(playerEntity), networkId);
    }

    void ServerReconciliationSystem::UnregisterClient(uint32_t clientId)
    {
        auto it = m_ClientStates.find(clientId);
        if (it == m_ClientStates.end()) {
            return;
        }

        m_ClientStates.erase(it);
        LOG_DEBUG("Unregistered client {} from reconciliation", clientId);
    }

    bool ServerReconciliationSystem::IsClientRegistered(uint32_t clientId) const
    {
        return m_ClientStates.find(clientId) != m_ClientStates.end();
    }

    ClientReconciliationState* ServerReconciliationSystem::GetClientState(uint32_t clientId)
    {
        auto it = m_ClientStates.find(clientId);
        if (it != m_ClientStates.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const ClientReconciliationState* ServerReconciliationSystem::GetClientState(uint32_t clientId) const
    {
        auto it = m_ClientStates.find(clientId);
        if (it != m_ClientStates.end()) {
            return &it->second;
        }
        return nullptr;
    }

    //==========================================================================
    // Input Processing
    //==========================================================================

    void ServerReconciliationSystem::ReceiveInput(uint32_t clientId, const ClientInputPacket& packet,
                                                  const InputSample* samples, uint32_t sampleCount)
    {
        // SECURITY: Validate packet ClientId matches connection's client ID
        if (packet.ClientId != clientId) {
            LOG_WARN("Client {} attempted to send input as client {} - rejecting",
                     clientId, packet.ClientId);
            return;
        }

        // SECURITY: Validate sample count against protocol limits
        if (sampleCount > MAX_INPUTS_PER_PACKET) {
            LOG_WARN("Client {} sent too many inputs: {} (max: {})",
                     clientId, sampleCount, MAX_INPUTS_PER_PACKET);
            return;
        }

        auto it = m_ClientStates.find(clientId);
        if (it == m_ClientStates.end()) {
            LOG_WARN("Received input from unregistered client");
            return;
        }

        ClientReconciliationState& state = it->second;
        uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        // Buffer each input sample
        for (uint32_t i = 0; i < sampleCount; i++) {
            // SECURITY: Check buffer size BEFORE insertion
            if (state.InputBuffer.size() >= m_Config.InputBufferSize) {
                LOG_WARN("Input buffer full for client");
                break;
            }

            const InputSample& sample = samples[i];

            // Skip if already processed
            if (sample.InputSequence <= state.LastProcessedSequence) {
                continue;
            }

            // Skip duplicates
            bool isDuplicate = false;
            for (const auto& buffered : state.InputBuffer) {
                if (buffered.Sample.InputSequence == sample.InputSequence) {
                    isDuplicate = true;
                    break;
                }
            }
            if (isDuplicate) {
                continue;
            }

            // Add to buffer
            BufferedInput buffered;
            buffered.Sample = sample;
            buffered.ReceivedTimestamp = now;
            buffered.Processed = false;

            // Insert in sequence order
            auto insertPos = std::lower_bound(state.InputBuffer.begin(), state.InputBuffer.end(),
                buffered, [](const BufferedInput& a, const BufferedInput& b) {
                    return a.Sample.InputSequence < b.Sample.InputSequence;
                });
            state.InputBuffer.insert(insertPos, buffered);
        }

        state.LastInputTimestamp = now;
    }

    void ServerReconciliationSystem::ProcessInputs(ECS::Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        for (auto& [clientId, state] : m_ClientStates) {
            ProcessClientInputs(scene, state, deltaTime);
        }
    }

    void ServerReconciliationSystem::ProcessClientInputs(ECS::Scene& scene, 
                                                         ClientReconciliationState& state, 
                                                         float deltaTime)
    {
        if (state.InputBuffer.empty()) {
            return;
        }

        uint32_t processedCount = 0;

        // Process inputs in sequence order
        for (auto& buffered : state.InputBuffer) {
            if (buffered.Processed) {
                continue;
            }

            // Check if this is the expected sequence
            if (buffered.Sample.InputSequence != state.ExpectedNextSequence) {
                // Check for timeout on missing inputs
                uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                float waitTimeMs = (now - buffered.ReceivedTimestamp) / 1000.0f;

                if (waitTimeMs < m_Config.InputTimeoutMs) {
                    break; // Wait for missing input
                }

                // Skip missing inputs and continue
                LOG_DEBUG("Skipping missing inputs {} to {} for client {}",
                         state.ExpectedNextSequence, buffered.Sample.InputSequence - 1, state.ClientId);
                state.ExpectedNextSequence = buffered.Sample.InputSequence;
            }

            // Validate input
            float inputDeltaTime = buffered.Sample.DeltaTimeMs / 1000.0f;
            ValidationResult result = ValidateInput(state, buffered.Sample, inputDeltaTime);

            if (result == ValidationResult::Valid) {
                // Simulate input
                SimulateInput(scene, state, buffered.Sample, inputDeltaTime);
                state.TotalInputsProcessed++;
            } else if (result == ValidationResult::Rejected) {
                // Cheat detected
                m_TotalCheatDetections++;
                state.SuspiciousInputCount++;

                ReconciliationEvent event;
                event.Type = ReconciliationEventType::CheatDetected;
                event.ClientId = state.ClientId;
                event.NetworkId = state.NetworkId;
                event.Reason = result;
                DispatchEvent(event);

                LOG_WARN("Rejected input {} from client {} (reason: {})", 
                        buffered.Sample.InputSequence, state.ClientId, static_cast<int>(result));
            } else {
                // Input dropped
                ReconciliationEvent event;
                event.Type = ReconciliationEventType::InputDropped;
                event.ClientId = state.ClientId;
                event.Reason = result;
                DispatchEvent(event);
            }

            buffered.Processed = true;
            state.LastProcessedSequence = buffered.Sample.InputSequence;
            state.ExpectedNextSequence = buffered.Sample.InputSequence + 1;
            processedCount++;

            if (processedCount >= m_Config.MaxInputsPerTick) {
                break;
            }
        }

        (void)deltaTime; // Used in validation
    }

    //==========================================================================
    // Validation
    //==========================================================================

    ValidationResult ServerReconciliationSystem::ValidateInput(const ClientReconciliationState& state,
                                                               const InputSample& input, 
                                                               float deltaTime)
    {
        // Check sequence
        if (input.InputSequence <= state.LastProcessedSequence) {
            return ValidationResult::OutOfSequence;
        }

        // Check timing (deltaTime should be reasonable)
        if (deltaTime <= 0.0f || deltaTime > 0.5f) {
            return ValidationResult::TimingViolation;
        }

        // Check for speed hacks if anti-cheat enabled
        if (m_Config.EnableAntiCheat) {
            // Movement axes should be normalized
            float moveLength = std::sqrt(input.MoveX * input.MoveX + input.MoveY * input.MoveY);
            if (moveLength > 1.5f) { // Allow some tolerance
                return ValidationResult::SpeedViolation;
            }
        }

        return ValidationResult::Valid;
    }

    bool ServerReconciliationSystem::IsPositionValid(const Math::Vec3& reported, 
                                                     const Math::Vec3& expected) const
    {
        float distance = glm::length(reported - expected);
        return distance <= m_Config.PositionTolerance;
    }

    //==========================================================================
    // Simulation
    //==========================================================================

    void ServerReconciliationSystem::SimulateInput(ECS::Scene& scene, ClientReconciliationState& state,
                                                   const InputSample& input, float deltaTime)
    {
        if (!m_SimulationCallback) {
            LOG_WARN("No simulation callback set");
            return;
        }

        if (state.PlayerEntity == entt::null) {
            return;
        }

        // Run server simulation
        Math::Vec3 newPosition;
        Math::Quat newRotation;
        Math::Vec3 newVelocity;

        m_SimulationCallback(scene, state.PlayerEntity, input, deltaTime,
                            newPosition, newRotation, newVelocity);

        // Check for cheating (speed hack detection)
        if (m_Config.EnableAntiCheat) {
            float speed = glm::length(newVelocity);
            if (speed > state.MaxObservedSpeed * m_Config.SpeedHackThreshold && 
                state.TotalInputsProcessed > 10) {
                state.SpeedViolationCount++;
                if (state.SpeedViolationCount > 5) {
                    LOG_WARN("Speed hack suspected for client {} (speed: {:.2f})", 
                            state.ClientId, speed);
                }
            }
            state.MaxObservedSpeed = std::max(state.MaxObservedSpeed, speed);
        }

        // Store authoritative state
        state.AuthoritativePosition = newPosition;
        state.AuthoritativeRotation = newRotation;
        state.AuthoritativeVelocity = newVelocity;
    }

    //==========================================================================
    // Corrections
    //==========================================================================

    void ServerReconciliationSystem::CalculateCorrection(ClientReconciliationState& state,
                                                         const Math::Vec3& clientPosition,
                                                         const Math::Quat& clientRotation)
    {
        // Calculate position error
        Math::Vec3 positionError = state.AuthoritativePosition - clientPosition;
        float errorMagnitude = glm::length(positionError);

        // Update running average
        float alpha = 0.1f;
        state.AveragePositionError = state.AveragePositionError * (1.0f - alpha) + errorMagnitude * alpha;

        // Update global average
        m_AveragePositionError = m_AveragePositionError * (1.0f - alpha) + errorMagnitude * alpha;

        // Check if correction needed
        if (errorMagnitude > m_Config.PositionTolerance) {
            state.PendingCorrection = positionError;
            state.PendingRotationCorrection = state.AuthoritativeRotation * glm::inverse(clientRotation);
            state.NeedsCorrection = true;
            state.NeedsHardCorrection = (errorMagnitude > m_Config.HardCorrectionThreshold);

            if (state.NeedsHardCorrection) {
                LOG_DEBUG("Hard correction needed for client {} (error: {:.3f}m)", 
                         state.ClientId, errorMagnitude);
            }
        } else {
            state.NeedsCorrection = false;
            state.NeedsHardCorrection = false;
        }

        state.ClientReportedPosition = clientPosition;
        state.ClientReportedRotation = clientRotation;
    }

    void ServerReconciliationSystem::ApplyCorrections(ECS::Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        for (auto& [clientId, state] : m_ClientStates) {
            if (!state.NeedsCorrection) {
                continue;
            }

            if (state.NeedsHardCorrection || !m_Config.EnableSoftCorrections) {
                ApplyHardCorrection(scene, state);
            } else {
                ApplySoftCorrection(scene, state, deltaTime);
            }
        }
    }

    void ServerReconciliationSystem::ApplySoftCorrection(ECS::Scene& scene, 
                                                         ClientReconciliationState& state, 
                                                         float deltaTime)
    {
        auto& registry = scene.GetRegistry();
        auto* transform = registry.try_get<ECS::TransformComponent>(state.PlayerEntity);
        
        if (!transform) {
            return;
        }

        // Blend towards authoritative position
        float blendFactor = m_Config.CorrectionBlendRate * deltaTime * 60.0f; // Normalize to 60fps
        blendFactor = std::min(blendFactor, 1.0f);

        transform->Position = glm::mix(transform->Position, state.AuthoritativePosition, blendFactor);
        
        Math::Quat currentRotation(transform->Rotation);
        Math::Quat blendedRotation = glm::slerp(currentRotation, state.AuthoritativeRotation, blendFactor);
        transform->Rotation = glm::eulerAngles(blendedRotation);
        
        transform->IsDirty = true;

        // Check if correction is complete
        float remainingError = glm::length(state.AuthoritativePosition - transform->Position);
        if (remainingError < m_Config.PositionTolerance * 0.5f) {
            state.NeedsCorrection = false;
            state.PendingCorrection = Math::Vec3(0.0f);
        }

        m_TotalCorrections++;
        state.TotalCorrections++;

        ReconciliationEvent event;
        event.Type = ReconciliationEventType::CorrectionApplied;
        event.ClientId = state.ClientId;
        event.NetworkId = state.NetworkId;
        event.ErrorMagnitude = remainingError;
        DispatchEvent(event);
    }

    void ServerReconciliationSystem::ApplyHardCorrection(ECS::Scene& scene, 
                                                         ClientReconciliationState& state)
    {
        auto& registry = scene.GetRegistry();
        auto* transform = registry.try_get<ECS::TransformComponent>(state.PlayerEntity);
        
        if (!transform) {
            return;
        }

        // Snap to authoritative position
        transform->Position = state.AuthoritativePosition;
        transform->Rotation = glm::eulerAngles(state.AuthoritativeRotation);
        transform->IsDirty = true;

        state.NeedsCorrection = false;
        state.NeedsHardCorrection = false;
        state.PendingCorrection = Math::Vec3(0.0f);

        m_TotalCorrections++;
        state.TotalCorrections++;

        ReconciliationEvent event;
        event.Type = ReconciliationEventType::HardCorrectionApplied;
        event.ClientId = state.ClientId;
        event.NetworkId = state.NetworkId;
        event.ErrorMagnitude = glm::length(state.PendingCorrection);
        DispatchEvent(event);

        LOG_DEBUG("Applied hard correction to client {}", state.ClientId);
    }

    void ServerReconciliationSystem::ForceHardCorrection(uint32_t clientId)
    {
        auto it = m_ClientStates.find(clientId);
        if (it != m_ClientStates.end()) {
            it->second.NeedsCorrection = true;
            it->second.NeedsHardCorrection = true;
        }
    }

    //==========================================================================
    // Acknowledgments
    //==========================================================================

    void ServerReconciliationSystem::SendAcknowledgments(ECS::Scene& scene)
    {
        PROFILE_FUNCTION();

        for (auto& [clientId, state] : m_ClientStates) {
            if (state.LastProcessedSequence > 0) {
                SendAckPacket(clientId, state);
            }
        }

        (void)scene;
    }

    void ServerReconciliationSystem::SendAckPacket(uint32_t clientId, const ClientReconciliationState& state)
    {
        if (!m_Server || !m_Server->IsRunning()) {
            return;
        }

        InputAckPacket packet;
        packet.Header.Type = PacketType::InputAck;
        packet.Header.Flags = 0;
        packet.Header.PayloadSize = sizeof(InputAckPacket) - sizeof(PacketHeader);
        packet.Header.SequenceNumber = m_CurrentTick;
        packet.Header.Timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        packet.ClientId = clientId;
        packet.AckedInputSequence = state.LastProcessedSequence;
        packet.ServerTick = m_CurrentTick;

        packet.ResultPosition.X = state.AuthoritativePosition.x;
        packet.ResultPosition.Y = state.AuthoritativePosition.y;
        packet.ResultPosition.Z = state.AuthoritativePosition.z;

        packet.ResultRotation.X = state.AuthoritativeRotation.x;
        packet.ResultRotation.Y = state.AuthoritativeRotation.y;
        packet.ResultRotation.Z = state.AuthoritativeRotation.z;
        packet.ResultRotation.W = state.AuthoritativeRotation.w;

        m_Server->SendToClient(clientId, &packet, sizeof(packet), k_nSteamNetworkingSend_UnreliableNoDelay);
    }

    //==========================================================================
    // Update Loop
    //==========================================================================

    void ServerReconciliationSystem::Update(ECS::Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_Server || !m_Server->IsRunning()) {
            return;
        }

        m_CurrentTick++;
        NetworkDiagnosticsState::Get().SetLastServerTick(m_CurrentTick);

        const std::string& sessionId = m_Server->GetSessionId();
        if (!sessionId.empty()) {
            NetworkReplayRecorder::Get().RecordAuthoritativeMarker(sessionId, m_CurrentTick, "authoritative");

            std::vector<uint32_t> sortedClientIds;
            sortedClientIds.reserve(m_ClientStates.size());
            for (const auto& [clientId, state] : m_ClientStates) {
                (void)state;
                sortedClientIds.push_back(clientId);
            }
            std::sort(sortedClientIds.begin(), sortedClientIds.end());

            uint64_t snapshotHash = HashStringFNV1a(sessionId, false);
            snapshotHash = HashCombineFNV1a(snapshotHash, m_CurrentTick);
            for (const uint32_t clientId : sortedClientIds) {
                const auto clientIt = m_ClientStates.find(clientId);
                if (clientIt == m_ClientStates.end()) {
                    continue;
                }
                const ClientReconciliationState& state = clientIt->second;
                snapshotHash = HashCombineFNV1a(snapshotHash, clientId);
                snapshotHash = HashCombineFNV1a(snapshotHash, state.NetworkId);
                snapshotHash = HashCombineFNV1a(snapshotHash, state.LastProcessedSequence);
                snapshotHash = HashCombineFNV1a(snapshotHash, HashValueFNV1a(state.AuthoritativePosition.x));
                snapshotHash = HashCombineFNV1a(snapshotHash, HashValueFNV1a(state.AuthoritativePosition.y));
                snapshotHash = HashCombineFNV1a(snapshotHash, HashValueFNV1a(state.AuthoritativePosition.z));
                snapshotHash = HashCombineFNV1a(snapshotHash, HashValueFNV1a(state.AuthoritativeRotation.x));
                snapshotHash = HashCombineFNV1a(snapshotHash, HashValueFNV1a(state.AuthoritativeRotation.y));
                snapshotHash = HashCombineFNV1a(snapshotHash, HashValueFNV1a(state.AuthoritativeRotation.z));
                snapshotHash = HashCombineFNV1a(snapshotHash, HashValueFNV1a(state.AuthoritativeRotation.w));
            }

            RollbackSnapshotRecord snapshot;
            snapshot.SessionId = sessionId;
            snapshot.FrameTick = m_CurrentTick;
            snapshot.SnapshotHash = snapshotHash;
            snapshot.EntityCount = static_cast<uint32_t>(m_ClientStates.size());
            snapshot.SnapshotSizeBytes = static_cast<uint32_t>(m_ClientStates.size() * sizeof(ClientReconciliationState));
            RollbackCoordinator::Get().RecordAuthoritativeSnapshot(snapshot);
        }

        // Process buffered inputs
        ProcessInputs(scene, deltaTime);

        // Apply corrections
        ApplyCorrections(scene, deltaTime);

        // Send acknowledgments
        SendAcknowledgments(scene);

        // Cleanup old inputs
        for (auto& [clientId, state] : m_ClientStates) {
            CleanupOldInputs(state);
        }
    }

    //==========================================================================
    // Internal Methods
    //==========================================================================

    void ServerReconciliationSystem::CleanupOldInputs(ClientReconciliationState& state)
    {
        // Remove processed inputs older than a threshold
        while (!state.InputBuffer.empty() && state.InputBuffer.front().Processed) {
            state.InputBuffer.pop_front();
        }
    }

    bool ServerReconciliationSystem::DetectCheating(ClientReconciliationState& state, 
                                                    const InputSample& input, 
                                                    float deltaTime)
    {
        if (!m_Config.EnableAntiCheat) {
            return false;
        }

        // Check for abnormal input frequency
        if (deltaTime < 0.001f) { // Less than 1ms between inputs
            state.SuspiciousInputCount++;
            return state.SuspiciousInputCount > 10;
        }

        // Check for impossible movement values
        float moveLength = std::sqrt(input.MoveX * input.MoveX + input.MoveY * input.MoveY);
        if (moveLength > 1.5f) {
            state.SuspiciousInputCount++;
            return state.SuspiciousInputCount > 5;
        }

        return false;
    }

    void ServerReconciliationSystem::DispatchEvent(const ReconciliationEvent& event)
    {
        if (m_EventCallback) {
            m_EventCallback(event);
        }
    }

} // namespace Network
} // namespace Core
