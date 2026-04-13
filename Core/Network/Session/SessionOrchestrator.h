#pragma once

#include "Core/Network/NetworkManager.h"
#include "Core/Network/NetworkServer.h"
#include "Core/Network/Session/SessionTypes.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Network {

    class SessionOrchestrator {
    public:
        static SessionOrchestrator& Get();

        DedicatedServerStartResult StartDedicatedServerInstance(const DedicatedServerStartRequest& request);
        void StopDedicatedServerInstance(const std::string& stopReason);

        SessionJoinResult JoinSessionByInviteCode(const SessionInviteJoinRequest& request);
        LANDiscoveryResult DiscoverLANSessions(const LANDiscoveryRequest& request);

        SessionRuntimeState GetRuntimeState() const;
        std::optional<SessionInstanceRecord> GetActiveSessionRecord() const;
        std::vector<SessionInstanceRecord> GetKnownSessionRecords() const;

        void SetLegacyFallbackEnabled(bool enabled);
        bool IsLegacyFallbackEnabled() const;

    private:
        DedicatedServerStartResult MakeStartFailure(const std::string& errorCode, const std::string& message) const;
        SessionJoinResult MakeJoinFailure(const std::string& errorCode, const std::string& message) const;
        LANDiscoveryResult MakeDiscoveryFailure(const std::string& errorCode, const std::string& message) const;

        std::string GenerateSessionId(const std::string& serverName, uint16_t port);
        std::string GenerateInviteCode(const std::string& sessionId, uint64_t createdAtUnixMs);
        std::string NormalizeInviteCode(const std::string& inviteCode) const;
        bool IsInviteCodeWellFormed(const std::string& inviteCode) const;
        bool TryParseDirectConnectInvite(const std::string& inviteCode, std::string& outAddress, uint16_t& outPort) const;
        uint64_t GetNowUnixMilliseconds() const;

        mutable std::mutex m_Mutex;

        SessionRuntimeState m_RuntimeState = SessionRuntimeState::Idle;
        bool m_LegacyFallbackEnabled = true;
        bool m_OwnsNetworkManager = false;

        std::unique_ptr<NetworkManager> m_OwnedNetworkManager;
        std::unique_ptr<NetworkServer> m_DedicatedServer;

        std::unordered_map<std::string, SessionInstanceRecord> m_Sessions;
        std::unordered_map<std::string, SessionInviteRecord> m_Invites;
        std::string m_ActiveSessionId;
        std::atomic<uint64_t> m_IdSequence{ 1 };
    };

} // namespace Network
} // namespace Core

