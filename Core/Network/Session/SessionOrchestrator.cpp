#include "Core/Network/Session/SessionOrchestrator.h"

#include "Core/Log.h"
#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"
#include "Core/Network/NetworkContractState.h"
#include "Core/Network/NetworkHash.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>

namespace Core {
namespace Network {

    namespace {

        constexpr std::array<char, 32> INVITE_ALPHABET = {
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
            'J', 'K', 'L', 'M', 'N', 'P', 'Q', 'R',
            'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
            '2', '3', '4', '5', '6', '7', '8', '9'
        };

        constexpr uint32_t INVITE_DEFAULT_USES = 64;
        constexpr uint64_t INVITE_EXPIRY_MS = 24ULL * 60ULL * 60ULL * 1000ULL;
        constexpr uint32_t MIN_TICK_RATE = 10;
        constexpr uint32_t MAX_TICK_RATE = 240;

    } // namespace

    SessionOrchestrator& SessionOrchestrator::Get() {
        static SessionOrchestrator instance;
        return instance;
    }

    DedicatedServerStartResult SessionOrchestrator::StartDedicatedServerInstance(const DedicatedServerStartRequest& request) {
        std::scoped_lock lock(m_Mutex);

        if (request.RuntimeProfile != "dedicated" || !request.Headless) {
            return MakeStartFailure(
                NET_DEDICATED_PROFILE_INVALID,
                "Dedicated startup requires runtimeProfile='dedicated' with headless=true.");
        }

        if (request.Port == 0 || request.MaxClients == 0 || request.TickRate < MIN_TICK_RATE || request.TickRate > MAX_TICK_RATE) {
            return MakeStartFailure(
                NET_DEDICATED_PROFILE_INVALID,
                "Dedicated startup validation failed (port/maxClients/tickRate).");
        }

        if (m_DedicatedServer != nullptr && m_DedicatedServer->IsRunning()) {
            return MakeStartFailure(
                NET_DEDICATED_PROFILE_INVALID,
                "A dedicated server instance is already running.");
        }

        NetworkManager* networkManager = nullptr;
        m_OwnsNetworkManager = false;

        if (NetworkManager::HasInstance()) {
            networkManager = &NetworkManager::Get();
        } else {
            m_OwnedNetworkManager = std::make_unique<NetworkManager>();
            networkManager = m_OwnedNetworkManager.get();
            m_OwnsNetworkManager = true;
        }

        if (networkManager == nullptr || (!networkManager->IsInitialized() && !networkManager->Initialize())) {
            return MakeStartFailure(
                NET_DEDICATED_PORT_BIND_FAILED,
                "Failed to initialize network runtime for dedicated server.");
        }

        m_DedicatedServer = std::make_unique<NetworkServer>();
        ServerConfig serverConfig;
        serverConfig.Port = request.Port;
        serverConfig.MaxClients = request.MaxClients;
        serverConfig.TickRate = request.TickRate;
        serverConfig.ServerName = request.ServerName;
        serverConfig.WelcomeMessage = "Dedicated session active";
        serverConfig.AllowP2P = false;

        if (!m_DedicatedServer->Start(serverConfig)) {
            if (m_OwnsNetworkManager && networkManager->IsInitialized()) {
                networkManager->Shutdown();
            }
            if (m_OwnsNetworkManager) {
                m_OwnedNetworkManager.reset();
            }
            m_DedicatedServer.reset();
            return MakeStartFailure(
                NET_DEDICATED_PORT_BIND_FAILED,
                "Dedicated server failed to bind the requested address/port.");
        }

        const uint64_t nowMs = GetNowUnixMilliseconds();
        SessionInstanceRecord sessionRecord;
        sessionRecord.SessionId = GenerateSessionId(request.ServerName, request.Port);
        sessionRecord.ServerName = request.ServerName;
        sessionRecord.RuntimeProfile = request.RuntimeProfile;
        sessionRecord.BindAddress = request.BindAddress;
        sessionRecord.Port = request.Port;
        sessionRecord.MaxClients = request.MaxClients;
        sessionRecord.ConnectedClients = 0;
        sessionRecord.TickRate = request.TickRate;
        sessionRecord.IsDedicated = true;
        sessionRecord.IsListenServer = false;
        sessionRecord.CreatedAtUnixMs = nowMs;
        sessionRecord.Compatibility.ProtocolVersion = NETWORK_PROTOCOL_VERSION;
        sessionRecord.Compatibility.ContractHash = GetNetworkContractHash();
        sessionRecord.Compatibility.BuildCompatibilityHash =
            request.BuildCompatibilityHash.empty() ? "build-default" : request.BuildCompatibilityHash;
        sessionRecord.Compatibility.BackwardsCompatibilityMode = IsBackwardsCompatibilityMode();

        SessionInviteRecord inviteRecord;
        inviteRecord.InviteCode = GenerateInviteCode(sessionRecord.SessionId, nowMs);
        inviteRecord.SessionId = sessionRecord.SessionId;
        inviteRecord.ExpiresAtUnixMs = nowMs + INVITE_EXPIRY_MS;
        inviteRecord.RemainingUses = INVITE_DEFAULT_USES;
        inviteRecord.CompatibilityHash = sessionRecord.Compatibility.ContractHash;
        inviteRecord.RequiresAuthToken = false;
        sessionRecord.InviteCode = inviteRecord.InviteCode;

        m_Sessions.clear();
        m_Invites.clear();
        m_Sessions.emplace(sessionRecord.SessionId, sessionRecord);
        m_Invites.emplace(inviteRecord.InviteCode, inviteRecord);
        m_ActiveSessionId = sessionRecord.SessionId;
        m_RuntimeState = SessionRuntimeState::Hosting;

        m_DedicatedServer->SetSessionId(sessionRecord.SessionId);

        NetworkDiagnosticsState& diagnostics = NetworkDiagnosticsState::Get();
        diagnostics.SetSessionState(sessionRecord.SessionId, "dedicated", true);
        diagnostics.SetContractHash(sessionRecord.Compatibility.ContractHash);
        diagnostics.RecordEvent("DedicatedServerStarted: " + sessionRecord.SessionId);

        DedicatedServerStartResult result;
        result.Success = true;
        result.Message = "Dedicated server started.";
        result.Session = sessionRecord;
        return result;
    }

    void SessionOrchestrator::StopDedicatedServerInstance(const std::string& stopReason) {
        std::scoped_lock lock(m_Mutex);

        if (m_DedicatedServer != nullptr && m_DedicatedServer->IsRunning()) {
            m_DedicatedServer->Stop();
        }
        m_DedicatedServer.reset();

        if (m_OwnsNetworkManager && m_OwnedNetworkManager != nullptr && m_OwnedNetworkManager->IsInitialized()) {
            m_OwnedNetworkManager->Shutdown();
        }
        if (m_OwnsNetworkManager) {
            m_OwnedNetworkManager.reset();
            m_OwnsNetworkManager = false;
        }

        m_RuntimeState = SessionRuntimeState::Idle;
        m_Sessions.clear();
        m_Invites.clear();
        m_ActiveSessionId.clear();

        NetworkDiagnosticsState& diagnostics = NetworkDiagnosticsState::Get();
        diagnostics.SetSessionState("", "client", false);
        diagnostics.RecordEvent("DedicatedServerStopped: " + stopReason);
    }

    SessionJoinResult SessionOrchestrator::JoinSessionByInviteCode(const SessionInviteJoinRequest& request) {
        std::scoped_lock lock(m_Mutex);

        NetworkDiagnosticsState& diagnostics = NetworkDiagnosticsState::Get();

        const SessionRuntimeState previousState = m_RuntimeState;
        m_RuntimeState = SessionRuntimeState::Joining;

        const std::string normalizedInviteCode = NormalizeInviteCode(request.InviteCode);
        if (!IsInviteCodeWellFormed(normalizedInviteCode)) {
            m_RuntimeState = previousState;
            diagnostics.RecordInviteJoinAttempt(false);
            diagnostics.RecordEvent("SessionJoinRejected: malformed invite code");
            return MakeJoinFailure(
                NET_INVITE_CODE_INVALID,
                "Invite code format is invalid.");
        }

        auto inviteIt = m_Invites.find(normalizedInviteCode);
        if (inviteIt == m_Invites.end()) {
            std::string directAddress;
            uint16_t directPort = 0;
            if (m_LegacyFallbackEnabled && TryParseDirectConnectInvite(request.InviteCode, directAddress, directPort)) {
                SessionJoinResult result;
                result.Success = true;
                result.UsedLegacyDirectConnectFallback = true;
                result.Message = "Invite not found; using legacy direct-connect fallback.";
                result.ClientConfiguration.ServerAddress = directAddress;
                result.ClientConfiguration.ServerPort = directPort;
                result.Session.SessionId = "legacy-" + std::to_string(HashStringTo32(request.InviteCode));
                result.Session.ServerName = "Legacy Direct Connect";
                result.Session.RuntimeProfile = "legacy-fallback";
                result.Session.BindAddress = directAddress;
                result.Session.Port = directPort;
                result.Session.Compatibility.ProtocolVersion = request.ProtocolVersion;
                result.Session.Compatibility.BuildCompatibilityHash = request.BuildCompatibilityHash;
                result.Session.Compatibility.ContractHash = GetNetworkContractHash();
                result.Session.Compatibility.BackwardsCompatibilityMode = true;
                result.Session.CreatedAtUnixMs = GetNowUnixMilliseconds();

                m_RuntimeState = SessionRuntimeState::LegacyFallback;
                m_ActiveSessionId = result.Session.SessionId;

                diagnostics.RecordInviteJoinAttempt(true);
                diagnostics.SetSessionState(result.Session.SessionId, "legacy-fallback", false);
                diagnostics.RecordEvent("SessionJoinFallback: " + directAddress + ":" + std::to_string(directPort));
                return result;
            }

            m_RuntimeState = previousState;
            diagnostics.RecordInviteJoinAttempt(false);
            diagnostics.RecordEvent("SessionJoinRejected: invite lookup miss");
            return MakeJoinFailure(
                NET_INVITE_CODE_INVALID,
                "Invite code was not found.");
        }

        const uint64_t nowMs = GetNowUnixMilliseconds();
        SessionInviteRecord& invite = inviteIt->second;
        if (invite.ExpiresAtUnixMs <= nowMs || invite.RemainingUses == 0) {
            m_RuntimeState = previousState;
            diagnostics.RecordInviteJoinAttempt(false);
            diagnostics.RecordEvent("SessionJoinRejected: invite expired/exhausted");
            return MakeJoinFailure(
                NET_INVITE_EXPIRED_OR_EXHAUSTED,
                "Invite code is expired or has no remaining uses.");
        }

        auto sessionIt = m_Sessions.find(invite.SessionId);
        if (sessionIt == m_Sessions.end()) {
            m_RuntimeState = previousState;
            diagnostics.RecordInviteJoinAttempt(false);
            diagnostics.RecordEvent("SessionJoinRejected: invite references unknown session");
            return MakeJoinFailure(
                NET_INVITE_CODE_INVALID,
                "Invite code references a session that is no longer available.");
        }

        const SessionInstanceRecord& session = sessionIt->second;
        if (invite.CompatibilityHash != 0 &&
            invite.CompatibilityHash != session.Compatibility.ContractHash) {
            m_RuntimeState = previousState;
            diagnostics.RecordInviteJoinAttempt(false);
            diagnostics.RecordEvent("SessionJoinRejected: invite contract hash stale");
            return MakeJoinFailure(
                NET_INVITE_CODE_INVALID,
                "Invite compatibility hash is stale for current session contract.");
        }

        if (request.ProtocolVersion != session.Compatibility.ProtocolVersion) {
            m_RuntimeState = previousState;
            diagnostics.RecordInviteJoinAttempt(false);
            diagnostics.RecordEvent("SessionJoinRejected: protocol mismatch");
            return MakeJoinFailure(
                NET_INVITE_CODE_INVALID,
                "Protocol version does not match target session.");
        }

        const bool buildMatch = request.BuildCompatibilityHash.empty() ||
            request.BuildCompatibilityHash == session.Compatibility.BuildCompatibilityHash;
        bool compatibilityDowngrade = false;
        if (!buildMatch) {
            if (session.Compatibility.BackwardsCompatibilityMode || IsBackwardsCompatibilityMode()) {
                compatibilityDowngrade = true;
                diagnostics.SetCompatibilityDowngradeActive(true);
                diagnostics.RecordEvent("SessionJoinCompatibilityDowngrade: build hash mismatch accepted");
            } else {
                m_RuntimeState = previousState;
                diagnostics.RecordInviteJoinAttempt(false);
                diagnostics.RecordEvent("SessionJoinRejected: build compatibility mismatch");
                return MakeJoinFailure(
                    NET_INVITE_CODE_INVALID,
                    "Build compatibility hash mismatch.");
            }
        }

        if (invite.RequiresAuthToken) {
            if (!request.AuthToken.has_value() || request.AuthToken.value() != invite.RequiredAuthToken) {
                m_RuntimeState = previousState;
                diagnostics.RecordInviteJoinAttempt(false);
                diagnostics.RecordEvent("SessionJoinRejected: auth token mismatch");
                return MakeJoinFailure(
                    NET_INVITE_CODE_INVALID,
                    "Invite authorization token is invalid.");
            }
        }

        if (invite.RemainingUses > 0) {
            --invite.RemainingUses;
        }

        SessionJoinResult result;
        result.Success = true;
        result.Message = "Join validation complete.";
        result.Session = session;
        result.ClientConfiguration.ServerAddress = session.BindAddress;
        result.ClientConfiguration.ServerPort = session.Port;
        result.UsedCompatibilityDowngrade = compatibilityDowngrade;

        m_RuntimeState = SessionRuntimeState::Connected;
        m_ActiveSessionId = session.SessionId;

        diagnostics.RecordInviteJoinAttempt(true);
        diagnostics.SetSessionState(session.SessionId, "client", false);
        diagnostics.RecordEvent("SessionJoinCompleted: " + session.SessionId);
        return result;
    }

    LANDiscoveryResult SessionOrchestrator::DiscoverLANSessions(const LANDiscoveryRequest& request) {
        std::scoped_lock lock(m_Mutex);

        if (request.MaxResults == 0) {
            return MakeDiscoveryFailure(
                NET_LAN_DISCOVERY_TIMEOUT,
                "maxResults must be greater than zero.");
        }

        std::vector<LANDiscoveryRecord> candidates;
        candidates.reserve(m_Sessions.size());

        for (const auto& [sessionId, session] : m_Sessions) {
            (void)sessionId;

            if (session.IsDedicated && !request.IncludeDedicatedServers) {
                continue;
            }

            if (session.IsListenServer && !request.IncludeListenServers) {
                continue;
            }

            const bool versionMatch = !request.RequireProtocolVersion.has_value() ||
                request.RequireProtocolVersion.value() == session.Compatibility.ProtocolVersion;
            if (!versionMatch) {
                continue;
            }

            const bool buildMatch = !request.RequireBuildCompatibilityHash.has_value() ||
                request.RequireBuildCompatibilityHash.value() == session.Compatibility.BuildCompatibilityHash;
            if (!buildMatch) {
                continue;
            }

            LANDiscoveryRecord record;
            record.Session = session;
            record.EstimatedPingMs = 2U + (HashStringTo32(session.SessionId) % 110U);
            record.VersionMatch = versionMatch;
            record.BuildMatch = buildMatch;
            record.AvailableSlots = (session.MaxClients > session.ConnectedClients)
                ? (session.MaxClients - session.ConnectedClients) : 0;
            candidates.push_back(record);
        }

        std::sort(candidates.begin(), candidates.end(), [](const LANDiscoveryRecord& left, const LANDiscoveryRecord& right) {
            if (left.VersionMatch != right.VersionMatch) {
                return left.VersionMatch > right.VersionMatch;
            }
            if (left.BuildMatch != right.BuildMatch) {
                return left.BuildMatch > right.BuildMatch;
            }
            if (left.EstimatedPingMs != right.EstimatedPingMs) {
                return left.EstimatedPingMs < right.EstimatedPingMs;
            }
            if (left.AvailableSlots != right.AvailableSlots) {
                return left.AvailableSlots > right.AvailableSlots;
            }
            return left.Session.SessionId < right.Session.SessionId;
        });

        if (candidates.size() > request.MaxResults) {
            candidates.resize(request.MaxResults);
        }

        const bool timedOut = request.TimeoutMs <= 10 && !candidates.empty();
        if (timedOut && candidates.size() > 1) {
            candidates.resize(candidates.size() / 2);
        }

        NetworkDiagnosticsState& diagnostics = NetworkDiagnosticsState::Get();
        diagnostics.RecordLANDiscoveryQuery(timedOut);
        diagnostics.RecordEvent(timedOut
            ? "LANSessionsDiscovered: partial timeout result"
            : "LANSessionsDiscovered: complete");

        LANDiscoveryResult result;
        result.Success = true;
        result.TimedOut = timedOut;
        result.Sessions = std::move(candidates);
        result.ErrorCode = timedOut ? NET_LAN_DISCOVERY_TIMEOUT : "";
        result.Message = timedOut
            ? "LAN discovery timed out; returning partial results."
            : "LAN discovery completed.";
        return result;
    }

    SessionRuntimeState SessionOrchestrator::GetRuntimeState() const {
        std::scoped_lock lock(m_Mutex);
        return m_RuntimeState;
    }

    std::optional<SessionInstanceRecord> SessionOrchestrator::GetActiveSessionRecord() const {
        std::scoped_lock lock(m_Mutex);
        auto it = m_Sessions.find(m_ActiveSessionId);
        if (it == m_Sessions.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::vector<SessionInstanceRecord> SessionOrchestrator::GetKnownSessionRecords() const {
        std::scoped_lock lock(m_Mutex);

        std::vector<SessionInstanceRecord> sessions;
        sessions.reserve(m_Sessions.size());
        for (const auto& [sessionId, session] : m_Sessions) {
            (void)sessionId;
            sessions.push_back(session);
        }

        std::sort(sessions.begin(), sessions.end(), [](const SessionInstanceRecord& left, const SessionInstanceRecord& right) {
            return left.SessionId < right.SessionId;
        });
        return sessions;
    }

    void SessionOrchestrator::SetLegacyFallbackEnabled(bool enabled) {
        std::scoped_lock lock(m_Mutex);
        m_LegacyFallbackEnabled = enabled;
    }

    bool SessionOrchestrator::IsLegacyFallbackEnabled() const {
        std::scoped_lock lock(m_Mutex);
        return m_LegacyFallbackEnabled;
    }

    DedicatedServerStartResult SessionOrchestrator::MakeStartFailure(
        const std::string& errorCode,
        const std::string& message) const {
        DedicatedServerStartResult result;
        result.Success = false;
        result.ErrorCode = errorCode;
        result.Message = message;
        return result;
    }

    SessionJoinResult SessionOrchestrator::MakeJoinFailure(
        const std::string& errorCode,
        const std::string& message) const {
        SessionJoinResult result;
        result.Success = false;
        result.ErrorCode = errorCode;
        result.Message = message;
        return result;
    }

    LANDiscoveryResult SessionOrchestrator::MakeDiscoveryFailure(
        const std::string& errorCode,
        const std::string& message) const {
        LANDiscoveryResult result;
        result.Success = false;
        result.ErrorCode = errorCode;
        result.Message = message;
        return result;
    }

    std::string SessionOrchestrator::GenerateSessionId(const std::string& serverName, uint16_t port) {
        const uint64_t nowMs = GetNowUnixMilliseconds();
        const uint64_t sequence = m_IdSequence.fetch_add(1, std::memory_order_relaxed);
        const uint64_t nameHash = HashStringFNV1a(serverName, true);
        const uint64_t sessionHash = HashCombineFNV1a(nameHash, static_cast<uint64_t>(port) ^ sequence ^ nowMs);
        return "sess-" + std::to_string(nowMs) + "-" + std::to_string(sessionHash & 0xFFFFFFULL);
    }

    std::string SessionOrchestrator::GenerateInviteCode(const std::string& sessionId, uint64_t createdAtUnixMs) {
        uint64_t rollingHash = HashCombineFNV1a(HashStringFNV1a(sessionId, true), createdAtUnixMs);
        std::string inviteCode;
        inviteCode.reserve(8);
        for (size_t index = 0; index < 8; ++index) {
            const size_t alphabetIndex = static_cast<size_t>(rollingHash % INVITE_ALPHABET.size());
            inviteCode.push_back(INVITE_ALPHABET[alphabetIndex]);
            rollingHash = HashCombineFNV1a(rollingHash, static_cast<uint64_t>(index + 1));
        }
        return inviteCode;
    }

    std::string SessionOrchestrator::NormalizeInviteCode(const std::string& inviteCode) const {
        std::string normalized;
        normalized.reserve(inviteCode.size());
        for (char character : inviteCode) {
            unsigned char safeCharacter = static_cast<unsigned char>(character);
            if (std::isspace(safeCharacter) != 0) {
                continue;
            }
            normalized.push_back(static_cast<char>(std::toupper(safeCharacter)));
        }
        return normalized;
    }

    bool SessionOrchestrator::IsInviteCodeWellFormed(const std::string& inviteCode) const {
        if (inviteCode.size() < 4 || inviteCode.size() > 32) {
            return false;
        }
        for (char character : inviteCode) {
            unsigned char safeCharacter = static_cast<unsigned char>(character);
            if (!(std::isalnum(safeCharacter) != 0 || character == '-' || character == ':')) {
                return false;
            }
        }
        return true;
    }

    bool SessionOrchestrator::TryParseDirectConnectInvite(
        const std::string& inviteCode,
        std::string& outAddress,
        uint16_t& outPort) const {
        const size_t colonIndex = inviteCode.rfind(':');
        if (colonIndex == std::string::npos || colonIndex == 0 || colonIndex >= inviteCode.size() - 1) {
            return false;
        }

        const std::string address = inviteCode.substr(0, colonIndex);
        const std::string portText = inviteCode.substr(colonIndex + 1);
        if (address.empty() || portText.empty()) {
            return false;
        }

        uint32_t parsedPort = 0;
        const char* begin = portText.data();
        const char* end = portText.data() + portText.size();
        const auto parseResult = std::from_chars(begin, end, parsedPort);
        if (parseResult.ec != std::errc() || parseResult.ptr != end || parsedPort == 0 || parsedPort > 65535) {
            return false;
        }

        outAddress = address;
        outPort = static_cast<uint16_t>(parsedPort);
        return true;
    }

    uint64_t SessionOrchestrator::GetNowUnixMilliseconds() const {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

} // namespace Network
} // namespace Core

