#include "Core/Network/NetworkManager.h"
#include "Core/Log.h"
#include "Core/Profile.h"

namespace Core {
namespace Network {

    // Static member initialization
    NetworkManager* NetworkManager::s_Instance = nullptr;

    // Debug output callback implementation
    void NetworkDebugOutputCallback(ESteamNetworkingSocketsDebugOutputType eType, const char* pszMsg)
    {
        switch (eType) {
            case k_ESteamNetworkingSocketsDebugOutputType_Error:
                ENGINE_CORE_ERROR("[GNS] {}", pszMsg);
                break;
            case k_ESteamNetworkingSocketsDebugOutputType_Warning:
                ENGINE_CORE_WARN("[GNS] {}", pszMsg);
                break;
            case k_ESteamNetworkingSocketsDebugOutputType_Msg:
            case k_ESteamNetworkingSocketsDebugOutputType_Important:
                ENGINE_CORE_INFO("[GNS] {}", pszMsg);
                break;
            case k_ESteamNetworkingSocketsDebugOutputType_Verbose:
            case k_ESteamNetworkingSocketsDebugOutputType_Debug:
            case k_ESteamNetworkingSocketsDebugOutputType_Everything:
            default:
                ENGINE_CORE_TRACE("[GNS] {}", pszMsg);
                break;
        }
    }

    NetworkManager::NetworkManager()
    {
        if (s_Instance != nullptr) {
            ENGINE_CORE_ERROR("NetworkManager already exists! Only one instance allowed.");
            return;
        }
        s_Instance = this;
    }

    NetworkManager::~NetworkManager()
    {
        if (m_Initialized) {
            Shutdown();
        }
        s_Instance = nullptr;
    }

    NetworkManager& NetworkManager::Get()
    {
        return *s_Instance;
    }

    bool NetworkManager::HasInstance()
    {
        return s_Instance != nullptr;
    }

    bool NetworkManager::Initialize()
    {
        PROFILE_FUNCTION();

        if (m_Initialized) {
            ENGINE_CORE_WARN("NetworkManager already initialized");
            return true;
        }

        ENGINE_CORE_INFO("Initializing GameNetworkingSockets...");

        // Set debug output callback before initialization
        SteamNetworkingUtils()->SetDebugOutputFunction(
            k_ESteamNetworkingSocketsDebugOutputType_Msg,
            NetworkDebugOutputCallback
        );

        // Initialize the library
        SteamNetworkingErrMsg errMsg;
        if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
            ENGINE_CORE_ERROR("GameNetworkingSockets_Init failed: {}", errMsg);
            return false;
        }

        // Get the sockets interface
        m_Sockets = SteamNetworkingSockets();
        if (!m_Sockets) {
            ENGINE_CORE_ERROR("Failed to get ISteamNetworkingSockets interface");
            GameNetworkingSockets_Kill();
            return false;
        }

        m_Initialized = true;
        ENGINE_CORE_INFO("NetworkManager initialized successfully");
        return true;
    }

    void NetworkManager::Shutdown()
    {
        PROFILE_FUNCTION();

        if (!m_Initialized) {
            return;
        }

        ENGINE_CORE_INFO("Shutting down NetworkManager...");

        m_Sockets = nullptr;
        m_Role = NetworkRole::None;

        // Shutdown the library
        GameNetworkingSockets_Kill();

        m_Initialized = false;
        ENGINE_CORE_INFO("NetworkManager shutdown complete");
    }

    void NetworkManager::SetDebugOutputLevel(ESteamNetworkingSocketsDebugOutputType level)
    {
        if (m_Initialized) {
            SteamNetworkingUtils()->SetDebugOutputFunction(level, NetworkDebugOutputCallback);
        }
    }

    void NetworkManager::PollEvents()
    {
        PROFILE_FUNCTION();

        if (!m_Initialized || !m_Sockets) {
            return;
        }

        // Poll connection state changes
        m_Sockets->RunCallbacks();
    }

    SteamNetworkingMicroseconds NetworkManager::GetCurrentTime() const
    {
        if (!m_Initialized) {
            return 0;
        }
        return SteamNetworkingUtils()->GetLocalTimestamp();
    }

    void NetworkManager::SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo)
    {
        if (s_Instance) {
            s_Instance->OnConnectionStatusChanged(pInfo);
        }
    }

    void NetworkManager::OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
    {
        PROFILE_FUNCTION();

        if (!pInfo) {
            return;
        }

        ConnectionState state = ConnectionState::Disconnected;
        const char* stateStr = "Unknown";

        switch (pInfo->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_None:
                state = ConnectionState::Disconnected;
                stateStr = "None";
                break;

            case k_ESteamNetworkingConnectionState_Connecting:
                state = ConnectionState::Connecting;
                stateStr = "Connecting";
                break;

            case k_ESteamNetworkingConnectionState_FindingRoute:
                state = ConnectionState::Connecting;
                stateStr = "FindingRoute";
                break;

            case k_ESteamNetworkingConnectionState_Connected:
                state = ConnectionState::Connected;
                stateStr = "Connected";
                break;

            case k_ESteamNetworkingConnectionState_ClosedByPeer:
                state = ConnectionState::Disconnected;
                stateStr = "ClosedByPeer";
                break;

            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
                state = ConnectionState::Failed;
                stateStr = "ProblemDetectedLocally";
                break;

            default:
                break;
        }

        ENGINE_CORE_INFO("Connection {} state changed: {} ({})",
            pInfo->m_hConn, stateStr, pInfo->m_info.m_szEndDebug);

        // Invoke user callback
        if (m_OnConnectionStatus) {
            m_OnConnectionStatus(pInfo->m_hConn, state, pInfo->m_info.m_szEndDebug);
        }
    }

} // namespace Network
} // namespace Core
