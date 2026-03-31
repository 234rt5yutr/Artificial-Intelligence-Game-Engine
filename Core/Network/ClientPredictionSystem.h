#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/NetworkTransformComponent.h"
#include "Core/Network/NetworkClient.h"
#include "Core/Network/NetworkPackets.h"
#include "Core/Math/Math.h"
#include <vector>
#include <deque>
#include <functional>
#include <chrono>

namespace Core {
namespace Network {

    //==========================================================================
    // PREDICTION CONFIGURATION
    //==========================================================================

    struct PredictionConfig {
        uint32_t MaxPendingInputs = 128;            // Maximum unacknowledged inputs to store
        uint32_t InputRedundancy = 3;               // Number of past inputs to send per packet
        float InputSendRate = 60.0f;                // Input send rate (Hz)
        float MaxPredictionTime = 0.5f;             // Max time to predict ahead (seconds)
        float CorrectionSmoothTime = 0.1f;          // Time to smooth corrections (seconds)
        float CorrectionThreshold = 0.01f;          // Min difference to trigger correction
        float SnapCorrectionThreshold = 2.0f;       // Distance to snap instead of smooth
        bool EnableSmoothing = true;                // Smooth corrections vs snap
        bool EnableInputBuffering = true;           // Buffer inputs before sending
    };

    //==========================================================================
    // INPUT STATE
    //==========================================================================

    // Local input state with timing
    struct PredictedInput {
        InputSample Sample;
        Math::Vec3 PredictedPosition{ 0.0f };       // Position after this input
        Math::Quat PredictedRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        Math::Vec3 PredictedVelocity{ 0.0f };
        uint64_t Timestamp = 0;                     // When input was generated
        bool Acknowledged = false;                  // Has server acknowledged this
    };

    //==========================================================================
    // PREDICTION STATE
    //==========================================================================

    // Per-entity prediction state
    struct EntityPredictionState {
        uint32_t NetworkId = 0;
        entt::entity Entity = entt::null;

        // Input history for reconciliation
        std::deque<PredictedInput> InputHistory;
        uint32_t NextInputSequence = 1;
        uint32_t LastAckedInputSequence = 0;

        // Server state for reconciliation
        Math::Vec3 LastServerPosition{ 0.0f };
        Math::Quat LastServerRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        uint32_t LastServerTick = 0;

        // Correction smoothing
        Math::Vec3 CorrectionOffset{ 0.0f };        // Current visual offset being smoothed
        Math::Quat CorrectionRotationOffset{ 1.0f, 0.0f, 0.0f, 0.0f };
        float CorrectionAlpha = 1.0f;               // Smoothing progress (0 = start, 1 = done)

        // Statistics
        uint32_t MispredictionCount = 0;
        float AverageMisprediction = 0.0f;
    };

    //==========================================================================
    // INPUT COMMAND CALLBACK
    //==========================================================================

    // Callback to apply input and return resulting state
    // This is the movement simulation that must match the server exactly
    using InputSimulationCallback = std::function<void(
        ECS::Scene& scene,
        entt::entity entity,
        const InputSample& input,
        float deltaTime,
        Math::Vec3& outPosition,
        Math::Quat& outRotation,
        Math::Vec3& outVelocity
    )>;

    //==========================================================================
    // CLIENT-SIDE PREDICTION SYSTEM
    //==========================================================================

    class ClientPredictionSystem {
    public:
        ClientPredictionSystem();
        ~ClientPredictionSystem();

        // Prevent copying
        ClientPredictionSystem(const ClientPredictionSystem&) = delete;
        ClientPredictionSystem& operator=(const ClientPredictionSystem&) = delete;

        //----------------------------------------------------------------------
        // Initialization
        //----------------------------------------------------------------------

        // Initialize with client reference
        void Initialize(NetworkClient* client, const PredictionConfig& config = PredictionConfig());

        // Shutdown
        void Shutdown();

        // Check if initialized
        bool IsInitialized() const { return m_Client != nullptr; }

        //----------------------------------------------------------------------
        // Entity Registration
        //----------------------------------------------------------------------

        // Register an entity for client-side prediction
        void RegisterEntity(entt::entity entity, uint32_t networkId);

        // Unregister an entity
        void UnregisterEntity(uint32_t networkId);

        // Check if entity is registered
        bool IsEntityRegistered(uint32_t networkId) const;

        // Get prediction state for an entity
        EntityPredictionState* GetPredictionState(uint32_t networkId);
        const EntityPredictionState* GetPredictionState(uint32_t networkId) const;

        //----------------------------------------------------------------------
        // Input Processing
        //----------------------------------------------------------------------

        // Record and predict a new input (call when player provides input)
        void RecordInput(ECS::Scene& scene, uint32_t networkId, const InputSample& rawInput, float deltaTime);

        // Send pending inputs to server
        void SendInputs();

        //----------------------------------------------------------------------
        // Server Acknowledgment
        //----------------------------------------------------------------------

        // Process input acknowledgment from server
        void ProcessInputAck(ECS::Scene& scene, const InputAckPacket& ackPacket);

        //----------------------------------------------------------------------
        // Update Loop
        //----------------------------------------------------------------------

        // Update prediction system (smooth corrections, cleanup old inputs)
        void Update(ECS::Scene& scene, float deltaTime);

        // Apply visual smoothing to entity transforms
        void ApplyVisualSmoothing(ECS::Scene& scene, float deltaTime);

        //----------------------------------------------------------------------
        // Callbacks
        //----------------------------------------------------------------------

        // Set the input simulation callback (REQUIRED - must match server simulation)
        void SetSimulationCallback(InputSimulationCallback callback) { m_SimulationCallback = std::move(callback); }

        //----------------------------------------------------------------------
        // Statistics
        //----------------------------------------------------------------------

        uint32_t GetPendingInputCount(uint32_t networkId) const;
        uint32_t GetTotalMispredictions() const { return m_TotalMispredictions; }
        float GetAverageMispredictionDistance() const { return m_AverageMisprediction; }
        uint64_t GetTotalInputsSent() const { return m_TotalInputsSent; }

        //----------------------------------------------------------------------
        // Configuration
        //----------------------------------------------------------------------

        const PredictionConfig& GetConfig() const { return m_Config; }
        void SetConfig(const PredictionConfig& config) { m_Config = config; }

    private:
        //----------------------------------------------------------------------
        // Internal Methods
        //----------------------------------------------------------------------

        // Simulate input and record predicted state
        void SimulateInput(ECS::Scene& scene, EntityPredictionState& state, 
                          PredictedInput& input, float deltaTime);

        // Reconcile prediction with server state
        void Reconcile(ECS::Scene& scene, EntityPredictionState& state,
                      const Math::Vec3& serverPosition, const Math::Quat& serverRotation,
                      uint32_t ackedSequence);

        // Re-simulate all unacknowledged inputs after reconciliation
        void ResimulateInputs(ECS::Scene& scene, EntityPredictionState& state);

        // Build and send input packet
        void BuildAndSendInputPacket(EntityPredictionState& state);

        // Clean up old acknowledged inputs
        void CleanupAcknowledgedInputs(EntityPredictionState& state);

        // Apply correction smoothing for an entity
        void SmoothCorrection(EntityPredictionState& state, float deltaTime);

    private:
        NetworkClient* m_Client = nullptr;
        PredictionConfig m_Config;

        // Per-entity prediction states
        std::unordered_map<uint32_t, EntityPredictionState> m_PredictionStates;
        std::unordered_map<entt::entity, uint32_t> m_EntityToNetworkId;

        // Input sending
        float m_InputSendAccumulator = 0.0f;
        std::vector<uint8_t> m_PacketBuffer;

        // Simulation callback
        InputSimulationCallback m_SimulationCallback;

        // Statistics
        uint32_t m_TotalMispredictions = 0;
        float m_AverageMisprediction = 0.0f;
        uint64_t m_TotalInputsSent = 0;
    };

} // namespace Network
} // namespace Core
