#pragma once

#include "MCPTool.h"

#include "Core/Network/MultiplayerProductLayer.h"

namespace Core {
namespace MCP {

    inline Json SessionCompatibilityToJson(const Network::SessionCompatibilityDescriptor& compatibility) {
        return {
            {"protocolVersion", compatibility.ProtocolVersion},
            {"contractHash", compatibility.ContractHash},
            {"buildCompatibilityHash", compatibility.BuildCompatibilityHash},
            {"backwardsCompatibilityMode", compatibility.BackwardsCompatibilityMode}
        };
    }

    inline Json SessionRecordToJson(const Network::SessionInstanceRecord& session) {
        return {
            {"sessionId", session.SessionId},
            {"serverName", session.ServerName},
            {"runtimeProfile", session.RuntimeProfile},
            {"bindAddress", session.BindAddress},
            {"port", session.Port},
            {"maxClients", session.MaxClients},
            {"connectedClients", session.ConnectedClients},
            {"tickRate", session.TickRate},
            {"isDedicated", session.IsDedicated},
            {"isListenServer", session.IsListenServer},
            {"inviteCode", session.InviteCode},
            {"createdAtUnixMs", session.CreatedAtUnixMs},
            {"compatibility", SessionCompatibilityToJson(session.Compatibility)}
        };
    }

    inline Json LANDiscoveryRecordToJson(const Network::LANDiscoveryRecord& record) {
        return {
            {"session", SessionRecordToJson(record.Session)},
            {"estimatedPingMs", record.EstimatedPingMs},
            {"versionMatch", record.VersionMatch},
            {"buildMatch", record.BuildMatch},
            {"availableSlots", record.AvailableSlots}
        };
    }

    inline Json DiagnosticsSnapshotToJson(const Network::NetworkDiagnosticsSnapshot& snapshot) {
        return {
            {"activeSessionId", snapshot.ActiveSessionId},
            {"runtimeProfile", snapshot.RuntimeProfile},
            {"dedicatedRunning", snapshot.DedicatedRunning},
            {"lastServerTick", snapshot.LastServerTick},
            {"replayCurrentTick", snapshot.ReplayCurrentTick},
            {"replaySeekDriftTicks", snapshot.ReplaySeekDriftTicks},
            {"replayPlaybackMode", snapshot.ReplayPlaybackMode},
            {"replayLastDecodeMicroseconds", snapshot.ReplayLastDecodeMicroseconds},
            {"lastRollbackTick", snapshot.LastRollbackTick},
            {"rollbackSnapshotRingUsage", snapshot.RollbackSnapshotRingUsage},
            {"pendingResimFrames", snapshot.PendingResimFrames},
            {"lastResimulatedFrames", snapshot.LastResimulatedFrames},
            {"migrationState", snapshot.MigrationState},
            {"migrationEpoch", snapshot.MigrationEpoch},
            {"recentNetworkEvents", snapshot.RecentNetworkEvents},
            {"contractHash", snapshot.ContractHash},
            {"compatibilityDowngradeActive", snapshot.CompatibilityDowngradeActive},
            {"registeredReplicationPolicies", snapshot.RegisteredReplicationPolicies},
            {"registeredRPCContracts", snapshot.RegisteredRPCContracts},
            {"contractHashMismatchCount", snapshot.ContractHashMismatchCount},
            {"inviteJoinAttempts", snapshot.InviteJoinAttempts},
            {"inviteJoinFailures", snapshot.InviteJoinFailures},
            {"lanDiscoveryQueries", snapshot.LANDiscoveryQueries},
            {"lanDiscoveryTimeouts", snapshot.LANDiscoveryTimeouts},
            {"replayRecordingsStarted", snapshot.ReplayRecordingsStarted},
            {"replayRecordingsCompleted", snapshot.ReplayRecordingsCompleted},
            {"replayRecordingFailures", snapshot.ReplayRecordingFailures},
            {"replayPlaybacksStarted", snapshot.ReplayPlaybacksStarted},
            {"replayPlaybackFailures", snapshot.ReplayPlaybackFailures},
            {"rollbacksApplied", snapshot.RollbacksApplied},
            {"rollbackFallbacks", snapshot.RollbackFallbacks},
            {"resimulationsExecuted", snapshot.ResimulationsExecuted},
            {"resimulationHardCorrections", snapshot.ResimulationHardCorrections},
            {"hostMigrationsStarted", snapshot.HostMigrationsStarted},
            {"hostMigrationsCompleted", snapshot.HostMigrationsCompleted},
            {"hostMigrationsFailed", snapshot.HostMigrationsFailed}
        };
    }

    inline Json ReplicationPolicyResultToJson(const Network::ReplicationPolicyRegistrationResult& result) {
        return {
            {"success", result.Success},
            {"errorCode", result.ErrorCode},
            {"message", result.Message},
            {"replacedExisting", result.ReplacedExisting},
            {"policyHash", result.PolicyHash}
        };
    }

    inline Json NetworkRPCResultToJson(const Network::NetworkRPCRegistrationResult& result) {
        return {
            {"success", result.Success},
            {"errorCode", result.ErrorCode},
            {"message", result.Message},
            {"replacedExisting", result.ReplacedExisting},
            {"rpcNameHash", result.RPCNameHash},
            {"contractHash", result.ContractHash}
        };
    }

    inline Json ReplayRecordResultToJson(const Network::ReplayRecordResult& result) {
        return {
            {"success", result.Success},
            {"errorCode", result.ErrorCode},
            {"message", result.Message},
            {"replayId", result.ReplayId},
            {"archivePath", result.ArchivePath.string()},
            {"packetCount", result.PacketCount},
            {"markerCount", result.MarkerCount}
        };
    }

    inline Json ReplayPlaybackResultToJson(const Network::ReplayPlaybackResult& result) {
        return {
            {"success", result.Success},
            {"errorCode", result.ErrorCode},
            {"message", result.Message},
            {"replayId", result.ReplayId},
            {"currentTick", result.CurrentTick},
            {"seekDriftTicks", result.SeekDriftTicks},
            {"paused", result.Paused},
            {"loop", result.Loop}
        };
    }

    inline Json RollbackResultToJson(const Network::RollbackSimulationResult& result) {
        return {
            {"success", result.Success},
            {"errorCode", result.ErrorCode},
            {"message", result.Message},
            {"targetFrameTick", result.TargetFrameTick},
            {"restoredFrameTick", result.RestoredFrameTick},
            {"rewoundFrameCount", result.RewoundFrameCount},
            {"restoredSnapshotHash", result.RestoredSnapshotHash},
            {"snapshotRingUsage", result.SnapshotRingUsage},
            {"usedFallbackSnapshot", result.UsedFallbackSnapshot},
            {"fullSyncFallbackTriggered", result.FullSyncFallbackTriggered}
        };
    }

    inline Json ResimulationResultToJson(const Network::ResimulationResult& result) {
        return {
            {"success", result.Success},
            {"errorCode", result.ErrorCode},
            {"message", result.Message},
            {"framesResimulated", result.FramesResimulated},
            {"hardCorrectionCount", result.HardCorrectionCount},
            {"divergenceMagnitude", result.DivergenceMagnitude},
            {"predictedStateHash", result.PredictedStateHash},
            {"authoritativeStateHash", result.AuthoritativeStateHash}
        };
    }

    inline Json HostMigrationResultToJson(const Network::HostMigrationResult& result) {
        return {
            {"success", result.Success},
            {"errorCode", result.ErrorCode},
            {"message", result.Message},
            {"epochId", result.EpochId},
            {"sessionEpoch", result.SessionEpoch},
            {"previousHostId", result.PreviousHostId},
            {"selectedHostId", result.SelectedHostId},
            {"selectedHostEndpoint", result.SelectedHostEndpoint},
            {"selectedHostPort", result.SelectedHostPort},
            {"usedFallbackCandidate", result.UsedFallbackCandidate},
            {"rolledBackToPreviousHost", result.RolledBackToPreviousHost},
            {"failedCandidates", result.FailedCandidates}
        };
    }

    inline Json FeatureGatesToJson(const Network::MultiplayerRuntimeFeatureGates& gates) {
        return {
            {"replayEnabled", gates.ReplayEnabled},
            {"rollbackEnabled", gates.RollbackEnabled},
            {"resimulationEnabled", gates.ResimulationEnabled},
            {"hostMigrationEnabled", gates.HostMigrationEnabled}
        };
    }

    inline std::vector<MCPToolPtr> CreateNetworkTools() {
        std::vector<MCPToolPtr> tools;

        ToolInputSchema dedicatedSchema;
        dedicatedSchema.Type = "object";
        dedicatedSchema.Properties = {
            {"serverName", { {"type", "string"} }},
            {"bindAddress", { {"type", "string"} }},
            {"port", { {"type", "integer"}, {"minimum", 1}, {"maximum", 65535} }},
            {"maxClients", { {"type", "integer"}, {"minimum", 1}, {"maximum", 256} }},
            {"tickRate", { {"type", "integer"}, {"minimum", 10}, {"maximum", 240} }},
            {"headless", { {"type", "boolean"}, {"default", true} }},
            {"runtimeProfile", { {"type", "string"}, {"enum", Json::array({"dedicated"})}, {"default", "dedicated"} }},
            {"buildCompatibilityHash", { {"type", "string"} }},
            {"featureFlags", {
                {"type", "object"},
                {"properties", {
                    {"disableRenderer", { {"type", "boolean"} }},
                    {"disableUI", { {"type", "boolean"} }},
                    {"enableReplay", { {"type", "boolean"} }},
                    {"enableHostMigration", { {"type", "boolean"} }}
                }}
            }}
        };
        dedicatedSchema.Required = { "serverName", "bindAddress", "port", "maxClients", "tickRate" };

        tools.push_back(CreateLambdaTool(
            "StartDedicatedServerInstance",
            "Start a dedicated server runtime profile with validated session metadata.",
            dedicatedSchema,
            [](const Json& arguments, ECS::Scene*) -> ToolResult {
                Network::DedicatedServerStartRequest request;
                request.ServerName = arguments.value("serverName", "Dedicated Server");
                request.BindAddress = arguments.value("bindAddress", "0.0.0.0");
                request.Port = static_cast<uint16_t>(arguments.value("port", 27015));
                request.MaxClients = arguments.value("maxClients", 32U);
                request.TickRate = arguments.value("tickRate", 60U);
                request.Headless = arguments.value("headless", true);
                request.RuntimeProfile = arguments.value("runtimeProfile", "dedicated");
                request.BuildCompatibilityHash = arguments.value("buildCompatibilityHash", std::string{});

                if (arguments.contains("featureFlags") && arguments["featureFlags"].is_object()) {
                    const Json& featureFlags = arguments["featureFlags"];
                    request.FeatureFlags.DisableRenderer = featureFlags.value("disableRenderer", true);
                    request.FeatureFlags.DisableUI = featureFlags.value("disableUI", true);
                    request.FeatureFlags.EnableReplay = featureFlags.value("enableReplay", true);
                    request.FeatureFlags.EnableHostMigration = featureFlags.value("enableHostMigration", true);
                }

                const Network::DedicatedServerStartResult result = Network::StartDedicatedServerInstance(request);
                if (!result.Success) {
                    return ToolResult::Error(result.ErrorCode + ": " + result.Message);
                }

                Json payload = {
                    {"success", true},
                    {"message", result.Message},
                    {"session", SessionRecordToJson(result.Session)}
                };
                return ToolResult::SuccessJson(payload);
            },
            false,
            { "network.session.start" }));

        ToolInputSchema joinSchema;
        joinSchema.Type = "object";
        joinSchema.Properties = {
            {"inviteCode", { {"type", "string"} }},
            {"clientDisplayName", { {"type", "string"} }},
            {"protocolVersion", { {"type", "integer"}, {"minimum", 1} }},
            {"buildCompatibilityHash", { {"type", "string"} }},
            {"authToken", { {"type", "string"} }}
        };
        joinSchema.Required = { "inviteCode", "clientDisplayName", "protocolVersion", "buildCompatibilityHash" };

        tools.push_back(CreateLambdaTool(
            "JoinSessionByInviteCode",
            "Validate invite-based session join metadata and return resolved endpoint information.",
            joinSchema,
            [](const Json& arguments, ECS::Scene*) -> ToolResult {
                Network::SessionInviteJoinRequest request;
                request.InviteCode = arguments.value("inviteCode", std::string{});
                request.ClientDisplayName = arguments.value("clientDisplayName", "Player");
                request.ProtocolVersion = arguments.value("protocolVersion", Network::NETWORK_PROTOCOL_VERSION);
                request.BuildCompatibilityHash = arguments.value("buildCompatibilityHash", std::string{});
                if (arguments.contains("authToken") && arguments["authToken"].is_string()) {
                    request.AuthToken = arguments["authToken"].get<std::string>();
                }

                const Network::SessionJoinResult result = Network::JoinSessionByInviteCode(request);
                if (!result.Success) {
                    return ToolResult::Error(result.ErrorCode + ": " + result.Message);
                }

                Json payload = {
                    {"success", true},
                    {"message", result.Message},
                    {"session", SessionRecordToJson(result.Session)},
                    {"clientConfiguration", {
                        {"serverAddress", result.ClientConfiguration.ServerAddress},
                        {"serverPort", result.ClientConfiguration.ServerPort},
                        {"connectionTimeoutMs", result.ClientConfiguration.ConnectionTimeoutMs}
                    }},
                    {"usedCompatibilityDowngrade", result.UsedCompatibilityDowngrade},
                    {"usedLegacyDirectConnectFallback", result.UsedLegacyDirectConnectFallback}
                };
                return ToolResult::SuccessJson(payload);
            },
            false,
            { "network.session.join" }));

        ToolInputSchema discoverySchema;
        discoverySchema.Type = "object";
        discoverySchema.Properties = {
            {"timeoutMs", { {"type", "integer"}, {"minimum", 1}, {"maximum", 5000} }},
            {"maxResults", { {"type", "integer"}, {"minimum", 1}, {"maximum", 256} }},
            {"requireProtocolVersion", { {"type", "integer"}, {"minimum", 1} }},
            {"requireBuildCompatibilityHash", { {"type", "string"} }},
            {"includeListenServers", { {"type", "boolean"}, {"default", true} }},
            {"includeDedicatedServers", { {"type", "boolean"}, {"default", true} }}
        };
        discoverySchema.Required = { "timeoutMs", "maxResults", "includeListenServers", "includeDedicatedServers" };

        tools.push_back(CreateLambdaTool(
            "DiscoverLANSessions",
            "Discover LAN session candidates with compatibility filtering and deterministic ranking.",
            discoverySchema,
            [](const Json& arguments, ECS::Scene*) -> ToolResult {
                Network::LANDiscoveryRequest request;
                request.TimeoutMs = arguments.value("timeoutMs", 250U);
                request.MaxResults = arguments.value("maxResults", 32U);
                request.IncludeListenServers = arguments.value("includeListenServers", true);
                request.IncludeDedicatedServers = arguments.value("includeDedicatedServers", true);
                if (arguments.contains("requireProtocolVersion")) {
                    request.RequireProtocolVersion = arguments["requireProtocolVersion"].get<uint32_t>();
                }
                if (arguments.contains("requireBuildCompatibilityHash")) {
                    request.RequireBuildCompatibilityHash = arguments["requireBuildCompatibilityHash"].get<std::string>();
                }

                const Network::LANDiscoveryResult result = Network::DiscoverLANSessions(request);
                if (!result.Success) {
                    return ToolResult::Error(result.ErrorCode + ": " + result.Message);
                }

                Json sessions = Json::array();
                for (const Network::LANDiscoveryRecord& record : result.Sessions) {
                    sessions.push_back(LANDiscoveryRecordToJson(record));
                }

                Json payload = {
                    {"success", true},
                    {"timedOut", result.TimedOut},
                    {"message", result.Message},
                    {"sessions", sessions}
                };
                if (!result.ErrorCode.empty()) {
                    payload["errorCode"] = result.ErrorCode;
                }
                return ToolResult::SuccessJson(payload);
            },
            false,
            { "network.discovery.read" }));

        ToolInputSchema replicationPolicySchema;
        replicationPolicySchema.Type = "object";
        replicationPolicySchema.Properties = {
            {"policyId", { {"type", "string"} }},
            {"targetComponent", { {"type", "string"} }},
            {"targetProperty", { {"type", "string"} }},
            {"sendRateHz", { {"type", "number"}, {"minimum", 1.0}, {"maximum", 240.0} }},
            {"relevanceClass", { {"type", "string"}, {"enum", Json::array({"global", "nearby", "owner-only", "team-only"})} }},
            {"quantizationProfile", { {"type", "string"}, {"enum", Json::array({"none", "coarse", "normal", "high"})} }},
            {"reliabilityClass", { {"type", "string"}, {"enum", Json::array({"unreliable", "reliable", "reliable-ordered"})} }}
        };
        replicationPolicySchema.Required = {
            "policyId",
            "targetComponent",
            "targetProperty",
            "sendRateHz",
            "relevanceClass",
            "quantizationProfile",
            "reliabilityClass"
        };

        tools.push_back(CreateLambdaTool(
            "RegisterReplicatedPropertyPolicy",
            "Register deterministic replication policy contracts for component property replication control.",
            replicationPolicySchema,
            [](const Json& arguments, ECS::Scene*) -> ToolResult {
                const std::optional<Network::ReplicationRelevanceClass> relevanceClass =
                    Network::ParseReplicationRelevanceClass(arguments.value("relevanceClass", std::string{}));
                const std::optional<Network::ReplicationQuantizationProfile> quantizationProfile =
                    Network::ParseReplicationQuantizationProfile(arguments.value("quantizationProfile", std::string{}));
                const std::optional<Network::ReplicationReliabilityClass> reliabilityClass =
                    Network::ParseReplicationReliabilityClass(arguments.value("reliabilityClass", std::string{}));

                if (!relevanceClass.has_value() || !quantizationProfile.has_value() || !reliabilityClass.has_value()) {
                    return ToolResult::Error("Invalid relevanceClass, quantizationProfile, or reliabilityClass.");
                }

                Network::ReplicatedPropertyPolicyRegistrationRequest request;
                request.PolicyId = arguments.value("policyId", std::string{});
                request.TargetComponent = arguments.value("targetComponent", std::string{});
                request.TargetProperty = arguments.value("targetProperty", std::string{});
                request.SendRateHz = arguments.value("sendRateHz", 20.0f);
                request.RelevanceClass = relevanceClass.value();
                request.QuantizationProfile = quantizationProfile.value();
                request.ReliabilityClass = reliabilityClass.value();

                const Network::ReplicationPolicyRegistrationResult result =
                    Network::RegisterReplicatedPropertyPolicy(request);
                if (!result.Success) {
                    return ToolResult::Error(result.ErrorCode + ": " + result.Message);
                }
                return ToolResult::SuccessJson(ReplicationPolicyResultToJson(result));
            },
            false,
            { "network.replication.policy.write" }));

        ToolInputSchema rpcSchema;
        rpcSchema.Type = "object";
        rpcSchema.Properties = {
            {"rpcName", { {"type", "string"} }},
            {"targetEntityScope", { {"type", "string"}, {"enum", Json::array({"global", "entity"})} }},
            {"reliabilityClass", { {"type", "string"}, {"enum", Json::array({"unreliable", "reliable", "reliable-ordered"})} }},
            {"requiresAuth", { {"type", "boolean"} }},
            {"replayAllowed", { {"type", "boolean"} }},
            {"payloadSchemaHash", { {"type", "string"} }}
        };
        rpcSchema.Required = {
            "rpcName",
            "targetEntityScope",
            "reliabilityClass",
            "requiresAuth",
            "replayAllowed",
            "payloadSchemaHash"
        };

        tools.push_back(CreateLambdaTool(
            "RegisterNetworkRPC",
            "Register network RPC contracts with reliability, auth, and replay policy validation.",
            rpcSchema,
            [](const Json& arguments, ECS::Scene*) -> ToolResult {
                const std::optional<Network::RPCEntityScope> entityScope =
                    Network::ParseRPCEntityScope(arguments.value("targetEntityScope", std::string{}));
                const std::optional<Network::ReplicationReliabilityClass> reliabilityClass =
                    Network::ParseReplicationReliabilityClass(arguments.value("reliabilityClass", std::string{}));

                if (!entityScope.has_value() || !reliabilityClass.has_value()) {
                    return ToolResult::Error("Invalid targetEntityScope or reliabilityClass.");
                }

                Network::NetworkRPCRegistrationRequest request;
                request.RPCName = arguments.value("rpcName", std::string{});
                request.TargetEntityScope = entityScope.value();
                request.ReliabilityClass = reliabilityClass.value();
                request.RequiresAuth = arguments.value("requiresAuth", false);
                request.ReplayAllowed = arguments.value("replayAllowed", false);
                request.PayloadSchemaHash = arguments.value("payloadSchemaHash", std::string{});

                const Network::NetworkRPCRegistrationResult result = Network::RegisterNetworkRPC(request);
                if (!result.Success) {
                    return ToolResult::Error(result.ErrorCode + ": " + result.Message);
                }
                return ToolResult::SuccessJson(NetworkRPCResultToJson(result));
            },
            false,
            { "network.rpc.register" }));

        ToolInputSchema replayRecordSchema;
        replayRecordSchema.Type = "object";
        replayRecordSchema.Properties = {
            {"sessionId", { {"type", "string"} }},
            {"action", { {"type", "string"}, {"enum", Json::array({"start", "stop", "flush", "cancel"})} }},
            {"outputPath", { {"type", "string"} }},
            {"tags", { {"type", "array"}, {"items", { {"type", "string"} }} }},
            {"includeInboundPackets", { {"type", "boolean"}, {"default", true} }},
            {"includeOutboundPackets", { {"type", "boolean"}, {"default", true} }},
            {"includeAuthoritativeMarkers", { {"type", "boolean"}, {"default", true} }},
            {"maxDurationSeconds", { {"type", "integer"}, {"minimum", 1} }}
        };
        replayRecordSchema.Required = { "sessionId", "action" };

        tools.push_back(CreateLambdaTool(
            "RecordNetworkReplay",
            "Start, stop, flush, or cancel deterministic replay recording for a session.",
            replayRecordSchema,
            [](const Json& arguments, ECS::Scene*) -> ToolResult {
                const std::optional<Network::ReplayControlAction> action =
                    Network::ParseReplayControlAction(arguments.value("action", std::string{}));
                if (!action.has_value()) {
                    return ToolResult::Error("Invalid replay recording action.");
                }

                Network::ReplayRecordRequest request;
                request.SessionId = arguments.value("sessionId", std::string{});
                request.Action = action.value();
                request.IncludeInboundPackets = arguments.value("includeInboundPackets", true);
                request.IncludeOutboundPackets = arguments.value("includeOutboundPackets", true);
                request.IncludeAuthoritativeMarkers = arguments.value("includeAuthoritativeMarkers", true);

                if (arguments.contains("outputPath") && arguments["outputPath"].is_string()) {
                    request.OutputPath = arguments["outputPath"].get<std::string>();
                }
                if (arguments.contains("maxDurationSeconds") && arguments["maxDurationSeconds"].is_number_unsigned()) {
                    request.MaxDurationSeconds = arguments["maxDurationSeconds"].get<uint32_t>();
                }
                if (arguments.contains("tags") && arguments["tags"].is_array()) {
                    for (const Json& tagValue : arguments["tags"]) {
                        if (tagValue.is_string()) {
                            request.Tags.push_back(tagValue.get<std::string>());
                        }
                    }
                }

                const Network::ReplayRecordResult result = Network::RecordNetworkReplay(request);
                if (!result.Success) {
                    return ToolResult::Error(result.ErrorCode + ": " + result.Message);
                }
                return ToolResult::SuccessJson(ReplayRecordResultToJson(result));
            },
            false,
            { "network.replay.record" }));

        ToolInputSchema replayPlaybackSchema;
        replayPlaybackSchema.Type = "object";
        replayPlaybackSchema.Properties = {
            {"replayId", { {"type", "string"} }},
            {"startTick", { {"type", "integer"}, {"minimum", 0} }},
            {"seekTick", { {"type", "integer"}, {"minimum", 0} }},
            {"frameStep", { {"type", "integer"}, {"minimum", -4096}, {"maximum", 4096} }},
            {"playbackSpeed", { {"type", "number"}, {"minimum", 0.1}, {"maximum", 8.0} }},
            {"paused", { {"type", "boolean"}, {"default", false} }},
            {"loop", { {"type", "boolean"}, {"default", false} }}
        };
        replayPlaybackSchema.Required = { "replayId" };

        tools.push_back(CreateLambdaTool(
            "PlayNetworkReplay",
            "Load a replay archive with seek, pause, speed, and frame-step controls.",
            replayPlaybackSchema,
            [](const Json& arguments, ECS::Scene*) -> ToolResult {
                Network::ReplayPlaybackRequest request;
                request.ReplayId = arguments.value("replayId", std::string{});
                request.PlaybackSpeed = arguments.value("playbackSpeed", 1.0f);
                request.Paused = arguments.value("paused", false);
                request.Loop = arguments.value("loop", false);

                if (arguments.contains("startTick") && arguments["startTick"].is_number_unsigned()) {
                    request.StartTick = arguments["startTick"].get<uint32_t>();
                }
                if (arguments.contains("seekTick") && arguments["seekTick"].is_number_unsigned()) {
                    request.SeekTick = arguments["seekTick"].get<uint32_t>();
                }
                if (arguments.contains("frameStep") && arguments["frameStep"].is_number_integer()) {
                    request.FrameStep = arguments["frameStep"].get<int32_t>();
                }

                const Network::ReplayPlaybackResult result = Network::PlayNetworkReplay(request);
                if (!result.Success) {
                    return ToolResult::Error(result.ErrorCode + ": " + result.Message);
                }
                return ToolResult::SuccessJson(ReplayPlaybackResultToJson(result));
            },
            false,
            { "network.replay.play" }));

        ToolInputSchema rollbackSchema;
        rollbackSchema.Type = "object";
        rollbackSchema.Properties = {
            {"sessionId", { {"type", "string"} }},
            {"targetFrameTick", { {"type", "integer"}, {"minimum", 0} }},
            {"correctionReason", { {"type", "string"}, {"enum", Json::array({
                "authority-correction",
                "prediction-divergence",
                "late-input-arrival",
                "migration-resync"
            })} }},
            {"maxRollbackFrames", { {"type", "integer"}, {"minimum", 1}, {"maximum", 8192} }},
            {"allowNearestSnapshotFallback", { {"type", "boolean"}, {"default", true} }},
            {"triggerFullSyncOnFailure", { {"type", "boolean"}, {"default", true} }}
        };
        rollbackSchema.Required = { "sessionId", "targetFrameTick", "correctionReason" };

        tools.push_back(CreateLambdaTool(
            "RollbackSimulationFrame",
            "Restore authoritative simulation snapshots for a rollback frame and correction reason.",
            rollbackSchema,
            [](const Json& arguments, ECS::Scene*) -> ToolResult {
                const std::optional<Network::RollbackCorrectionReason> correctionReason =
                    Network::ParseRollbackCorrectionReason(arguments.value("correctionReason", std::string{}));
                if (!correctionReason.has_value()) {
                    return ToolResult::Error("Invalid rollback correctionReason.");
                }

                Network::RollbackSimulationRequest request;
                request.SessionId = arguments.value("sessionId", std::string{});
                request.TargetFrameTick = arguments.value("targetFrameTick", 0U);
                request.CorrectionReason = correctionReason.value();
                request.MaxRollbackFrames = arguments.value("maxRollbackFrames", 120U);
                request.AllowNearestSnapshotFallback = arguments.value("allowNearestSnapshotFallback", true);
                request.TriggerFullSyncOnFailure = arguments.value("triggerFullSyncOnFailure", true);

                const Network::RollbackSimulationResult result = Network::RollbackSimulationFrame(request);
                if (!result.Success) {
                    return ToolResult::Error(result.ErrorCode + ": " + result.Message);
                }
                return ToolResult::SuccessJson(RollbackResultToJson(result));
            },
            false,
            { "network.rollback.execute" }));

        ToolInputSchema resimulationSchema;
        resimulationSchema.Type = "object";
        resimulationSchema.Properties = {
            {"sessionId", { {"type", "string"} }},
            {"fromFrameTick", { {"type", "integer"}, {"minimum", 0} }},
            {"toFrameTick", { {"type", "integer"}, {"minimum", 0} }},
            {"maxFramesPerUpdate", { {"type", "integer"}, {"minimum", 1}, {"maximum", 1024} }},
            {"divergenceThreshold", { {"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0} }},
            {"enableSmoothing", { {"type", "boolean"}, {"default", true} }},
            {"enableHardCorrectionOnDivergence", { {"type", "boolean"}, {"default", true} }},
            {"authoritativeStateHash", { {"type", "integer"}, {"minimum", 0} }},
            {"bufferedInputs", {
                {"type", "array"},
                {"items", {
                    {"type", "object"},
                    {"properties", {
                        {"frameTick", { {"type", "integer"}, {"minimum", 0} }},
                        {"inputSequence", { {"type", "integer"}, {"minimum", 0} }},
                        {"buttons", { {"type", "integer"}, {"minimum", 0} }},
                        {"moveX", { {"type", "number"} }},
                        {"moveY", { {"type", "number"} }},
                        {"lookYaw", { {"type", "number"} }},
                        {"lookPitch", { {"type", "number"} }},
                        {"deltaTimeMs", { {"type", "integer"}, {"minimum", 0}, {"maximum", 65535} }}
                    }}
                }}
            }}
        };
        resimulationSchema.Required = { "sessionId", "fromFrameTick", "toFrameTick" };

        tools.push_back(CreateLambdaTool(
            "ResimulatePredictedFrames",
            "Resimulate predicted frame ranges with divergence metrics and hard-correction fallback.",
            resimulationSchema,
            [](const Json& arguments, ECS::Scene*) -> ToolResult {
                Network::ResimulationRequest request;
                request.SessionId = arguments.value("sessionId", std::string{});
                request.FromFrameTick = arguments.value("fromFrameTick", 0U);
                request.ToFrameTick = arguments.value("toFrameTick", 0U);
                request.MaxFramesPerUpdate = arguments.value("maxFramesPerUpdate", 32U);
                request.DivergenceThreshold = arguments.value("divergenceThreshold", 0.25f);
                request.EnableSmoothing = arguments.value("enableSmoothing", true);
                request.EnableHardCorrectionOnDivergence = arguments.value("enableHardCorrectionOnDivergence", true);

                if (arguments.contains("authoritativeStateHash") && arguments["authoritativeStateHash"].is_number_unsigned()) {
                    request.AuthoritativeStateHash = arguments["authoritativeStateHash"].get<uint64_t>();
                }

                if (arguments.contains("bufferedInputs") && arguments["bufferedInputs"].is_array()) {
                    for (const Json& inputJson : arguments["bufferedInputs"]) {
                        if (!inputJson.is_object()) {
                            continue;
                        }
                        Network::ResimulationInputRecord inputRecord;
                        inputRecord.FrameTick = inputJson.value("frameTick", 0U);
                        inputRecord.InputSequence = inputJson.value("inputSequence", 0U);
                        inputRecord.Input.InputSequence = inputRecord.InputSequence;
                        inputRecord.Input.Buttons = inputJson.value("buttons", 0U);
                        inputRecord.Input.MoveX = inputJson.value("moveX", 0.0f);
                        inputRecord.Input.MoveY = inputJson.value("moveY", 0.0f);
                        inputRecord.Input.LookYaw = inputJson.value("lookYaw", 0.0f);
                        inputRecord.Input.LookPitch = inputJson.value("lookPitch", 0.0f);
                        inputRecord.Input.DeltaTimeMs =
                            static_cast<uint16_t>(inputJson.value("deltaTimeMs", 0U));
                        request.BufferedInputs.push_back(inputRecord);
                    }
                }

                const Network::ResimulationResult result = Network::ResimulatePredictedFrames(request);
                if (!result.Success) {
                    return ToolResult::Error(result.ErrorCode + ": " + result.Message);
                }
                return ToolResult::SuccessJson(ResimulationResultToJson(result));
            },
            false,
            { "network.rollback.execute" }));

        ToolInputSchema migrationSchema;
        migrationSchema.Type = "object";
        migrationSchema.Properties = {
            {"sessionId", { {"type", "string"} }},
            {"currentHostId", { {"type", "string"} }},
            {"preferredCandidateId", { {"type", "string"} }},
            {"migrationArtifactPath", { {"type", "string"} }},
            {"commitTimeoutMs", { {"type", "integer"}, {"minimum", 100}, {"maximum", 120000} }},
            {"requiredAckTick", { {"type", "integer"}, {"minimum", 0} }},
            {"maxCandidateAttempts", { {"type", "integer"}, {"minimum", 1}, {"maximum", 64} }},
            {"allowFallbackCandidates", { {"type", "boolean"}, {"default", true} }},
            {"requireMigrationArtifact", { {"type", "boolean"}, {"default", false} }},
            {"rollbackOnCommitFailure", { {"type", "boolean"}, {"default", true} }},
            {"candidates", {
                {"type", "array"},
                {"items", {
                    {"type", "object"},
                    {"properties", {
                        {"clientId", { {"type", "string"} }},
                        {"endpointAddress", { {"type", "string"} }},
                        {"endpointPort", { {"type", "integer"}, {"minimum", 1}, {"maximum", 65535} }},
                        {"healthScore", { {"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0} }},
                        {"latencyMs", { {"type", "number"}, {"minimum", 0.0} }},
                        {"performanceScore", { {"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0} }},
                        {"lastAckTick", { {"type", "integer"}, {"minimum", 0} }},
                        {"compatible", { {"type", "boolean"}, {"default", true} }},
                        {"isCurrentHost", { {"type", "boolean"}, {"default", false} }}
                    }},
                    {"required", Json::array({"clientId", "endpointAddress", "endpointPort"})}
                }}
            }}
        };
        migrationSchema.Required = { "sessionId", "currentHostId", "candidates" };

        tools.push_back(CreateLambdaTool(
            "MigrateHostSession",
            "Execute host migration candidate ranking, artifact transfer validation, and commit/fallback flow.",
            migrationSchema,
            [](const Json& arguments, ECS::Scene*) -> ToolResult {
                Network::HostMigrationRequest request;
                request.SessionId = arguments.value("sessionId", std::string{});
                request.CurrentHostId = arguments.value("currentHostId", std::string{});
                request.CommitTimeoutMs = arguments.value("commitTimeoutMs", 3000U);
                request.RequiredAckTick = arguments.value("requiredAckTick", 0U);
                request.MaxCandidateAttempts = arguments.value("maxCandidateAttempts", 3U);
                request.AllowFallbackCandidates = arguments.value("allowFallbackCandidates", true);
                request.RequireMigrationArtifact = arguments.value("requireMigrationArtifact", false);
                request.RollbackOnCommitFailure = arguments.value("rollbackOnCommitFailure", true);

                if (arguments.contains("preferredCandidateId") && arguments["preferredCandidateId"].is_string()) {
                    request.PreferredCandidateId = arguments["preferredCandidateId"].get<std::string>();
                }
                if (arguments.contains("migrationArtifactPath") && arguments["migrationArtifactPath"].is_string()) {
                    request.MigrationArtifactPath = arguments["migrationArtifactPath"].get<std::string>();
                }

                if (arguments.contains("candidates") && arguments["candidates"].is_array()) {
                    for (const Json& candidateJson : arguments["candidates"]) {
                        if (!candidateJson.is_object()) {
                            continue;
                        }

                        Network::HostMigrationCandidate candidate;
                        candidate.ClientId = candidateJson.value("clientId", std::string{});
                        candidate.EndpointAddress = candidateJson.value("endpointAddress", std::string{});
                        candidate.EndpointPort = static_cast<uint16_t>(candidateJson.value("endpointPort", 0U));
                        candidate.HealthScore = candidateJson.value("healthScore", 0.0f);
                        candidate.LatencyMs = candidateJson.value("latencyMs", 0.0f);
                        candidate.PerformanceScore = candidateJson.value("performanceScore", 0.0f);
                        candidate.LastAckTick = candidateJson.value("lastAckTick", 0U);
                        candidate.Compatible = candidateJson.value("compatible", true);
                        candidate.IsCurrentHost = candidateJson.value("isCurrentHost", false);
                        request.Candidates.push_back(candidate);
                    }
                }

                const Network::HostMigrationResult result = Network::MigrateHostSession(request);
                if (!result.Success) {
                    return ToolResult::Error(result.ErrorCode + ": " + result.Message);
                }
                return ToolResult::SuccessJson(HostMigrationResultToJson(result));
            },
            false,
            { "network.hostmigration.execute" }));

        ToolInputSchema featureGateGetSchema;
        featureGateGetSchema.Type = "object";

        tools.push_back(CreateLambdaTool(
            "GetNetworkRuntimeFeatureGates",
            "Get replay, rollback, resimulation, and host-migration runtime feature-gate state.",
            featureGateGetSchema,
            [](const Json&, ECS::Scene*) -> ToolResult {
                return ToolResult::SuccessJson(FeatureGatesToJson(Network::GetMultiplayerRuntimeFeatureGates()));
            },
            false,
            { "network.discovery.read" }));

        ToolInputSchema featureGateSetSchema;
        featureGateSetSchema.Type = "object";
        featureGateSetSchema.Properties = {
            {"replayEnabled", { {"type", "boolean"} }},
            {"rollbackEnabled", { {"type", "boolean"} }},
            {"resimulationEnabled", { {"type", "boolean"} }},
            {"hostMigrationEnabled", { {"type", "boolean"} }}
        };

        tools.push_back(CreateLambdaTool(
            "SetNetworkRuntimeFeatureGates",
            "Set runtime feature gates for replay, rollback, resimulation, and host migration APIs.",
            featureGateSetSchema,
            [](const Json& arguments, ECS::Scene*) -> ToolResult {
                Network::MultiplayerRuntimeFeatureGates gates = Network::GetMultiplayerRuntimeFeatureGates();
                if (arguments.contains("replayEnabled") && arguments["replayEnabled"].is_boolean()) {
                    gates.ReplayEnabled = arguments["replayEnabled"].get<bool>();
                }
                if (arguments.contains("rollbackEnabled") && arguments["rollbackEnabled"].is_boolean()) {
                    gates.RollbackEnabled = arguments["rollbackEnabled"].get<bool>();
                }
                if (arguments.contains("resimulationEnabled") && arguments["resimulationEnabled"].is_boolean()) {
                    gates.ResimulationEnabled = arguments["resimulationEnabled"].get<bool>();
                }
                if (arguments.contains("hostMigrationEnabled") && arguments["hostMigrationEnabled"].is_boolean()) {
                    gates.HostMigrationEnabled = arguments["hostMigrationEnabled"].get<bool>();
                }
                Network::SetMultiplayerRuntimeFeatureGates(gates);
                return ToolResult::SuccessJson(FeatureGatesToJson(gates));
            },
            false,
            { "network.hostmigration.execute" }));

        ToolInputSchema diagnosticsSchema;
        diagnosticsSchema.Type = "object";

        tools.push_back(CreateLambdaTool(
            "GetNetworkDiagnostics",
            "Get centralized multiplayer session diagnostics state for runtime observability.",
            diagnosticsSchema,
            [](const Json&, ECS::Scene*) -> ToolResult {
                const Network::NetworkDiagnosticsSnapshot snapshot = Network::GetNetworkDiagnosticsSnapshot();
                return ToolResult::SuccessJson(DiagnosticsSnapshotToJson(snapshot));
            },
            false,
            { "network.discovery.read" }));

        return tools;
    }

} // namespace MCP
} // namespace Core

