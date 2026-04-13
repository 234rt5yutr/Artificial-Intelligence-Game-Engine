#pragma once

#include "Core/Network/NetworkClient.h"
#include "Core/Network/NetworkPackets.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Core {
namespace Network {

    constexpr const char* NET_DEDICATED_PROFILE_INVALID = "NET_DEDICATED_PROFILE_INVALID";
    constexpr const char* NET_DEDICATED_PORT_BIND_FAILED = "NET_DEDICATED_PORT_BIND_FAILED";
    constexpr const char* NET_INVITE_CODE_INVALID = "NET_INVITE_CODE_INVALID";
    constexpr const char* NET_INVITE_EXPIRED_OR_EXHAUSTED = "NET_INVITE_EXPIRED_OR_EXHAUSTED";
    constexpr const char* NET_LAN_DISCOVERY_TIMEOUT = "NET_LAN_DISCOVERY_TIMEOUT";

    struct DedicatedServerFeatureFlags {
        bool DisableRenderer = true;
        bool DisableUI = true;
        bool EnableReplay = true;
        bool EnableHostMigration = true;
    };

    struct SessionCompatibilityDescriptor {
        uint32_t ProtocolVersion = NETWORK_PROTOCOL_VERSION;
        uint64_t ContractHash = 0;
        std::string BuildCompatibilityHash;
        bool BackwardsCompatibilityMode = true;
    };

    struct SessionInstanceRecord {
        std::string SessionId;
        std::string ServerName;
        std::string RuntimeProfile = "client";
        std::string BindAddress = "127.0.0.1";
        uint16_t Port = 27015;
        uint32_t MaxClients = 0;
        uint32_t ConnectedClients = 0;
        uint32_t TickRate = 60;
        bool IsDedicated = false;
        bool IsListenServer = false;
        std::string InviteCode;
        SessionCompatibilityDescriptor Compatibility;
        uint64_t CreatedAtUnixMs = 0;
    };

    struct SessionInviteRecord {
        std::string InviteCode;
        std::string SessionId;
        uint64_t ExpiresAtUnixMs = 0;
        uint32_t RemainingUses = 0;
        uint64_t CompatibilityHash = 0;
        bool RequiresAuthToken = false;
        std::string RequiredAuthToken;
    };

    enum class SessionRuntimeState : uint8_t {
        Idle = 0,
        Hosting,
        Joining,
        Connected,
        LegacyFallback
    };

    struct DedicatedServerStartRequest {
        std::string ServerName = "Dedicated Server";
        std::string BindAddress = "0.0.0.0";
        uint16_t Port = 27015;
        uint32_t MaxClients = 32;
        uint32_t TickRate = 60;
        bool Headless = true;
        std::string RuntimeProfile = "dedicated";
        DedicatedServerFeatureFlags FeatureFlags{};
        std::string BuildCompatibilityHash;
    };

    struct DedicatedServerStartResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        SessionInstanceRecord Session;
    };

    struct SessionInviteJoinRequest {
        std::string InviteCode;
        std::string ClientDisplayName = "Player";
        uint32_t ProtocolVersion = NETWORK_PROTOCOL_VERSION;
        std::string BuildCompatibilityHash;
        std::optional<std::string> AuthToken;
    };

    struct SessionJoinResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        SessionInstanceRecord Session;
        ClientConfig ClientConfiguration;
        bool UsedCompatibilityDowngrade = false;
        bool UsedLegacyDirectConnectFallback = false;
    };

    struct LANDiscoveryRequest {
        uint32_t TimeoutMs = 250;
        uint32_t MaxResults = 32;
        std::optional<uint32_t> RequireProtocolVersion;
        std::optional<std::string> RequireBuildCompatibilityHash;
        bool IncludeListenServers = true;
        bool IncludeDedicatedServers = true;
    };

    struct LANDiscoveryRecord {
        SessionInstanceRecord Session;
        uint32_t EstimatedPingMs = 0;
        bool VersionMatch = true;
        bool BuildMatch = true;
        uint32_t AvailableSlots = 0;
    };

    struct LANDiscoveryResult {
        bool Success = false;
        bool TimedOut = false;
        std::string ErrorCode;
        std::string Message;
        std::vector<LANDiscoveryRecord> Sessions;
    };

} // namespace Network
} // namespace Core

