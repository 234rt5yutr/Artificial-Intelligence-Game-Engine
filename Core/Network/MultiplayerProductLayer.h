#pragma once

#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"
#include "Core/Network/Migration/HostMigrationTypes.h"
#include "Core/Network/MultiplayerRuntimeFeatures.h"
#include "Core/Network/Policies/ReplicationPolicyTypes.h"
#include "Core/Network/RPC/NetworkRPCTypes.h"
#include "Core/Network/Replay/ReplayTypes.h"
#include "Core/Network/Rollback/RollbackTypes.h"
#include "Core/Network/Session/SessionTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace Core {
namespace Network {

    DedicatedServerStartResult StartDedicatedServerInstance(const DedicatedServerStartRequest& request);
    void StopDedicatedServerInstance(const std::string& stopReason = "Stopped by API");

    SessionJoinResult JoinSessionByInviteCode(const SessionInviteJoinRequest& request);
    LANDiscoveryResult DiscoverLANSessions(const LANDiscoveryRequest& request);
    ReplicationPolicyRegistrationResult RegisterReplicatedPropertyPolicy(
        const ReplicatedPropertyPolicyRegistrationRequest& request);
    NetworkRPCRegistrationResult RegisterNetworkRPC(const NetworkRPCRegistrationRequest& request);
    ReplayRecordResult RecordNetworkReplay(const ReplayRecordRequest& request);
    ReplayPlaybackResult PlayNetworkReplay(const ReplayPlaybackRequest& request);
    RollbackSimulationResult RollbackSimulationFrame(const RollbackSimulationRequest& request);
    ResimulationResult ResimulatePredictedFrames(const ResimulationRequest& request);
    HostMigrationResult MigrateHostSession(const HostMigrationRequest& request);

    MultiplayerRuntimeFeatureGates GetMultiplayerRuntimeFeatureGates();
    void SetMultiplayerRuntimeFeatureGates(const MultiplayerRuntimeFeatureGates& gates);

    SessionRuntimeState GetSessionRuntimeState();
    std::optional<SessionInstanceRecord> GetActiveSessionRecord();
    std::vector<SessionInstanceRecord> GetKnownSessionRecords();

    void SetLegacySessionFallbackEnabled(bool enabled);
    bool IsLegacySessionFallbackEnabled();

    void SetNetworkBackwardsCompatibilityMode(bool enabled);
    bool IsNetworkBackwardsCompatibilityMode();

    NetworkDiagnosticsSnapshot GetNetworkDiagnosticsSnapshot();

} // namespace Network
} // namespace Core

