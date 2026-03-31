#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/NetworkTransformComponent.h"
#include "Core/Network/NetworkServer.h"
#include "Core/Network/NetworkPackets.h"
#include "Core/Math/Math.h"
#include <unordered_map>
#include <deque>
#include <functional>
#include <chrono>

namespace Core {
namespace Network {

    //==========================================================================
    // RECONCILIATION CONFIGURATION
    //==========================================================================

    struct ReconciliationConfig {
        float PositionTolerance = 0.1f;             // Max allowed position error (meters)
        float VelocityTolerance = 0.5f;             // Max allowed velocity error (m/s)
        float RotationTolerance = 0.1f;             // Max allowed rotation error (radians)
        float CorrectionBlendRate = 0.2f;           // How fast to blend corrections (0-1)
        float HardCorrectionThreshold = 2.0f;       // Distance for immediate snap correction
        float SpeedHackThreshold = 2.0f;            // Speed multiplier threshold for detection
        uint32_t MaxInputsPerTick = 10;             // Max inputs to process per server tick
        uint32_t InputBufferSize = 64;              // Input buffer size per client
        float InputTimeoutMs = 200.0f;              // Time to wait for missing inputs
        bool EnableAntiCheat = true;                // Enable cheat detection
        bool EnableSoftCorrections = true;          // Blend corrections vs snap
    };

    //==========================================================================
    // CLIENT INPUT STATE
    //==========================================================================

    // Buffered input from a client
    struct BufferedInput {
        InputSample Sample;
        uint64_t ReceivedTimestamp = 0;
        bool Processed = false;
    };

    // Per-client reconciliation state
    struct ClientReconciliationState {
        uint32_t ClientId = 0;
        entt::entity PlayerEntity = entt::null;
        uint32_t NetworkId = 0;

        // Input buffer (ordered by sequence)
        std::deque<BufferedInput> InputBuffer;
        uint32_t LastProcessedSequence = 0;
        uint32_t ExpectedNextSequence = 1;

        // Server-side authoritative state
        Math::Vec3 AuthoritativePosition{ 0.0f };
        Math::Quat AuthoritativeRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        Math::Vec3 AuthoritativeVelocity{ 0.0f };

        // Client-reported state (for validation)
        Math::Vec3 ClientReportedPosition{ 0.0f };
        Math::Quat ClientReportedRotation{ 1.0f, 0.0f, 0.0f, 0.0f };

        // Correction state
        Math::Vec3 PendingCorrection{ 0.0f };
        Math::Quat PendingRotationCorrection{ 1.0f, 0.0f, 0.0f, 0.0f };
        bool NeedsCorrection = false;
        bool NeedsHardCorrection = false;

        // Anti-cheat tracking
        uint32_t SuspiciousInputCount = 0;
        uint32_t SpeedViolationCount = 0;
        float MaxObservedSpeed = 0.0f;
        uint64_t LastInputTimestamp = 0;

        // Statistics
        uint32_t TotalInputsProcessed = 0;
        uint32_t TotalCorrections = 0;
        float AveragePositionError = 0.0f;
    };

    //==========================================================================
    // VALIDATION RESULT
    //==========================================================================

    enum class ValidationResult : uint8_t {
        Valid = 0,              // Input is valid
        OutOfSequence,          // Input sequence is wrong
        TimingViolation,        // Input timing is suspicious
        SpeedViolation,         // Player moving too fast
        PositionMismatch,       // Position doesn't match expected
        Rejected                // Input rejected (cheat detected)
    };

    //==========================================================================
    // RECONCILIATION EVENTS
    //==========================================================================

    enum class ReconciliationEventType : uint8_t {
        CorrectionApplied,      // Soft correction applied
        HardCorrectionApplied,  // Hard snap correction
        CheatDetected,          // Suspicious activity detected
        InputDropped,           // Input was dropped/invalid
        ClientDesynced          // Client significantly desynced
    };

    struct ReconciliationEvent {
        ReconciliationEventType Type;
        uint32_t ClientId = 0;
        uint32_t NetworkId = 0;
        float ErrorMagnitude = 0.0f;
        ValidationResult Reason = ValidationResult::Valid;
    };

    using ReconciliationEventCallback = std::function<void(const ReconciliationEvent&)>;

    // Callback for server-side input simulation (must match client)
    using ServerSimulationCallback = std::function<void(
        ECS::Scene& scene,
        entt::entity entity,
        const InputSample& input,
        float deltaTime,
        Math::Vec3& outPosition,
        Math::Quat& outRotation,
        Math::Vec3& outVelocity
    )>;

    //==========================================================================
    // SERVER RECONCILIATION SYSTEM
    //==========================================================================

    class ServerReconciliationSystem {
    public:
        ServerReconciliationSystem();
        ~ServerReconciliationSystem();

        // Prevent copying
        ServerReconciliationSystem(const ServerReconciliationSystem&) = delete;
        ServerReconciliationSystem& operator=(const ServerReconciliationSystem&) = delete;

        //----------------------------------------------------------------------
        // Initialization
        //----------------------------------------------------------------------

        void Initialize(NetworkServer* server, const ReconciliationConfig& config = ReconciliationConfig());
        void Shutdown();
        bool IsInitialized() const { return m_Server != nullptr; }

        //----------------------------------------------------------------------
        // Client Registration
        //----------------------------------------------------------------------

        // Register a client's player entity for reconciliation
        void RegisterClient(uint32_t clientId, entt::entity playerEntity, uint32_t networkId);

        // Unregister a client
        void UnregisterClient(uint32_t clientId);

        // Check if client is registered
        bool IsClientRegistered(uint32_t clientId) const;

        // Get client state
        ClientReconciliationState* GetClientState(uint32_t clientId);
        const ClientReconciliationState* GetClientState(uint32_t clientId) const;

        //----------------------------------------------------------------------
        // Input Processing
        //----------------------------------------------------------------------

        // Receive and buffer input from client
        void ReceiveInput(uint32_t clientId, const ClientInputPacket& packet, 
                         const InputSample* samples, uint32_t sampleCount);

        // Process buffered inputs for all clients (call once per server tick)
        void ProcessInputs(ECS::Scene& scene, float deltaTime);

        //----------------------------------------------------------------------
        // Validation
        //----------------------------------------------------------------------

        // Validate a single input sample
        ValidationResult ValidateInput(const ClientReconciliationState& state, 
                                       const InputSample& input, float deltaTime);

        // Check if position is within tolerance
        bool IsPositionValid(const Math::Vec3& reported, const Math::Vec3& expected) const;

        //----------------------------------------------------------------------
        // Corrections
        //----------------------------------------------------------------------

        // Apply pending corrections to entities
        void ApplyCorrections(ECS::Scene& scene, float deltaTime);

        // Force a hard correction for a client
        void ForceHardCorrection(uint32_t clientId);

        //----------------------------------------------------------------------
        // Acknowledgments
        //----------------------------------------------------------------------

        // Send input acknowledgments to clients
        void SendAcknowledgments(ECS::Scene& scene);

        //----------------------------------------------------------------------
        // Update Loop
        //----------------------------------------------------------------------

        // Main update (call every server tick)
        void Update(ECS::Scene& scene, float deltaTime);

        //----------------------------------------------------------------------
        // Callbacks
        //----------------------------------------------------------------------

        void SetSimulationCallback(ServerSimulationCallback callback) { m_SimulationCallback = std::move(callback); }
        void SetEventCallback(ReconciliationEventCallback callback) { m_EventCallback = std::move(callback); }

        //----------------------------------------------------------------------
        // Statistics
        //----------------------------------------------------------------------

        uint32_t GetCurrentTick() const { return m_CurrentTick; }
        uint32_t GetTotalCorrections() const { return m_TotalCorrections; }
        uint32_t GetTotalCheatDetections() const { return m_TotalCheatDetections; }
        float GetAveragePositionError() const { return m_AveragePositionError; }

        //----------------------------------------------------------------------
        // Configuration
        //----------------------------------------------------------------------

        const ReconciliationConfig& GetConfig() const { return m_Config; }
        void SetConfig(const ReconciliationConfig& config) { m_Config = config; }

    private:
        //----------------------------------------------------------------------
        // Internal Methods
        //----------------------------------------------------------------------

        // Process inputs for a single client
        void ProcessClientInputs(ECS::Scene& scene, ClientReconciliationState& state, float deltaTime);

        // Simulate a single input
        void SimulateInput(ECS::Scene& scene, ClientReconciliationState& state,
                          const InputSample& input, float deltaTime);

        // Calculate correction needed
        void CalculateCorrection(ClientReconciliationState& state, 
                                const Math::Vec3& clientPosition,
                                const Math::Quat& clientRotation);

        // Apply soft correction (blended)
        void ApplySoftCorrection(ECS::Scene& scene, ClientReconciliationState& state, float deltaTime);

        // Apply hard correction (snap)
        void ApplyHardCorrection(ECS::Scene& scene, ClientReconciliationState& state);

        // Build and send ack packet
        void SendAckPacket(uint32_t clientId, const ClientReconciliationState& state);

        // Clean up old inputs
        void CleanupOldInputs(ClientReconciliationState& state);

        // Check for cheat patterns
        bool DetectCheating(ClientReconciliationState& state, const InputSample& input, float deltaTime);

        // Dispatch event
        void DispatchEvent(const ReconciliationEvent& event);

    private:
        NetworkServer* m_Server = nullptr;
        ReconciliationConfig m_Config;

        // Server tick
        uint32_t m_CurrentTick = 0;

        // Per-client states
        std::unordered_map<uint32_t, ClientReconciliationState> m_ClientStates;

        // Simulation callback
        ServerSimulationCallback m_SimulationCallback;

        // Event callback
        ReconciliationEventCallback m_EventCallback;

        // Packet buffer
        std::vector<uint8_t> m_PacketBuffer;

        // Statistics
        uint32_t m_TotalCorrections = 0;
        uint32_t m_TotalCheatDetections = 0;
        float m_AveragePositionError = 0.0f;
    };

} // namespace Network
} // namespace Core
