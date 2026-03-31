#include "Core/Network/NetworkServer.h"
#include "Core/Log.h"
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

        // Receive messages from all clients in the poll group
        constexpr int MAX_MESSAGES = 64;
        SteamNetworkingMessage_t* messages[MAX_MESSAGES];

        int numMessages = sockets->ReceiveMessagesOnPollGroup(m_PollGroup, messages, MAX_MESSAGES);

        for (int i = 0; i < numMessages; ++i) {
            SteamNetworkingMessage_t* msg = messages[i];

            // Find which client sent this message
            uint32_t clientId = FindClientIdByConnection(msg->m_conn);
            if (clientId != 0) {
                auto it = m_Clients.find(clientId);
                if (it != m_Clients.end()) {
                    // Check if handshake is complete
                    if (it->second.HandshakeState != ClientHandshakeState::Completed) {
                        // Process as handshake packet
                        ProcessHandshakePacket(clientId, msg->m_pData, static_cast<uint32_t>(msg->m_cbSize));
                    }
                    else if (m_OnMessage) {
                        // Forward to game message callback
                        m_OnMessage(clientId, msg->m_pData, msg->m_cbSize);
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
            case k_ESteamNetworkingConnectionState_Connecting:
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
        if (packet.ProtocolVersion != 1) {
            ENGINE_CORE_WARN("Client {}: Protocol version mismatch (client: {}, server: 1)",
                             clientId, packet.ProtocolVersion);
            RejectClient(clientId, RejectionReason::VersionMismatch, "Protocol version mismatch");
            return;
        }

        // Store client info
        it->second.ClientName = packet.ClientName;
        it->second.ProtocolVersion = packet.ProtocolVersion;

        ENGINE_CORE_INFO("Client {}: ClientHello received - Name: '{}', Protocol: {}",
                         clientId, it->second.ClientName, packet.ProtocolVersion);

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

} // namespace Network
} // namespace Core
