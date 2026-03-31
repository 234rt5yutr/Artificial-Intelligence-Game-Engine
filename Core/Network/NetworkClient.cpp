#include "Core/Network/NetworkClient.h"
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
    NetworkClient* NetworkClient::s_Instance = nullptr;

    NetworkClient::NetworkClient()
    {
        if (s_Instance != nullptr) {
            ENGINE_CORE_WARN("NetworkClient instance already exists, replacing");
        }
        s_Instance = this;
    }

    NetworkClient::~NetworkClient()
    {
        if (m_State != ConnectionState::Disconnected) {
            Disconnect("Client destroyed");
        }
        s_Instance = nullptr;
    }

    bool NetworkClient::Connect(const ClientConfig& config, const char* clientName)
    {
        PROFILE_FUNCTION();

        if (m_State != ConnectionState::Disconnected) {
            ENGINE_CORE_WARN("NetworkClient::Connect called while already connected/connecting");
            return false;
        }

        auto& netManager = NetworkManager::Get();
        if (!netManager.IsInitialized()) {
            ENGINE_CORE_ERROR("NetworkManager not initialized");
            return false;
        }

        m_Config = config;
        m_ClientName = clientName ? clientName : "Player";
        m_ReconnectAttempts = 0;

        // Parse server address
        SteamNetworkingIPAddr serverAddr;
        serverAddr.Clear();

        // Try to parse as IP:port or just IP
        std::string fullAddress = config.ServerAddress + ":" + std::to_string(config.ServerPort);
        if (!serverAddr.ParseString(fullAddress.c_str())) {
            ENGINE_CORE_ERROR("Failed to parse server address: {}", fullAddress);
            return false;
        }

        ISteamNetworkingSockets* sockets = netManager.GetSockets();

        // Configure connection options
        SteamNetworkingConfigValue_t options[2];
        options[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                          reinterpret_cast<void*>(SteamNetConnectionStatusChangedCallback));
        options[1].SetInt32(k_ESteamNetworkingConfig_SendBufferSize, config.SendBufferSize);

        // Initiate connection
        m_Connection = sockets->ConnectByIPAddress(serverAddr, 2, options);
        if (m_Connection == k_HSteamNetConnection_Invalid) {
            ENGINE_CORE_ERROR("Failed to initiate connection to {}", fullAddress);
            return false;
        }

        m_State = ConnectionState::Connecting;
        m_HandshakeState = HandshakeState::NotStarted;
        m_HandshakeStartTime = std::chrono::steady_clock::now();
        m_LastHandshakePacketTime = m_HandshakeStartTime;

        ENGINE_CORE_INFO("Connecting to server at {}...", fullAddress);
        return true;
    }

    void NetworkClient::Disconnect(const char* reason)
    {
        PROFILE_FUNCTION();

        if (m_State == ConnectionState::Disconnected) {
            return;
        }

        ENGINE_CORE_INFO("Disconnecting from server: {}", reason ? reason : "No reason");

        m_State = ConnectionState::Disconnecting;

        if (m_Connection != k_HSteamNetConnection_Invalid) {
            ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
            sockets->CloseConnection(m_Connection, 0, reason, true);
        }

        ResetState();

        if (m_OnDisconnected) {
            m_OnDisconnected(reason ? reason : "Disconnected");
        }
    }

    void NetworkClient::Update()
    {
        PROFILE_FUNCTION();

        if (m_State == ConnectionState::Disconnected) {
            return;
        }

        // Poll for connection status changes
        NetworkManager::Get().GetSockets()->RunCallbacks();

        // Process incoming messages
        if (m_Connection != k_HSteamNetConnection_Invalid) {
            ProcessIncomingMessages();
        }

        // Check for handshake timeout
        if (m_State == ConnectionState::Connecting || 
            (m_State == ConnectionState::Connected && m_HandshakeState != HandshakeState::Completed)) {
            UpdateHandshakeTimeout();
        }
    }

    void NetworkClient::ProcessIncomingMessages()
    {
        PROFILE_FUNCTION();

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();

        constexpr int MAX_MESSAGES = 64;
        SteamNetworkingMessage_t* messages[MAX_MESSAGES];

        int numMessages = sockets->ReceiveMessagesOnConnection(m_Connection, messages, MAX_MESSAGES);

        for (int i = 0; i < numMessages; ++i) {
            SteamNetworkingMessage_t* msg = messages[i];

            if (msg->m_cbSize > 0) {
                // Check if this is a handshake packet
                if (m_HandshakeState != HandshakeState::Completed) {
                    ProcessHandshakePacket(msg->m_pData, static_cast<uint32_t>(msg->m_cbSize));
                }
                else {
                    // Regular game message - forward to callback
                    if (m_OnMessage) {
                        m_OnMessage(msg->m_pData, static_cast<uint32_t>(msg->m_cbSize));
                    }
                }
            }

            msg->Release();
        }
    }

    bool NetworkClient::Send(const void* data, uint32_t size, int sendFlags)
    {
        PROFILE_FUNCTION();

        if (!IsConnected()) {
            ENGINE_CORE_WARN("NetworkClient::Send called but not connected");
            return false;
        }

        if (m_Connection == k_HSteamNetConnection_Invalid) {
            return false;
        }

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        EResult result = sockets->SendMessageToConnection(
            m_Connection,
            data,
            size,
            sendFlags,
            nullptr
        );

        return result == k_EResultOK;
    }

    void NetworkClient::QueueMessage(const void* data, uint32_t size, int sendFlags)
    {
        QueuedMessage msg;
        msg.Data.resize(size);
        std::memcpy(msg.Data.data(), data, size);
        msg.SendFlags = sendFlags;
        m_MessageQueue.push(std::move(msg));
    }

    void NetworkClient::FlushMessageQueue()
    {
        PROFILE_FUNCTION();

        while (!m_MessageQueue.empty() && IsConnected()) {
            const auto& msg = m_MessageQueue.front();
            Send(msg.Data.data(), static_cast<uint32_t>(msg.Data.size()), msg.SendFlags);
            m_MessageQueue.pop();
        }
    }

    NetworkStats NetworkClient::GetStats() const
    {
        NetworkStats stats;

        if (m_Connection == k_HSteamNetConnection_Invalid) {
            return stats;
        }

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        SteamNetConnectionRealTimeStatus_t status;

        if (sockets->GetConnectionRealTimeStatus(m_Connection, &status, 0, nullptr)) {
            stats.PingMs = static_cast<float>(status.m_nPing);
            stats.PacketLossPercent = (1.0f - status.m_flConnectionQualityLocal) * 100.0f;
            stats.BytesSentPerSecond = status.m_flOutBytesPerSec;
            stats.BytesReceivedPerSecond = status.m_flInBytesPerSec;
        }

        return stats;
    }

    bool NetworkClient::Reconnect()
    {
        PROFILE_FUNCTION();

        if (m_State != ConnectionState::Disconnected) {
            Disconnect("Reconnecting");
        }

        if (m_ReconnectAttempts >= m_Config.MaxReconnectAttempts && m_Config.MaxReconnectAttempts > 0) {
            ENGINE_CORE_WARN("Max reconnect attempts ({}) reached", m_Config.MaxReconnectAttempts);
            return false;
        }

        m_ReconnectAttempts++;
        ENGINE_CORE_INFO("Reconnect attempt {} of {}", 
                         m_ReconnectAttempts, 
                         m_Config.MaxReconnectAttempts > 0 ? m_Config.MaxReconnectAttempts : 0);

        return Connect(m_Config, m_ClientName.c_str());
    }

    void NetworkClient::SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo)
    {
        if (s_Instance) {
            s_Instance->OnConnectionStatusChanged(pInfo);
        }
    }

    void NetworkClient::OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
    {
        PROFILE_FUNCTION();

        if (!pInfo) {
            return;
        }

        // Ignore callbacks for other connections
        if (pInfo->m_hConn != m_Connection) {
            return;
        }

        switch (pInfo->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_Connecting:
                ENGINE_CORE_TRACE("Connection state: Connecting...");
                m_State = ConnectionState::Connecting;
                break;

            case k_ESteamNetworkingConnectionState_FindingRoute:
                ENGINE_CORE_TRACE("Connection state: Finding route...");
                break;

            case k_ESteamNetworkingConnectionState_Connected:
                ENGINE_CORE_INFO("Connected to server, starting handshake...");
                m_State = ConnectionState::Connected;
                StartHandshake();
                break;

            case k_ESteamNetworkingConnectionState_ClosedByPeer:
                ENGINE_CORE_INFO("Connection closed by server: {}", pInfo->m_info.m_szEndDebug);
                m_State = ConnectionState::Disconnected;
                ResetState();
                if (m_OnDisconnected) {
                    m_OnDisconnected(pInfo->m_info.m_szEndDebug);
                }
                break;

            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
                ENGINE_CORE_ERROR("Connection problem: {}", pInfo->m_info.m_szEndDebug);
                m_State = ConnectionState::Failed;
                
                // Clean up connection
                NetworkManager::Get().GetSockets()->CloseConnection(m_Connection, 0, nullptr, false);
                ResetState();

                if (m_OnConnectionFailed) {
                    m_OnConnectionFailed(pInfo->m_info.m_szEndDebug);
                }
                break;

            default:
                break;
        }
    }

    void NetworkClient::StartHandshake()
    {
        PROFILE_FUNCTION();

        ENGINE_CORE_TRACE("Starting handshake sequence");
        m_HandshakeState = HandshakeState::SendingClientInfo;
        m_HandshakeStartTime = std::chrono::steady_clock::now();
        m_LastHandshakePacketTime = m_HandshakeStartTime;

        SendClientHello();
    }

    void NetworkClient::SendClientHello()
    {
        PROFILE_FUNCTION();

        ClientHelloPacket packet;
        packet.Type = HandshakePacketType::ClientHello;
        packet.ProtocolVersion = 1;

        // Copy client name (truncate if too long)
        SAFE_STRCPY(packet.ClientName, m_ClientName.c_str(), sizeof(packet.ClientName));

        std::memset(packet.Reserved, 0, sizeof(packet.Reserved));

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        EResult result = sockets->SendMessageToConnection(
            m_Connection,
            &packet,
            sizeof(packet),
            k_nSteamNetworkingSend_Reliable,
            nullptr
        );

        if (result == k_EResultOK) {
            ENGINE_CORE_TRACE("Sent ClientHello packet");
            m_HandshakeState = HandshakeState::WaitingForServerAck;
            m_LastHandshakePacketTime = std::chrono::steady_clock::now();
        }
        else {
            ENGINE_CORE_ERROR("Failed to send ClientHello packet");
            m_HandshakeState = HandshakeState::Failed;
        }
    }

    void NetworkClient::SendClientReady()
    {
        PROFILE_FUNCTION();

        ClientReadyPacket packet;
        packet.Type = HandshakePacketType::ClientReady;
        packet.AcknowledgedClientId = m_AssignedClientId;
        std::memset(packet.Reserved, 0, sizeof(packet.Reserved));

        ISteamNetworkingSockets* sockets = NetworkManager::Get().GetSockets();
        EResult result = sockets->SendMessageToConnection(
            m_Connection,
            &packet,
            sizeof(packet),
            k_nSteamNetworkingSend_Reliable,
            nullptr
        );

        if (result == k_EResultOK) {
            ENGINE_CORE_TRACE("Sent ClientReady packet");
            m_HandshakeState = HandshakeState::ReceivingWorldState;
            m_LastHandshakePacketTime = std::chrono::steady_clock::now();
        }
        else {
            ENGINE_CORE_ERROR("Failed to send ClientReady packet");
            m_HandshakeState = HandshakeState::Failed;
        }
    }

    void NetworkClient::ProcessHandshakePacket(const void* data, uint32_t size)
    {
        PROFILE_FUNCTION();

        if (size < 1) {
            ENGINE_CORE_WARN("Received empty handshake packet");
            return;
        }

        // First byte is packet type
        auto packetType = *static_cast<const HandshakePacketType*>(data);

        switch (packetType) {
            case HandshakePacketType::ServerWelcome:
                if (size >= sizeof(ServerWelcomePacket)) {
                    HandleServerWelcome(*static_cast<const ServerWelcomePacket*>(data));
                }
                else {
                    ENGINE_CORE_WARN("ServerWelcome packet too small: {} bytes", size);
                }
                break;

            case HandshakePacketType::ServerReady:
                if (size >= sizeof(ServerReadyPacket)) {
                    HandleServerReady(*static_cast<const ServerReadyPacket*>(data));
                }
                else {
                    ENGINE_CORE_WARN("ServerReady packet too small: {} bytes", size);
                }
                break;

            case HandshakePacketType::Rejected:
                if (size >= sizeof(RejectionPacket)) {
                    HandleRejection(*static_cast<const RejectionPacket*>(data));
                }
                else {
                    ENGINE_CORE_WARN("Rejection packet too small: {} bytes", size);
                }
                break;

            default:
                ENGINE_CORE_WARN("Unknown handshake packet type: {}", static_cast<int>(packetType));
                break;
        }
    }

    void NetworkClient::HandleServerWelcome(const ServerWelcomePacket& packet)
    {
        PROFILE_FUNCTION();

        if (m_HandshakeState != HandshakeState::WaitingForServerAck) {
            ENGINE_CORE_WARN("Received ServerWelcome in unexpected state");
            return;
        }

        m_AssignedClientId = packet.AssignedClientId;
        m_ServerTickRate = packet.ServerTickRate;
        m_ServerName = packet.ServerName;

        ENGINE_CORE_INFO("Received ServerWelcome: ClientID={}, Server='{}', TickRate={}Hz",
                         m_AssignedClientId, m_ServerName, m_ServerTickRate);

        if (packet.Message[0] != '\0') {
            ENGINE_CORE_INFO("Server message: {}", packet.Message);
        }

        // Send acknowledgment
        SendClientReady();
    }

    void NetworkClient::HandleServerReady(const ServerReadyPacket& packet)
    {
        PROFILE_FUNCTION();

        if (m_HandshakeState != HandshakeState::ReceivingWorldState) {
            ENGINE_CORE_WARN("Received ServerReady in unexpected state");
            return;
        }

        m_ServerTimestamp = packet.ServerTimestamp;
        m_HandshakeState = HandshakeState::Completed;

        ENGINE_CORE_INFO("Handshake completed! Connected as client {} to '{}'",
                         m_AssignedClientId, m_ServerName);

        // Flush any queued messages
        FlushMessageQueue();

        // Notify callback
        if (m_OnConnected) {
            m_OnConnected(m_AssignedClientId);
        }
    }

    void NetworkClient::HandleRejection(const RejectionPacket& packet)
    {
        PROFILE_FUNCTION();

        ENGINE_CORE_WARN("Connection rejected: {} (code: {})", 
                         packet.Reason, packet.ReasonCode);

        m_HandshakeState = HandshakeState::Failed;
        m_State = ConnectionState::Failed;

        if (m_OnConnectionFailed) {
            m_OnConnectionFailed(packet.Reason);
        }

        // Clean up
        if (m_Connection != k_HSteamNetConnection_Invalid) {
            NetworkManager::Get().GetSockets()->CloseConnection(m_Connection, 0, nullptr, false);
        }
        ResetState();
    }

    void NetworkClient::UpdateHandshakeTimeout()
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_HandshakeStartTime
        ).count();

        if (static_cast<uint32_t>(elapsed) >= m_Config.ConnectionTimeoutMs) {
            ENGINE_CORE_ERROR("Connection/handshake timed out after {}ms", elapsed);

            m_HandshakeState = HandshakeState::Failed;
            m_State = ConnectionState::Failed;

            if (m_OnConnectionFailed) {
                m_OnConnectionFailed("Connection timed out");
            }

            // Clean up
            if (m_Connection != k_HSteamNetConnection_Invalid) {
                NetworkManager::Get().GetSockets()->CloseConnection(m_Connection, 0, "Timeout", false);
            }
            ResetState();
        }
    }

    void NetworkClient::ResetState()
    {
        m_Connection = k_HSteamNetConnection_Invalid;
        m_State = ConnectionState::Disconnected;
        m_HandshakeState = HandshakeState::NotStarted;
        m_AssignedClientId = 0;
        m_ServerName.clear();
        m_ServerTickRate = 60;
        m_ServerTimestamp = 0;

        // Clear message queue
        while (!m_MessageQueue.empty()) {
            m_MessageQueue.pop();
        }
    }

} // namespace Network
} // namespace Core
