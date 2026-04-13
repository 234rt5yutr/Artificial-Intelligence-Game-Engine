#include "Core/Network/MultiplayerProductLayer.h"

#include "Core/Network/Migration/HostMigrationCoordinator.h"
#include "Core/Network/NetworkContractState.h"
#include "Core/Network/MultiplayerRuntimeFeatures.h"
#include "Core/Network/Policies/ReplicationPolicyRegistry.h"
#include "Core/Network/Replay/NetworkReplayPlayer.h"
#include "Core/Network/Replay/NetworkReplayRecorder.h"
#include "Core/Network/RPC/NetworkRPCRegistry.h"
#include "Core/Network/Rollback/PredictionResimulation.h"
#include "Core/Network/Rollback/RollbackCoordinator.h"
#include "Core/Network/Session/SessionOrchestrator.h"

namespace Core {
namespace Network {

    DedicatedServerStartResult StartDedicatedServerInstance(const DedicatedServerStartRequest& request) {
        return SessionOrchestrator::Get().StartDedicatedServerInstance(request);
    }

    void StopDedicatedServerInstance(const std::string& stopReason) {
        SessionOrchestrator::Get().StopDedicatedServerInstance(stopReason);
    }

    SessionJoinResult JoinSessionByInviteCode(const SessionInviteJoinRequest& request) {
        return SessionOrchestrator::Get().JoinSessionByInviteCode(request);
    }

    LANDiscoveryResult DiscoverLANSessions(const LANDiscoveryRequest& request) {
        return SessionOrchestrator::Get().DiscoverLANSessions(request);
    }

    ReplicationPolicyRegistrationResult RegisterReplicatedPropertyPolicy(
        const ReplicatedPropertyPolicyRegistrationRequest& request) {
        return ReplicationPolicyRegistry::Get().RegisterReplicatedPropertyPolicy(request);
    }

    NetworkRPCRegistrationResult RegisterNetworkRPC(const NetworkRPCRegistrationRequest& request) {
        return NetworkRPCRegistry::Get().RegisterNetworkRPC(request);
    }

    ReplayRecordResult RecordNetworkReplay(const ReplayRecordRequest& request) {
        const MultiplayerRuntimeFeatureGates gates = MultiplayerRuntimeFeatures::Get().GetFeatureGates();
        if (!gates.ReplayEnabled) {
            ReplayRecordResult result;
            result.Success = false;
            result.ErrorCode = NET_FEATURE_GATE_DISABLED;
            result.Message = "Replay runtime feature gate is disabled.";
            return result;
        }
        return NetworkReplayRecorder::Get().RecordNetworkReplay(request);
    }

    ReplayPlaybackResult PlayNetworkReplay(const ReplayPlaybackRequest& request) {
        const MultiplayerRuntimeFeatureGates gates = MultiplayerRuntimeFeatures::Get().GetFeatureGates();
        if (!gates.ReplayEnabled) {
            ReplayPlaybackResult result;
            result.Success = false;
            result.ErrorCode = NET_FEATURE_GATE_DISABLED;
            result.Message = "Replay runtime feature gate is disabled.";
            result.ReplayId = request.ReplayId;
            return result;
        }
        return NetworkReplayPlayer::Get().PlayNetworkReplay(request);
    }

    RollbackSimulationResult RollbackSimulationFrame(const RollbackSimulationRequest& request) {
        const MultiplayerRuntimeFeatureGates gates = MultiplayerRuntimeFeatures::Get().GetFeatureGates();
        if (!gates.RollbackEnabled) {
            RollbackSimulationResult result;
            result.Success = false;
            result.ErrorCode = NET_FEATURE_GATE_DISABLED;
            result.Message = "Rollback runtime feature gate is disabled.";
            result.TargetFrameTick = request.TargetFrameTick;
            result.FullSyncFallbackTriggered = request.TriggerFullSyncOnFailure;
            return result;
        }
        return RollbackCoordinator::Get().RollbackSimulationFrame(request);
    }

    ResimulationResult ResimulatePredictedFrames(const ResimulationRequest& request) {
        const MultiplayerRuntimeFeatureGates gates = MultiplayerRuntimeFeatures::Get().GetFeatureGates();
        if (!gates.ResimulationEnabled) {
            ResimulationResult result;
            result.Success = false;
            result.ErrorCode = NET_FEATURE_GATE_DISABLED;
            result.Message = "Resimulation runtime feature gate is disabled.";
            return result;
        }
        return PredictionResimulationService::Get().ResimulatePredictedFrames(request);
    }

    HostMigrationResult MigrateHostSession(const HostMigrationRequest& request) {
        const MultiplayerRuntimeFeatureGates gates = MultiplayerRuntimeFeatures::Get().GetFeatureGates();
        if (!gates.HostMigrationEnabled) {
            HostMigrationResult result;
            result.Success = false;
            result.ErrorCode = NET_FEATURE_GATE_DISABLED;
            result.Message = "Host migration runtime feature gate is disabled.";
            result.PreviousHostId = request.CurrentHostId;
            return result;
        }
        return HostMigrationCoordinator::Get().MigrateHostSession(request);
    }

    MultiplayerRuntimeFeatureGates GetMultiplayerRuntimeFeatureGates() {
        return MultiplayerRuntimeFeatures::Get().GetFeatureGates();
    }

    void SetMultiplayerRuntimeFeatureGates(const MultiplayerRuntimeFeatureGates& gates) {
        MultiplayerRuntimeFeatures::Get().SetFeatureGates(gates);
    }

    SessionRuntimeState GetSessionRuntimeState() {
        return SessionOrchestrator::Get().GetRuntimeState();
    }

    std::optional<SessionInstanceRecord> GetActiveSessionRecord() {
        return SessionOrchestrator::Get().GetActiveSessionRecord();
    }

    std::vector<SessionInstanceRecord> GetKnownSessionRecords() {
        return SessionOrchestrator::Get().GetKnownSessionRecords();
    }

    void SetLegacySessionFallbackEnabled(bool enabled) {
        SessionOrchestrator::Get().SetLegacyFallbackEnabled(enabled);
    }

    bool IsLegacySessionFallbackEnabled() {
        return SessionOrchestrator::Get().IsLegacyFallbackEnabled();
    }

    void SetNetworkBackwardsCompatibilityMode(bool enabled) {
        SetBackwardsCompatibilityMode(enabled);
    }

    bool IsNetworkBackwardsCompatibilityMode() {
        return IsBackwardsCompatibilityMode();
    }

    NetworkDiagnosticsSnapshot GetNetworkDiagnosticsSnapshot() {
        return NetworkDiagnosticsState::Get().GetSnapshot();
    }

} // namespace Network
} // namespace Core

