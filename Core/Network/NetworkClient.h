#pragma once

#include "Core/Network/NetworkManager.h"
#include "Core/Network/NetworkPackets.h"
#include <string>
#include <chrono>
#include <queue>

namespace Core {
namespace Network {

    // Client configuration
    struct ClientConfig {
        std::string ServerAddress = "127.0.0.1";  // Server IP or hostname
        uint16_t ServerPort = 27015;              // Server port
        uint32_t ConnectionTimeoutMs = 10000;     // Timeout for initial connection
        uint32_t ReconnectDelayMs = 3000;         // Delay between reconnect attempts
        uint32_t MaxReconnectAttempts = 5;        // Maximum auto-reconnect attempts (0 = disabled)
        int32_t SendBufferSize = 256 * 1024;      // 256KB send buffer
    };

    // Handshake state machine
    enum class HandshakeState : uint8_t {
        NotStarted = 0,
        SendingClientInfo,      // Client sends identification data
        WaitingForServerAck,    // Waiting for server acknowledgment
        ReceivingWorldState,    // Receiving initial world state (optional)
        Completed,              // Handshake completed successfully
        Failed                  // Handshake failed
    };

    // Callback types for client-side events (prefixed to avoid conflict with server callbacks)
    using OnClientConnectedCallback = std::function<void(uint32_t assignedClientId)>;
    using OnClientDisconnectedCallback = std::function<void(const char* reason)>;
    using OnClientMessageCallback = std::function<void(const void* data, uint32_t size)>;
    using OnClientConnectionFailedCallback = std::function<void(const char* reason)>;

    class NetworkClient {
    public:
        NetworkClient();
        ~NetworkClient();

        // Prevent copying
        NetworkClient(const NetworkClient&) = delete;
        NetworkClient& operator=(const NetworkClient&) = delete;

        // Connect to a server
        bool Connect(const ClientConfig& config, const char* clientName = "Player");

        // Disconnect from the server
        void Disconnect(const char* reason = "Client disconnected");

        // Check connection state
        bool IsConnected() const { return m_State == ConnectionState::Connected && m_HandshakeState == HandshakeState::Completed; }
        bool IsConnecting() const { return m_State == ConnectionState::Connecting; }
        ConnectionState GetConnectionState() const { return m_State; }
        HandshakeState GetHandshakeState() const { return m_HandshakeState; }

        // Get assigned client ID (valid after handshake complete)
        uint32_t GetClientId() const { return m_AssignedClientId; }

        // Get current configuration
        const ClientConfig& GetConfig() const { return m_Config; }

        // Get server info (valid after handshake complete)
        const std::string& GetServerName() const { return m_ServerName; }
        uint32_t GetServerTickRate() const { return m_ServerTickRate; }
        bool IsContractCompatibilityDowngradeActive() const { return m_ContractCompatibilityDowngrade; }

        // Poll for incoming messages and connection changes
        void Update();

        // Send message to server (only valid when connected)
        bool Send(const void* data, uint32_t size, int sendFlags = k_nSteamNetworkingSend_Reliable);

        // Queue a message to be sent (useful during handshake)
        void QueueMessage(const void* data, uint32_t size, int sendFlags = k_nSteamNetworkingSend_Reliable);

        // Flush queued messages
        void FlushMessageQueue();

        // Get network statistics
        NetworkStats GetStats() const;

        // Set callbacks
        void SetConnectedCallback(OnClientConnectedCallback callback) { m_OnConnected = std::move(callback); }
        void SetDisconnectedCallback(OnClientDisconnectedCallback callback) { m_OnDisconnected = std::move(callback); }
        void SetMessageCallback(OnClientMessageCallback callback) { m_OnMessage = std::move(callback); }
        void SetConnectionFailedCallback(OnClientConnectionFailedCallback callback) { m_OnConnectionFailed = std::move(callback); }

        // Manual reconnect (call after disconnect if you want to reconnect)
        bool Reconnect();

    private:
        // Handle connection status changes from GNS
        void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo);

        // Process incoming messages
        void ProcessIncomingMessages();

        // Handshake state machine handlers
        void StartHandshake();
        void ProcessHandshakePacket(const void* data, uint32_t size);
        void HandleServerWelcome(const ServerWelcomePacket& packet);
        void HandleServerReady(const ServerReadyPacket& packet);
        void HandleRejection(const RejectionPacket& packet);
        void SendClientHello();
        void SendClientReady();

        // Update handshake timeout
        void UpdateHandshakeTimeout();

        // Reset client state
        void ResetState();

        // Static callback thunk
        static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo);

    private:
        ConnectionState m_State = ConnectionState::Disconnected;
        HandshakeState m_HandshakeState = HandshakeState::NotStarted;
        ClientConfig m_Config;
        HSteamNetConnection m_Connection = k_HSteamNetConnection_Invalid;

        // Client identity
        std::string m_ClientName;
        uint32_t m_AssignedClientId = 0;

        // Server info (received during handshake)
        std::string m_ServerName;
        uint32_t m_ServerTickRate = 60;
        uint64_t m_ServerTimestamp = 0;
        bool m_ContractCompatibilityDowngrade = false;

        // Handshake timing
        std::chrono::steady_clock::time_point m_HandshakeStartTime;
        std::chrono::steady_clock::time_point m_LastHandshakePacketTime;

        // Reconnection
        uint32_t m_ReconnectAttempts = 0;
        bool m_WantsReconnect = false;

        // Message queue for messages sent before handshake completes
        struct QueuedMessage {
            std::vector<uint8_t> Data;
            int SendFlags;
        };
        std::queue<QueuedMessage> m_MessageQueue;

        // Callbacks
        OnClientConnectedCallback m_OnConnected;
        OnClientDisconnectedCallback m_OnDisconnected;
        OnClientMessageCallback m_OnMessage;
        OnClientConnectionFailedCallback m_OnConnectionFailed;

        // Singleton for callback routing (only one client per process typically)
        static NetworkClient* s_Instance;
    };

} // namespace Network
} // namespace Core
