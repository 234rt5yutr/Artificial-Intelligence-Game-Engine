#include "Core/Network/NetworkServer.h"
#include "Core/Log.h"
#include "Core/Profile.h"

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
            if (clientId != 0 && m_OnMessage) {
                m_OnMessage(clientId, msg->m_pData, msg->m_cbSize);
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

} // namespace Network
} // namespace Core
