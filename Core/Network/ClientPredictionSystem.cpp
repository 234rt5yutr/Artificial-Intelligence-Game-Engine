#include "Core/Network/ClientPredictionSystem.h"
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

    ClientPredictionSystem::ClientPredictionSystem()
    {
        m_PacketBuffer.reserve(1024);
    }

    ClientPredictionSystem::~ClientPredictionSystem()
    {
        Shutdown();
    }

    //==========================================================================
    // Initialization
    //==========================================================================

    void ClientPredictionSystem::Initialize(NetworkClient* client, const PredictionConfig& config)
    {
        if (m_Client) {
            LOG_WARN("ClientPredictionSystem already initialized");
            return;
        }

        m_Client = client;
        m_Config = config;
        m_TotalMispredictions = 0;
        m_AverageMisprediction = 0.0f;
        m_TotalInputsSent = 0;
        m_InputSendAccumulator = 0.0f;

        LOG_INFO("ClientPredictionSystem initialized (send rate: {} Hz, max pending: {})",
                m_Config.InputSendRate, m_Config.MaxPendingInputs);
    }

    void ClientPredictionSystem::Shutdown()
    {
        if (!m_Client) {
            return;
        }

        m_PredictionStates.clear();
        m_EntityToNetworkId.clear();
        m_Client = nullptr;

        LOG_INFO("ClientPredictionSystem shutdown");
    }

    //==========================================================================
    // Entity Registration
    //==========================================================================

    void ClientPredictionSystem::RegisterEntity(entt::entity entity, uint32_t networkId)
    {
        if (m_PredictionStates.find(networkId) != m_PredictionStates.end()) {
            LOG_WARN("Entity {} already registered for prediction", networkId);
            return;
        }

        EntityPredictionState& state = m_PredictionStates[networkId];
        state.NetworkId = networkId;
        state.Entity = entity;
        state.NextInputSequence = 1;
        state.LastAckedInputSequence = 0;

        m_EntityToNetworkId[entity] = networkId;

        LOG_DEBUG("Registered entity {} (NetworkId: {}) for prediction", 
                 static_cast<uint32_t>(entity), networkId);
    }

    void ClientPredictionSystem::UnregisterEntity(uint32_t networkId)
    {
        auto it = m_PredictionStates.find(networkId);
        if (it == m_PredictionStates.end()) {
            return;
        }

        m_EntityToNetworkId.erase(it->second.Entity);
        m_PredictionStates.erase(it);

        LOG_DEBUG("Unregistered entity (NetworkId: {}) from prediction", networkId);
    }

    bool ClientPredictionSystem::IsEntityRegistered(uint32_t networkId) const
    {
        return m_PredictionStates.find(networkId) != m_PredictionStates.end();
    }

    EntityPredictionState* ClientPredictionSystem::GetPredictionState(uint32_t networkId)
    {
        auto it = m_PredictionStates.find(networkId);
        if (it != m_PredictionStates.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const EntityPredictionState* ClientPredictionSystem::GetPredictionState(uint32_t networkId) const
    {
        auto it = m_PredictionStates.find(networkId);
        if (it != m_PredictionStates.end()) {
            return &it->second;
        }
        return nullptr;
    }

    //==========================================================================
    // Input Processing
    //==========================================================================

    void ClientPredictionSystem::RecordInput(ECS::Scene& scene, uint32_t networkId, 
                                            const InputSample& rawInput, float deltaTime)
    {
        PROFILE_FUNCTION();

        auto it = m_PredictionStates.find(networkId);
        if (it == m_PredictionStates.end()) {
            LOG_WARN("Cannot record input for unregistered entity {}", networkId);
            return;
        }

        EntityPredictionState& state = it->second;

        // Check if we have too many pending inputs
        if (state.InputHistory.size() >= m_Config.MaxPendingInputs) {
            LOG_WARN("Input buffer full for entity {}, dropping oldest input", networkId);
            state.InputHistory.pop_front();
        }

        // Create predicted input
        PredictedInput predictedInput;
        predictedInput.Sample = rawInput;
        predictedInput.Sample.InputSequence = state.NextInputSequence++;
        predictedInput.Sample.DeltaTimeMs = static_cast<uint16_t>(deltaTime * 1000.0f);
        predictedInput.Timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        predictedInput.Acknowledged = false;

        // Simulate input locally and get predicted state
        SimulateInput(scene, state, predictedInput, deltaTime);

        // Store in history for reconciliation
        state.InputHistory.push_back(predictedInput);

        LOG_DEBUG("Recorded input {} for entity {} (pos: {:.2f}, {:.2f}, {:.2f})",
                 predictedInput.Sample.InputSequence, networkId,
                 predictedInput.PredictedPosition.x, predictedInput.PredictedPosition.y,
                 predictedInput.PredictedPosition.z);
    }

    void ClientPredictionSystem::SendInputs()
    {
        PROFILE_FUNCTION();

        if (!m_Client || !m_Client->IsConnected()) {
            return;
        }

        for (auto& [networkId, state] : m_PredictionStates) {
            if (state.InputHistory.empty()) {
                continue;
            }

            // Count unacknowledged inputs
            uint32_t unackedCount = 0;
            for (const auto& input : state.InputHistory) {
                if (!input.Acknowledged) {
                    unackedCount++;
                }
            }

            if (unackedCount > 0) {
                BuildAndSendInputPacket(state);
            }
        }
    }

    //==========================================================================
    // Server Acknowledgment
    //==========================================================================

    void ClientPredictionSystem::ProcessInputAck(ECS::Scene& scene, const InputAckPacket& ackPacket)
    {
        PROFILE_FUNCTION();

        // Find the entity this ack is for (assuming ClientId maps to our entity)
        // In a real implementation, you'd map ClientId to the player's entity
        for (auto& [networkId, state] : m_PredictionStates) {
            // Check if this ack is for inputs we've sent
            if (ackPacket.AckedInputSequence <= state.LastAckedInputSequence) {
                continue; // Already processed or not for us
            }

            // Extract server authoritative state
            Math::Vec3 serverPosition(
                ackPacket.ResultPosition.X,
                ackPacket.ResultPosition.Y,
                ackPacket.ResultPosition.Z
            );
            Math::Quat serverRotation(
                ackPacket.ResultRotation.W,
                ackPacket.ResultRotation.X,
                ackPacket.ResultRotation.Y,
                ackPacket.ResultRotation.Z
            );

            // Perform reconciliation
            Reconcile(scene, state, serverPosition, serverRotation, ackPacket.AckedInputSequence);

            // Update last acked
            state.LastAckedInputSequence = ackPacket.AckedInputSequence;
            state.LastServerTick = ackPacket.ServerTick;

            break; // Assume one entity per client for now
        }
    }

    //==========================================================================
    // Update Loop
    //==========================================================================

    void ClientPredictionSystem::Update(ECS::Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_Client) {
            return;
        }

        // Accumulate time for input sending
        m_InputSendAccumulator += deltaTime;
        // SECURITY: Prevent division by zero
        float safeSendRate = (m_Config.InputSendRate > 0.0f) ? m_Config.InputSendRate : 1.0f;
        float sendInterval = 1.0f / safeSendRate;

        if (m_InputSendAccumulator >= sendInterval) {
            SendInputs();
            m_InputSendAccumulator -= sendInterval;
        }

        // Clean up old inputs and smooth corrections
        for (auto& [networkId, state] : m_PredictionStates) {
            CleanupAcknowledgedInputs(state);
            SmoothCorrection(state, deltaTime);
        }

        (void)scene; // May be used for future features
    }

    void ClientPredictionSystem::ApplyVisualSmoothing(ECS::Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_Config.EnableSmoothing) {
            return;
        }

        auto& registry = scene.GetRegistry();

        for (auto& [networkId, state] : m_PredictionStates) {
            if (state.Entity == entt::null || state.CorrectionAlpha >= 1.0f) {
                continue;
            }

            auto* transform = registry.try_get<ECS::TransformComponent>(state.Entity);
            if (!transform) {
                continue;
            }

            // Apply remaining correction offset (this creates smooth visual interpolation)
            // The actual position is already correct, this just affects rendering
            float smoothingFactor = 1.0f - state.CorrectionAlpha;
            Math::Vec3 visualOffset = state.CorrectionOffset * smoothingFactor;

            // Note: In a real implementation, you'd have a separate "visual position"
            // that's offset from the actual physics position for rendering only

            (void)visualOffset; // Store in a render-only component in real implementation
            (void)deltaTime;
        }
    }

    //==========================================================================
    // Internal Methods
    //==========================================================================

    void ClientPredictionSystem::SimulateInput(ECS::Scene& scene, EntityPredictionState& state,
                                               PredictedInput& input, float deltaTime)
    {
        if (!m_SimulationCallback) {
            LOG_WARN("No simulation callback set - cannot predict input");
            return;
        }

        // Call the simulation callback to apply input
        m_SimulationCallback(
            scene,
            state.Entity,
            input.Sample,
            deltaTime,
            input.PredictedPosition,
            input.PredictedRotation,
            input.PredictedVelocity
        );
    }

    void ClientPredictionSystem::Reconcile(ECS::Scene& scene, EntityPredictionState& state,
                                           const Math::Vec3& serverPosition, 
                                           const Math::Quat& serverRotation,
                                           uint32_t ackedSequence)
    {
        PROFILE_FUNCTION();

        // Find the input that was acknowledged
        auto it = std::find_if(state.InputHistory.begin(), state.InputHistory.end(),
            [ackedSequence](const PredictedInput& input) {
                return input.Sample.InputSequence == ackedSequence;
            });

        if (it == state.InputHistory.end()) {
            // Input not found, might be already cleaned up
            return;
        }

        // Compare server state with our predicted state at that input
        Math::Vec3 predictedPosition = it->PredictedPosition;
        float mispredictionDistance = glm::length(serverPosition - predictedPosition);

        // Check if we need to correct
        if (mispredictionDistance > m_Config.CorrectionThreshold) {
            m_TotalMispredictions++;
            
            // Update running average
            float alpha = 0.1f;
            m_AverageMisprediction = m_AverageMisprediction * (1.0f - alpha) + mispredictionDistance * alpha;

            LOG_DEBUG("Misprediction detected: {:.4f}m (predicted: {:.2f},{:.2f},{:.2f} server: {:.2f},{:.2f},{:.2f})",
                     mispredictionDistance,
                     predictedPosition.x, predictedPosition.y, predictedPosition.z,
                     serverPosition.x, serverPosition.y, serverPosition.z);

            // Store correction offset for visual smoothing
            if (m_Config.EnableSmoothing && mispredictionDistance < m_Config.SnapCorrectionThreshold) {
                // Calculate the difference we need to smooth out visually
                state.CorrectionOffset = predictedPosition - serverPosition;
                state.CorrectionAlpha = 0.0f;
            } else {
                // Snap - no smoothing needed
                state.CorrectionOffset = Math::Vec3(0.0f);
                state.CorrectionAlpha = 1.0f;
            }

            // Update server authoritative state
            state.LastServerPosition = serverPosition;
            state.LastServerRotation = serverRotation;

            // Re-simulate all inputs after the acknowledged one
            ResimulateInputs(scene, state);
        }

        // Mark all inputs up to and including acked as acknowledged
        for (auto& input : state.InputHistory) {
            if (input.Sample.InputSequence <= ackedSequence) {
                input.Acknowledged = true;
            }
        }
    }

    void ClientPredictionSystem::ResimulateInputs(ECS::Scene& scene, EntityPredictionState& state)
    {
        PROFILE_FUNCTION();

        if (!m_SimulationCallback) {
            return;
        }

        auto& registry = scene.GetRegistry();
        auto* transform = registry.try_get<ECS::TransformComponent>(state.Entity);

        if (!transform) {
            return;
        }

        // Reset to server authoritative state
        transform->Position = state.LastServerPosition;
        transform->Rotation = glm::eulerAngles(state.LastServerRotation);
        transform->IsDirty = true;

        // Re-simulate all unacknowledged inputs
        for (auto& input : state.InputHistory) {
            if (!input.Acknowledged) {
                float deltaTime = input.Sample.DeltaTimeMs / 1000.0f;
                
                // Re-run simulation
                m_SimulationCallback(
                    scene,
                    state.Entity,
                    input.Sample,
                    deltaTime,
                    input.PredictedPosition,
                    input.PredictedRotation,
                    input.PredictedVelocity
                );
            }
        }

        LOG_DEBUG("Re-simulated {} unacknowledged inputs", 
                 std::count_if(state.InputHistory.begin(), state.InputHistory.end(),
                              [](const PredictedInput& i) { return !i.Acknowledged; }));
    }

    void ClientPredictionSystem::BuildAndSendInputPacket(EntityPredictionState& state)
    {
        if (!m_Client || !m_Client->IsConnected()) {
            return;
        }

        // Count inputs to send (with redundancy)
        uint32_t inputCount = 0;
        for (auto it = state.InputHistory.rbegin(); 
             it != state.InputHistory.rend() && inputCount < m_Config.InputRedundancy; 
             ++it) {
            if (!it->Acknowledged) {
                inputCount++;
            }
        }

        if (inputCount == 0) {
            return;
        }

        // Build packet
        size_t packetSize = sizeof(ClientInputPacket) + inputCount * sizeof(InputSample);
        m_PacketBuffer.resize(packetSize);

        ClientInputPacket* packet = reinterpret_cast<ClientInputPacket*>(m_PacketBuffer.data());
        packet->Header.Type = PacketType::ClientInput;
        packet->Header.Flags = 0;
        packet->Header.PayloadSize = static_cast<uint16_t>(inputCount * sizeof(InputSample));
        packet->Header.SequenceNumber = state.NextInputSequence - 1;
        packet->Header.Timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        packet->ClientId = m_Client->GetClientId();
        packet->LastAckedSequence = state.LastAckedInputSequence;
        packet->InputCount = static_cast<uint8_t>(inputCount);

        // Copy input samples (most recent first for redundancy)
        InputSample* samples = reinterpret_cast<InputSample*>(
            m_PacketBuffer.data() + sizeof(ClientInputPacket));
        
        uint32_t sampleIndex = 0;
        for (auto it = state.InputHistory.rbegin(); 
             it != state.InputHistory.rend() && sampleIndex < inputCount; 
             ++it) {
            if (!it->Acknowledged) {
                samples[sampleIndex++] = it->Sample;
            }
        }

        // Send unreliable (input is redundant)
        m_Client->Send(m_PacketBuffer.data(), static_cast<uint32_t>(packetSize),
                      k_nSteamNetworkingSend_UnreliableNoDelay);

        m_TotalInputsSent++;
    }

    void ClientPredictionSystem::CleanupAcknowledgedInputs(EntityPredictionState& state)
    {
        // Remove old acknowledged inputs, but keep some for redundancy
        while (state.InputHistory.size() > m_Config.InputRedundancy) {
            if (state.InputHistory.front().Acknowledged) {
                state.InputHistory.pop_front();
            } else {
                break;
            }
        }
    }

    void ClientPredictionSystem::SmoothCorrection(EntityPredictionState& state, float deltaTime)
    {
        if (state.CorrectionAlpha >= 1.0f) {
            return;
        }

        // Progress the smoothing
        float smoothingSpeed = 1.0f / m_Config.CorrectionSmoothTime;
        state.CorrectionAlpha += deltaTime * smoothingSpeed;
        state.CorrectionAlpha = std::min(state.CorrectionAlpha, 1.0f);

        // Decay the correction offset
        float remaining = 1.0f - state.CorrectionAlpha;
        state.CorrectionOffset *= remaining;
    }

    //==========================================================================
    // Statistics
    //==========================================================================

    uint32_t ClientPredictionSystem::GetPendingInputCount(uint32_t networkId) const
    {
        auto it = m_PredictionStates.find(networkId);
        if (it == m_PredictionStates.end()) {
            return 0;
        }

        uint32_t count = 0;
        for (const auto& input : it->second.InputHistory) {
            if (!input.Acknowledged) {
                count++;
            }
        }
        return count;
    }

} // namespace Network
} // namespace Core
