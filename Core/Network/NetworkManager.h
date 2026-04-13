#pragma once

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <functional>
#include <string>
#include <cstdint>

namespace Core {
namespace Network {

    // Network role enumeration
    enum class NetworkRole : uint8_t {
        None = 0,       // Not initialized for networking
        Server,         // Hosting a game server
        Client,         // Connected to a server
        ListenServer    // Hosting and playing (combined server + client)
    };

    // Connection state
    enum class ConnectionState : uint8_t {
        Disconnected = 0,
        Connecting,
        Connected,
        Disconnecting,
        Failed
    };

    // Network statistics
    struct NetworkStats {
        float PingMs = 0.0f;
        float PacketLossPercent = 0.0f;
        float BytesSentPerSecond = 0.0f;
        float BytesReceivedPerSecond = 0.0f;
        uint64_t TotalBytesSent = 0;
        uint64_t TotalBytesReceived = 0;
        uint32_t PacketsSent = 0;
        uint32_t PacketsReceived = 0;
    };

    // Callback types
    using ConnectionStatusCallback = std::function<void(HSteamNetConnection, ConnectionState, const char*)>;
    using MessageReceivedCallback = std::function<void(HSteamNetConnection, const void*, uint32_t)>;

    // Debug output callback for GNS
    void NetworkDebugOutputCallback(ESteamNetworkingSocketsDebugOutputType eType, const char* pszMsg);

    class NetworkManager {
    public:
        NetworkManager();
        ~NetworkManager();

        // Prevent copying
        NetworkManager(const NetworkManager&) = delete;
        NetworkManager& operator=(const NetworkManager&) = delete;

        // Initialize the networking library (must be called before any other networking)
        bool Initialize();

        // Shutdown the networking library
        void Shutdown();

        // Check if initialized
        bool IsInitialized() const { return m_Initialized; }

        // Get the current network role
        NetworkRole GetRole() const { return m_Role; }

        // Get the ISteamNetworkingSockets interface
        ISteamNetworkingSockets* GetSockets() const { return m_Sockets; }

        // Get singleton instance
        static NetworkManager& Get();
        static bool HasInstance();

        // Set debug output level
        void SetDebugOutputLevel(ESteamNetworkingSocketsDebugOutputType level);

        // Poll for incoming messages and connection state changes
        // Should be called every frame
        void PollEvents();

        // Set callbacks
        void SetConnectionStatusCallback(ConnectionStatusCallback callback) { m_OnConnectionStatus = std::move(callback); }
        void SetMessageReceivedCallback(MessageReceivedCallback callback) { m_OnMessageReceived = std::move(callback); }

        // Get current timestamp (microseconds since init)
        SteamNetworkingMicroseconds GetCurrentTime() const;

    protected:
        // Called when connection status changes
        void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo);

        // Static callback thunk for GNS
        static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo);

    protected:
        bool m_Initialized = false;
        NetworkRole m_Role = NetworkRole::None;
        ISteamNetworkingSockets* m_Sockets = nullptr;

        // Callbacks
        ConnectionStatusCallback m_OnConnectionStatus;
        MessageReceivedCallback m_OnMessageReceived;

        // Singleton instance
        static NetworkManager* s_Instance;
    };

} // namespace Network
} // namespace Core
