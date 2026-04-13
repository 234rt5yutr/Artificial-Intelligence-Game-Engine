#include "Core/Network/NetworkServer.h"
#include "Core/Log.h"
#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"
#include "Core/Network/NetworkContractState.h"
#include "Core/Network/NetworkHash.h"
#include "Core/Network/Replay/NetworkReplayRecorder.h"
#include "Core/Network/RPC/NetworkRPCRegistry.h"
#include "Core/Profile.h"
#include <cstring>

#ifdef _MSC_VER
#define SAFE_STRCPY(dest, src, size) strncpy_s(dest, size, src, _TRUNCATE)
#else
#define SAFE_STRCPY(dest, src, size) do { std::strncpy(dest, src, size - 1); dest[size - 1] = '\0'; } while(0)
#endif

namespace Core {
namespace Network {

    // Static member initialization
    NetworkServer* NetworkServer::s_Instance = nullptr;

    NetworkServer::NetworkServer()
    {
        if (s_Instance != nullptr) {
            ENGINE_CORE_WARN("NetworkServer instance already exists, replacing");
        }
        s_Instance = this;
    }

    NetworkServer::~NetworkServer()
    {
        if (m_Running) {
            Stop();
        }
        s_Instance = nullptr;
    }

    bool NetworkServer::Start(const ServerConfig& config)
    {
        PROFILE_FUNCTION();

        if (m_Running) {
            ENGINE_CORE_WARN("NetworkServer already running");
            return false;
        }

        auto& netManager = NetworkManager::Get();
        if (!netManager.IsInitialized()) {
            ENGINE_CORE_ERROR("NetworkManager not initialized");
            return false;
        }

        m_Config = config;
        ISteamNetworkingSockets* sockets = netManager.GetSockets();

        // Create listen socket address
        SteamNetworkingIPAddr serverAddr;
        serverAddr.Clear();
        serverAddr.m_port = config.Port;

        // Configure socket options
        SteamNetworkingConfigValue_t options[2];
        options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                          reinterpret_cast<void*>(SteamNetConnectionStatusChangedCallback));
        options[1].SetInt32(k_ESteamNetworkingConfig_SendBufferSize, config.SendBufferSize);

        // Create the listen socket
        m_ListenSocket = sockets->CreateListenSocketIP(serverAddr, 2, options);
        if (m_ListenSocket == k_HSteamListenSocket_Invalid) {
            ENGINE_CORE_ERROR("Failed to create listen socket on port {}", config.Port);
            return false;
        }

        // Create poll group for efficient message polling
        m_PollGroup = sockets->CreatePollGroup();
        if (m_PollGroup == k_HSteamNetPollGroup_Invalid) {
            ENGINE_CORE_ERROR("Failed to create poll group");
            sockets->CloseListenSocket(m_ListenSocket);
            m_ListenSocket = k_HSteamListenSocket_Invalid;
            return false;
        }

        m_Running = true;
        m_NextClientId = 1;
        m_Clients.clear();
        m_ConnectionToClientId.clear();

        ENGINE_CORE_INFO("NetworkServer started on port {} (max {} clients)",
                         config.Port, config.MaxClients);
        return true;
    }

    void NetworkServer::Stop()
    {
        PROFILE_FUNCTION();

        if (!m_Running) {
            return;
        }

        ENGINE_CORE_INFO("Stopping NetworkServer...");

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();

        // Close all client connections
        for (auto& [clientId, info] : m_Clients) {
            if (info.Connection != k_HSteamNetConnection_Invalid) {
                sockets->CloseConnection(info.Connection, 0, "Server shutting down", true);
            }
        }
        m_Clients.clear();
        m_ConnectionToClientId.clear();

        // Destroy poll group
        if (m_PollGroup != k_HSteamNetPollGroup_Invalid) {
            sockets->DestroyPollGroup(m_PollGroup);
            m_PollGroup = k_HSteamNetPollGroup_Invalid;
        }

        // Close listen socket
        if (m_ListenSocket != k_HSteamListenSocket_Invalid) {
            sockets->CloseListenSocket(m_ListenSocket);
            m_ListenSocket = k_HSteamListenSocket_Invalid;
        }

        m_Running = false;
        ENGINE_CORE_INFO("NetworkServer stopped");
    }

    void NetworkServer::Update()
    {
        PROFILE_FUNCTION();

        if (!m_Running) {
            return;
        }

        // Poll for connection status changes (via callbacks)
        NetworkManager::Get().GetSockets()->RunCallbacks();

        // Process incoming messages
        ProcessIncomingMessages();

        // Check for handshake timeouts
        UpdateHandshakeTimeouts();
    }

    void NetworkServer::ProcessIncomingMessages()
    {
        PROFILE_FUNCTION();

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        const uint32_t replayFrameTick = NetworkDiagnosticsState::Get().GetSnapshot().LastServerTick;

        // Receive messages from all clients in the poll group
        constexpr int MAX_MESSAGES = 64;
        SteamNetworkingMessage_t* messages[MAX_MESSAGES];

        int numMessages = sockets->ReceiveMessagesOnPollGroup(m_PollGroup, messages, MAX_MESSAGES);

        for (int i = 0; i < numMessages; ++i) {
            SteamNetworkingMessage_t* msg = messages[i];

            // Find which client sent this message
            uint32_t clientId = FindClientIdByConnection(msg->m_conn);
            if (clientId != 0) {
                if (!m_SessionId.empty() && msg->m_cbSize > 0) {
                    uint32_t packetType = 0;
                    if (msg->m_cbSize >= static_cast<int>(sizeof(PacketHeader))) {
                        const auto* header = static_cast<const PacketHeader*>(msg->m_pData);
                        packetType = static_cast<uint32_t>(header->Type);
                    }

                    const uint64_t payloadHash = HashBytesFNV1a(msg->m_pData, static_cast<size_t>(msg->m_cbSize));
                    NetworkReplayRecorder::Get().RecordPacketSample(
                        m_SessionId,
                        ReplayPacketDirection::Inbound,
                        replayFrameTick,
                        0,
                        static_cast<uint32_t>(msg->m_cbSize),
                        payloadHash,
                        packetType);
                }

                auto it = m_Clients.find(clientId);
                if (it != m_Clients.end()) {
                    // Check if handshake is complete
                    if (it->second.HandshakeState != ClientHandshakeState::Completed) {
                        // Process as handshake packet
                        ProcessHandshakePacket(clientId, msg->m_pData, static_cast<uint32_t>(msg->m_cbSize));
                    }
                    else {
                        bool allowForward = true;
                        if (msg->m_cbSize >= static_cast<int>(sizeof(PacketHeader))) {
                            const auto* header = static_cast<const PacketHeader*>(msg->m_pData);
                            if (header->Type == PacketType::RemoteCall &&
                                msg->m_cbSize >= static_cast<int>(sizeof(RemoteCallPacket))) {
                                const auto* rpcPacket = static_cast<const RemoteCallPacket*>(msg->m_pData);
                                const NetworkRPCValidationResult validation =
                                    NetworkRPCRegistry::Get().ValidateInvocation(
                                        rpcPacket->FunctionHash,
                                        it->second.IsAuthenticated,
                                        false);
                                if (!validation.Allowed) {
                                    allowForward = false;

                                    NetworkDiagnosticsState::Get().RecordEvent(
                                        "RPCRejected: client=" + std::to_string(clientId) +
                                        " reason=" + validation.ErrorCode);

                                    RemoteCallResponsePacket response;
                                    response.Header.Type = PacketType::RemoteCallResponse;
                                    response.Header.Flags = static_cast<uint8_t>(PacketFlags::Reliable);
                                    response.Header.PayloadSize =
                                        static_cast<uint16_t>(sizeof(RemoteCallResponsePacket) - sizeof(PacketHeader));
                                    response.Header.SequenceNumber = 0;
                                    response.Header.Timestamp = static_cast<uint64_t>(NetworkManager::Get().GetCurrentTime());
                                    response.CallId = rpcPacket->CallId;
                                    response.ResultCode = (validation.ErrorCode == NET_RPC_AUTH_FAILED) ? 403U : 400U;
                                    response.PayloadSize = 0;

                                    sockets->SendMessageToConnection(
                                        msg->m_conn,
                                        &response,
                                        sizeof(response),
                                        k_nSteamNetworkingSend_Reliable,
                                        nullptr);
                                }
                            }
                        }

                        if (allowForward && m_OnMessage) {
                            // Forward to game message callback
                            m_OnMessage(clientId, msg->m_pData, msg->m_cbSize);
                        }
                    }
                }
            }

            // Release the message
            msg->Release();
        }
    }

    bool NetworkServer::SendToClient(uint32_t clientId, const void* data, uint32_t size, int sendFlags)
    {
        PROFILE_FUNCTION();

        auto it = m_Clients.find(clientId);
        if (it == m_Clients.end() || it->second.State != ConnectionState::Connected) {
            return false;
        }

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        EResult result = sockets->SendMessageToConnection(
            it->second.Connection,
            data,
            size,
            sendFlags,
            nullptr
        );

        if (result == k_EResultOK && !m_SessionId.empty() && data != nullptr && size > 0) {
            uint32_t packetType = 0;
            if (size >= sizeof(PacketHeader)) {
                const auto* header = static_cast<const PacketHeader*>(data);
                packetType = static_cast<uint32_t>(header->Type);
            }

            const uint64_t payloadHash = HashBytesFNV1a(data, size);
            const uint32_t replayFrameTick = NetworkDiagnosticsState::Get().GetSnapshot().LastServerTick;
            NetworkReplayRecorder::Get().RecordPacketSample(
                m_SessionId,
                ReplayPacketDirection::Outbound,
                replayFrameTick,
                0,
                size,
                payloadHash,
                packetType);
        }

        return result == k_EResultOK;
    }

    void NetworkServer::BroadcastToAll(const void* data, uint32_t size, int sendFlags)
    {
        PROFILE_FUNCTION();

        for (auto& [clientId, info] : m_Clients) {
            if (info.State == ConnectionState::Connected) {
                SendToClient(clientId, data, size, sendFlags);
            }
        }
    }

    void NetworkServer::BroadcastExcept(uint32_t excludeClientId, const void* data, uint32_t size, int sendFlags)
    {
        PROFILE_FUNCTION();

        for (auto& [clientId, info] : m_Clients) {
            if (clientId != excludeClientId && info.State == ConnectionState::Connected) {
                SendToClient(clientId, data, size, sendFlags);
            }
        }
    }

    void NetworkServer::DisconnectClient(uint32_t clientId, const char* reason)
    {
        PROFILE_FUNCTION();

        auto it = m_Clients.find(clientId);
        if (it == m_Clients.end()) {
            return;
        }

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        sockets->CloseConnection(it->second.Connection, 0, reason, true);

        // Note: Client will be removed from m_Clients in OnConnectionStatusChanged
    }

    uint32_t NetworkServer::GetClientCount() const
    {
        uint32_t count = 0;
        for (const auto& [id, info] : m_Clients) {
            if (info.State == ConnectionState::Connected) {
                ++count;
            }
        }
        return count;
    }

    const ClientInfo* NetworkServer::GetClientInfo(uint32_t clientId) const
    {
        auto it = m_Clients.find(clientId);
        return (it != m_Clients.end()) ? &it->second : nullptr;
    }

    std::vector<uint32_t> NetworkServer::GetConnectedClientIds() const
    {
        std::vector<uint32_t> ids;
        for (const auto& [clientId, info] : m_Clients) {
            if (info.State == ConnectionState::Connected) {
                ids.push_back(clientId);
            }
        }
        return ids;
    }

    HSteamNetConnection NetworkServer::GetClientConnection(uint32_t clientId) const
    {
        auto it = m_Clients.find(clientId);
        return (it != m_Clients.end()) ? it->second.Connection : k_HSteamNetConnection_Invalid;
    }

    void NetworkServer::UpdateClientStats(uint32_t clientId)
    {
        PROFILE_FUNCTION();

        auto it = m_Clients.find(clientId);
        if (it == m_Clients.end()) {
            return;
        }

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        SteamNetConnectionRealTimeStatus_t status;

        if (sockets->GetConnectionRealTimeStatus(it->second.Connection, &status, 0, nullptr)) {
            it->second.Stats.PingMs = static_cast<float>(status.m_nPing);
            it->second.Stats.PacketLossPercent = status.m_flConnectionQualityLocal * 100.0f;
            it->second.Stats.BytesSentPerSecond = status.m_flOutBytesPerSec;
            it->second.Stats.BytesReceivedPerSecond = status.m_flInBytesPerSec;
        }
    }

    void NetworkServer::SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo)
    {
        if (s_Instance) {
            s_Instance->OnConnectionStatusChanged(pInfo);
        }
    }

    void NetworkServer::OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
    {
        PROFILE_FUNCTION();

        if (!pInfo) {
            return;
        }

        switch (pInfo->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_Connecting: {
                // SECURITY: Rate limiting check
                uint32_t ipKey = ExtractIPKey(pInfo->m_hConn);
                if (ipKey != 0 && ShouldRateLimitConnection(ipKey)) {
                    NetworkManager::Get().GetSockets()->CloseConnection(
                        pInfo->m_hConn, 0, "Rate limited", false);
                    return;
                }

                // Clean up stale rate limit entries periodically
                CleanupStaleConnectionAttempts();

                // New incoming connection - accept or reject
                if (GetClientCount() >= m_Config.MaxClients) {
                    ENGINE_CORE_WARN("Rejecting connection: server full ({}/{})",
                                     GetClientCount(), m_Config.MaxClients);
                    NetworkManager::Get().GetSockets()->CloseConnection(
                        pInfo->m_hConn, 0, "Server full", false);
                }
                else {
                    AcceptConnection(pInfo->m_hConn);
                }
                break;
            }

            case k_ESteamNetworkingConnectionState_Connected: {
                uint32_t clientId = FindClientIdByConnection(pInfo->m_hConn);
                if (clientId != 0) {
                    auto it = m_Clients.find(clientId);
                    if (it != m_Clients.end()) {
                        it->second.State = ConnectionState::Connected;
                        ENGINE_CORE_INFO("Client {} connected from {}",
                                         clientId, it->second.Address);

                        if (m_OnClientConnected) {
                            m_OnClientConnected(clientId, it->second);
                        }
                    }
                }
                break;
            }

            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
                uint32_t clientId = FindClientIdByConnection(pInfo->m_hConn);
                if (clientId != 0) {
                    ENGINE_CORE_INFO("Client {} disconnected: {}",
                                     clientId, pInfo->m_info.m_szEndDebug);

                    if (m_OnClientDisconnected) {
                        m_OnClientDisconnected(clientId, pInfo->m_info.m_szEndDebug);
                    }

                    // Clean up
                    m_ConnectionToClientId.erase(pInfo->m_hConn);
                    m_Clients.erase(clientId);
                }

                // Close connection handle
                NetworkManager::Get().GetSockets()->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
                break;
            }

            default:
                break;
        }
    }

    bool NetworkServer::AcceptConnection(HSteamNetConnection connection)
    {
        PROFILE_FUNCTION();

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();

        // Accept the connection
        if (sockets->AcceptConnection(connection) != k_EResultOK) {
            ENGINE_CORE_ERROR("Failed to accept connection");
            sockets->CloseConnection(connection, 0, "Accept failed", false);
            return false;
        }

        // Add to poll group
        if (!sockets->SetConnectionPollGroup(connection, m_PollGroup)) {
            ENGINE_CORE_ERROR("Failed to add connection to poll group");
            sockets->CloseConnection(connection, 0, "Poll group failed", false);
            return false;
        }

        // Get connection info
        SteamNetConnectionInfo_t connInfo;
        sockets->GetConnectionInfo(connection, &connInfo);

        // Create client info
        uint32_t clientId = GenerateClientId();
        ClientInfo info;
        info.Connection = connection;
        info.State = ConnectionState::Connecting;
        info.HandshakeState = ClientHandshakeState::WaitingForHello;
        info.ClientId = clientId;
        info.ConnectedTimestamp = static_cast<uint64_t>(
            NetworkManager::Get().GetCurrentTime() / 1000); // Convert to milliseconds

        // Get address string
        char addrStr[SteamNetworkingIPAddr::k_cchMaxString];
        connInfo.m_addrRemote.ToString(addrStr, sizeof(addrStr), true);
        info.Address = addrStr;

        // Store client
        m_Clients[clientId] = info;
        m_ConnectionToClientId[connection] = clientId;

        ENGINE_CORE_INFO("Accepted connection from {} (client ID: {})", info.Address, clientId);
        return true;
    }

    uint32_t NetworkServer::GenerateClientId()
    {
        return m_NextClientId++;
    }

    uint32_t NetworkServer::FindClientIdByConnection(HSteamNetConnection connection) const
    {
        auto it = m_ConnectionToClientId.find(connection);
        return (it != m_ConnectionToClientId.end()) ? it->second : 0;
    }

    void NetworkServer::RejectClient(uint32_t clientId, RejectionReason reason, const char* message)
    {
        PROFILE_FUNCTION();

        auto it = m_Clients.find(clientId);
        if (it == m_Clients.end()) {
            return;
        }

        SendRejection(clientId, reason, message);
        
        // Close connection after sending rejection
        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        sockets->CloseConnection(it->second.Connection, static_cast<int>(reason), message, true);
    }

    void NetworkServer::ProcessHandshakePacket(uint32_t clientId, const void* data, uint32_t size)
    {
        PROFILE_FUNCTION();

        if (size < 1) {
            ENGINE_CORE_WARN("Client {}: Empty handshake packet", clientId);
            return;
        }

        auto packetType = *static_cast<const HandshakePacketType*>(data);

        switch (packetType) {
            case HandshakePacketType::ClientHello:
                if (size >= sizeof(ClientHelloPacket)) {
                    HandleClientHello(clientId, *static_cast<const ClientHelloPacket*>(data));
                }
                else {
                    ENGINE_CORE_WARN("Client {}: ClientHello packet too small: {} bytes", clientId, size);
                }
                break;

            case HandshakePacketType::ClientReady:
                if (size >= sizeof(ClientReadyPacket)) {
                    HandleClientReady(clientId, *static_cast<const ClientReadyPacket*>(data));
                }
                else {
                    ENGINE_CORE_WARN("Client {}: ClientReady packet too small: {} bytes", clientId, size);
                }
                break;

            default:
                ENGINE_CORE_WARN("Client {}: Unexpected handshake packet type: {}", 
                                 clientId, static_cast<int>(packetType));
                break;
        }
    }

    void NetworkServer::HandleClientHello(uint32_t clientId, const ClientHelloPacket& packet)
    {
        PROFILE_FUNCTION();

        auto it = m_Clients.find(clientId);
        if (it == m_Clients.end()) {
            return;
        }

        if (it->second.HandshakeState != ClientHandshakeState::WaitingForHello) {
            ENGINE_CORE_WARN("Client {}: Received ClientHello in unexpected state", clientId);
            return;
        }

        // Check protocol version
        if (packet.ProtocolVersion != NETWORK_PROTOCOL_VERSION) {
            ENGINE_CORE_WARN("Client {}: Protocol version mismatch (client: {}, server: {})",
                             clientId, packet.ProtocolVersion, NETWORK_PROTOCOL_VERSION);
            RejectClient(clientId, RejectionReason::VersionMismatch, "Protocol version mismatch");
            return;
        }

        const uint64_t serverContractHash = GetNetworkContractHash();
        const bool contractMismatch =
            serverContractHash != 0 && packet.ContractSignatureHash != serverContractHash;
        if (contractMismatch) {
            if (!IsBackwardsCompatibilityMode()) {
                ENGINE_CORE_WARN("Client {}: Contract hash mismatch (client: {}, server: {})",
                                 clientId,
                                 packet.ContractSignatureHash,
                                 serverContractHash);
                NetworkDiagnosticsState::Get().IncrementContractHashMismatch();
                NetworkDiagnosticsState::Get().RecordEvent("ContractMismatchRejected: client=" + std::to_string(clientId));
                RejectClient(clientId, RejectionReason::VersionMismatch, "Contract compatibility mismatch");
                return;
            }

            it->second.UsedCompatibilityDowngrade = true;
            NetworkDiagnosticsState::Get().IncrementContractHashMismatch();
            NetworkDiagnosticsState::Get().SetCompatibilityDowngradeActive(true);
            NetworkDiagnosticsState::Get().RecordEvent("ContractMismatchDowngrade: client=" + std::to_string(clientId));
        }

        // Store client info
        it->second.ClientName = packet.ClientName;
        it->second.ProtocolVersion = packet.ProtocolVersion;
        it->second.ContractHash = packet.ContractSignatureHash;
        NetworkDiagnosticsState::Get().SetContractHash(serverContractHash);

        ENGINE_CORE_INFO("Client {}: ClientHello received - Name: '{}', Protocol: {}, ContractHash: {}",
                         clientId, it->second.ClientName, packet.ProtocolVersion, packet.ContractSignatureHash);

        // Send welcome response
        SendServerWelcome(clientId);
    }

    void NetworkServer::HandleClientReady(uint32_t clientId, const ClientReadyPacket& packet)
    {
        PROFILE_FUNCTION();

        auto it = m_Clients.find(clientId);
        if (it == m_Clients.end()) {
            return;
        }

        if (it->second.HandshakeState != ClientHandshakeState::WaitingForReady) {
            ENGINE_CORE_WARN("Client {}: Received ClientReady in unexpected state", clientId);
            return;
        }

        // Verify client ID matches
        if (packet.AcknowledgedClientId != clientId) {
            ENGINE_CORE_WARN("Client {}: ClientReady ID mismatch (expected {}, got {})",
                             clientId, clientId, packet.AcknowledgedClientId);
            RejectClient(clientId, RejectionReason::InvalidClientInfo, "Client ID mismatch");
            return;
        }

        ENGINE_CORE_INFO("Client {}: ClientReady received", clientId);

        // Complete handshake
        SendServerReady(clientId);
    }

    void NetworkServer::SendServerWelcome(uint32_t clientId)
    {
        PROFILE_FUNCTION();

        auto it = m_Clients.find(clientId);
        if (it == m_Clients.end()) {
            return;
        }

        ServerWelcomePacket packet;
        packet.Type = HandshakePacketType::ServerWelcome;
        packet.AssignedClientId = clientId;
        packet.ServerTickRate = m_Config.TickRate;
        packet.ContractSignatureHash = GetNetworkContractHash();
        packet.CompatibilityMode = IsBackwardsCompatibilityMode() ? 1 : 0;

        SAFE_STRCPY(packet.ServerName, m_Config.ServerName.c_str(), sizeof(packet.ServerName));
        SAFE_STRCPY(packet.Message, m_Config.WelcomeMessage.c_str(), sizeof(packet.Message));

        std::memset(packet.Reserved, 0, sizeof(packet.Reserved));

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        EResult result = sockets->SendMessageToConnection(
            it->second.Connection,
            &packet,
            sizeof(packet),
            k_nSteamNetworkingSend_Reliable,
            nullptr
        );

        if (result == k_EResultOK) {
            ENGINE_CORE_TRACE("Client {}: Sent ServerWelcome", clientId);
            it->second.HandshakeState = ClientHandshakeState::WaitingForReady;
        }
        else {
            ENGINE_CORE_ERROR("Client {}: Failed to send ServerWelcome", clientId);
            it->second.HandshakeState = ClientHandshakeState::Failed;
        }
    }

    void NetworkServer::SendServerReady(uint32_t clientId)
    {
        PROFILE_FUNCTION();

        auto it = m_Clients.find(clientId);
        if (it == m_Clients.end()) {
            return;
        }

        ServerReadyPacket packet;
        packet.Type = HandshakePacketType::ServerReady;
        packet.ServerTimestamp = static_cast<uint64_t>(NetworkManager::Get().GetCurrentTime());
        std::memset(packet.Reserved, 0, sizeof(packet.Reserved));

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        EResult result = sockets->SendMessageToConnection(
            it->second.Connection,
            &packet,
            sizeof(packet),
            k_nSteamNetworkingSend_Reliable,
            nullptr
        );

        if (result == k_EResultOK) {
            ENGINE_CORE_TRACE("Client {}: Sent ServerReady", clientId);
            it->second.HandshakeState = ClientHandshakeState::Completed;
            it->second.State = ConnectionState::Connected;

            ENGINE_CORE_INFO("Client {} '{}' handshake completed, fully connected",
                             clientId, it->second.ClientName);

            // Invoke connected callback now that handshake is complete
            if (m_OnClientConnected) {
                m_OnClientConnected(clientId, it->second);
            }
        }
        else {
            ENGINE_CORE_ERROR("Client {}: Failed to send ServerReady", clientId);
            it->second.HandshakeState = ClientHandshakeState::Failed;
        }
    }

    void NetworkServer::SendRejection(uint32_t clientId, RejectionReason reason, const char* message)
    {
        PROFILE_FUNCTION();

        auto it = m_Clients.find(clientId);
        if (it == m_Clients.end()) {
            return;
        }

        RejectionPacket packet;
        packet.Type = HandshakePacketType::Rejected;
        packet.ReasonCode = static_cast<uint32_t>(reason);

        if (message) {
            SAFE_STRCPY(packet.Reason, message, sizeof(packet.Reason));
        }
        else {
            // Default reason messages
            switch (reason) {
                case RejectionReason::ServerFull:
                    SAFE_STRCPY(packet.Reason, "Server is full", sizeof(packet.Reason));
                    break;
                case RejectionReason::VersionMismatch:
                    SAFE_STRCPY(packet.Reason, "Protocol version mismatch", sizeof(packet.Reason));
                    break;
                case RejectionReason::Banned:
                    SAFE_STRCPY(packet.Reason, "You are banned from this server", sizeof(packet.Reason));
                    break;
                case RejectionReason::InvalidClientInfo:
                    SAFE_STRCPY(packet.Reason, "Invalid client information", sizeof(packet.Reason));
                    break;
                case RejectionReason::Timeout:
                    SAFE_STRCPY(packet.Reason, "Connection timed out", sizeof(packet.Reason));
                    break;
                case RejectionReason::ServerShuttingDown:
                    SAFE_STRCPY(packet.Reason, "Server is shutting down", sizeof(packet.Reason));
                    break;
                default:
                    SAFE_STRCPY(packet.Reason, "Connection rejected", sizeof(packet.Reason));
                    break;
            }
        }

        std::memset(packet.Reserved, 0, sizeof(packet.Reserved));

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        sockets->SendMessageToConnection(
            it->second.Connection,
            &packet,
            sizeof(packet),
            k_nSteamNetworkingSend_Reliable,
            nullptr
        );

        ENGINE_CORE_INFO("Client {}: Sent rejection - {}", clientId, packet.Reason);
        it->second.HandshakeState = ClientHandshakeState::Failed;
    }

    void NetworkServer::UpdateHandshakeTimeouts()
    {
        PROFILE_FUNCTION();

        uint64_t currentTime = static_cast<uint64_t>(NetworkManager::Get().GetCurrentTime() / 1000);
        std::vector<uint32_t> timedOutClients;

        for (auto& [clientId, info] : m_Clients) {
            // Check only clients in handshake state
            if (info.HandshakeState != ClientHandshakeState::Completed &&
                info.HandshakeState != ClientHandshakeState::Failed) {
                
                uint64_t elapsed = currentTime - info.ConnectedTimestamp;
                if (elapsed >= m_Config.HandshakeTimeoutMs) {
                    ENGINE_CORE_WARN("Client {}: Handshake timed out after {}ms", clientId, elapsed);
                    timedOutClients.push_back(clientId);
                }
            }
        }

        // Reject timed out clients
        for (uint32_t clientId : timedOutClients) {
            RejectClient(clientId, RejectionReason::Timeout, "Handshake timed out");
        }
    }

    //==========================================================================
    // Security: Rate Limiting
    //==========================================================================

    bool NetworkServer::ShouldRateLimitConnection(uint32_t ipKey)
    {
        uint64_t now = static_cast<uint64_t>(NetworkManager::Get().GetCurrentTime() / 1000);
        
        auto it = m_ConnectionAttempts.find(ipKey);
        if (it == m_ConnectionAttempts.end()) {
            // First connection from this IP
            m_ConnectionAttempts[ipKey] = { 1, now };
            return false;
        }

        ConnectionAttempt& attempt = it->second;
        
        // Reset if window has passed
        if (now - attempt.FirstAttemptTime > CONNECTION_RATE_WINDOW_MS) {
            attempt.Count = 1;
            attempt.FirstAttemptTime = now;
            return false;
        }

        // Increment and check limit
        attempt.Count++;
        if (attempt.Count > MAX_CONNECTIONS_PER_IP) {
            ENGINE_CORE_WARN("Rate limiting connection from IP key: {} (attempts: {})",
                             ipKey, attempt.Count);
            return true;
        }

        return false;
    }

    void NetworkServer::CleanupStaleConnectionAttempts()
    {
        uint64_t now = static_cast<uint64_t>(NetworkManager::Get().GetCurrentTime() / 1000);
        
        // Only cleanup every 30 seconds
        if (now - m_LastRateLimitCleanup < 30000) {
            return;
        }
        m_LastRateLimitCleanup = now;

        // Remove entries older than 2x the rate window
        auto it = m_ConnectionAttempts.begin();
        while (it != m_ConnectionAttempts.end()) {
            if (now - it->second.FirstAttemptTime > CONNECTION_RATE_WINDOW_MS * 2) {
                it = m_ConnectionAttempts.erase(it);
            } else {
                ++it;
            }
        }
    }

    uint32_t NetworkServer::ExtractIPKey(HSteamNetConnection connection) const
    {
        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        SteamNetConnectionInfo_t connInfo;
        if (!sockets->GetConnectionInfo(connection, &connInfo)) {
            return 0;
        }
        
        // Extract IP as a simple hash key (IPv4 focused, but works for IPv6 too)
        uint32_t ipKey = 0;
        const uint8_t* addr = connInfo.m_addrRemote.m_ipv6;
        for (int i = 0; i < 16; i += 4) {
            ipKey ^= (static_cast<uint32_t>(addr[i]) << 24) |
                     (static_cast<uint32_t>(addr[i+1]) << 16) |
                     (static_cast<uint32_t>(addr[i+2]) << 8) |
                     static_cast<uint32_t>(addr[i+3]);
        }
        return ipKey;
    }

} // namespace Network
} // namespace Core
