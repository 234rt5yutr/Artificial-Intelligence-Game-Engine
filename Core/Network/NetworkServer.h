#pragma once

#include "Core/Network/NetworkManager.h"
#include "Core/Network/NetworkPackets.h"
#include <unordered_map>
#include <vector>
#include <string>

namespace Core {
namespace Network {

    // Client handshake state (server-side tracking)
    enum class ClientHandshakeState : uint8_t {
        WaitingForHello = 0,    // Waiting for ClientHello
        WaitingForReady,        // Sent ServerWelcome, waiting for ClientReady
        Completed,              // Handshake complete
        Failed                  // Handshake failed
    };

    // Information about a connected client
    struct ClientInfo {
        HSteamNetConnection Connection = k_HSteamNetConnection_Invalid;
        ConnectionState State = ConnectionState::Disconnected;
        ClientHandshakeState HandshakeState = ClientHandshakeState::WaitingForHello;
        std::string Address;
        std::string ClientName;     // Client's display name
        uint64_t ConnectedTimestamp = 0;
        uint32_t ClientId = 0;      // Server-assigned unique ID
        uint32_t ProtocolVersion = 0;   // Client's protocol version
        NetworkStats Stats;
    };

    // Server configuration
    struct ServerConfig {
        uint16_t Port = 27015;              // Default game server port
        uint32_t MaxClients = 32;           // Maximum simultaneous connections
        std::string ServerName = "Game Server";
        std::string WelcomeMessage = "Welcome to the server!";
        uint32_t TickRate = 60;             // Server tick rate in Hz
        uint32_t HandshakeTimeoutMs = 10000; // Handshake timeout
        bool AllowP2P = false;              // Allow P2P connections
        int32_t SendBufferSize = 512 * 1024;  // 512KB send buffer
    };

    // Callback types for server events
    using ClientConnectedCallback = std::function<void(uint32_t clientId, const ClientInfo&)>;
    using ClientDisconnectedCallback = std::function<void(uint32_t clientId, const char* reason)>;
    using ServerMessageCallback = std::function<void(uint32_t clientId, const void* data, uint32_t size)>;

    class NetworkServer {
    public:
        NetworkServer();
        ~NetworkServer();

        // Prevent copying
        NetworkServer(const NetworkServer&) = delete;
        NetworkServer& operator=(const NetworkServer&) = delete;

        // Start the server listening on the specified port
        bool Start(const ServerConfig& config = ServerConfig());

        // Stop the server
        void Stop();

        // Check if server is running
        bool IsRunning() const { return m_Running; }

        // Get server configuration
        const ServerConfig& GetConfig() const { return m_Config; }

        // Poll for incoming messages and connection changes
        void Update();

        // Send message to a specific client
        bool SendToClient(uint32_t clientId, const void* data, uint32_t size, int sendFlags = k_nSteamNetworkingSend_Reliable);

        // Broadcast message to all connected clients
        void BroadcastToAll(const void* data, uint32_t size, int sendFlags = k_nSteamNetworkingSend_Reliable);

        // Broadcast message to all clients except one
        void BroadcastExcept(uint32_t excludeClientId, const void* data, uint32_t size, int sendFlags = k_nSteamNetworkingSend_Reliable);

        // Disconnect a specific client
        void DisconnectClient(uint32_t clientId, const char* reason = "Kicked by server");

        // Get number of connected clients
        uint32_t GetClientCount() const;

        // Get client info by ID
        const ClientInfo* GetClientInfo(uint32_t clientId) const;

        // Get all connected client IDs
        std::vector<uint32_t> GetConnectedClientIds() const;

        // Get connection handle for client ID
        HSteamNetConnection GetClientConnection(uint32_t clientId) const;

        // Set callbacks
        void SetClientConnectedCallback(ClientConnectedCallback callback) { m_OnClientConnected = std::move(callback); }
        void SetClientDisconnectedCallback(ClientDisconnectedCallback callback) { m_OnClientDisconnected = std::move(callback); }
        void SetMessageCallback(ServerMessageCallback callback) { m_OnMessage = std::move(callback); }

        // Update client network stats
        void UpdateClientStats(uint32_t clientId);

        // Reject a client with reason
        void RejectClient(uint32_t clientId, RejectionReason reason, const char* message = nullptr);

    private:
        // Handle connection status changes
        void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo);

        // Accept a pending connection
        bool AcceptConnection(HSteamNetConnection connection);

        // Process incoming messages from all clients
        void ProcessIncomingMessages();

        // Generate a unique client ID
        uint32_t GenerateClientId();

        // Find client ID by connection handle
        uint32_t FindClientIdByConnection(HSteamNetConnection connection) const;

        // Static callback thunk
        static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo);

        // Handshake packet handlers
        void ProcessHandshakePacket(uint32_t clientId, const void* data, uint32_t size);
        void HandleClientHello(uint32_t clientId, const ClientHelloPacket& packet);
        void HandleClientReady(uint32_t clientId, const ClientReadyPacket& packet);
        void SendServerWelcome(uint32_t clientId);
        void SendServerReady(uint32_t clientId);
        void SendRejection(uint32_t clientId, RejectionReason reason, const char* message);

        // Check handshake timeouts
        void UpdateHandshakeTimeouts();

        // SECURITY: Rate limiting helpers
        struct ConnectionAttempt {
            uint32_t Count = 0;
            uint64_t FirstAttemptTime = 0;
        };
        bool ShouldRateLimitConnection(uint32_t ipKey);
        void CleanupStaleConnectionAttempts();
        uint32_t ExtractIPKey(HSteamNetConnection connection) const;

    private:
        bool m_Running = false;
        ServerConfig m_Config;
        HSteamListenSocket m_ListenSocket = k_HSteamListenSocket_Invalid;
        HSteamNetPollGroup m_PollGroup = k_HSteamNetPollGroup_Invalid;

        // Client management
        std::unordered_map<uint32_t, ClientInfo> m_Clients;
        std::unordered_map<HSteamNetConnection, uint32_t> m_ConnectionToClientId;
        uint32_t m_NextClientId = 1;

        // SECURITY: Connection rate limiting
        std::unordered_map<uint32_t, ConnectionAttempt> m_ConnectionAttempts;
        uint64_t m_LastRateLimitCleanup = 0;

        // Callbacks
        ClientConnectedCallback m_OnClientConnected;
        ClientDisconnectedCallback m_OnClientDisconnected;
        ServerMessageCallback m_OnMessage;

        // Singleton for callback routing
        static NetworkServer* s_Instance;
    };

} // namespace Network
} // namespace Core
