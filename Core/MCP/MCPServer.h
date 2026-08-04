#pragma once

// MCP (Model Context Protocol) Server
// Implements the server-side of MCP for AI agent communication with the game engine
// 
// The MCP Server listens for JSON-RPC 2.0 requests over HTTP and routes them to
// registered tools that can inspect and modify the game world.

#include "HttpServer.h"
#include "MCPTypes.h"
#include "MCPTool.h"
#include "Core/Log.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <queue>
#include <functional>
#include <thread>
#include <condition_variable>
#include <unordered_set>
#include <vector>
#include <optional>

namespace Core {

namespace ECS {
    class Scene;
}

namespace MCP {

    // Configuration for the MCP server
    struct MCPServerConfig {
        std::string Host = "127.0.0.1";
        int Port = 3000;                    // Default MCP port
        size_t ThreadPoolSize = 4;
        bool EnableCORS = true;
        bool EnableLogging = true;
        LogLevel MinLogLevel = LogLevel::Info;
        std::string ServerName = "AIGameEngine-MCP";
        std::string ServerVersion = "1.0.0";
        bool EnforceCapabilityScopes = false;
        std::vector<std::string> GrantedCapabilities;
        // How long an HTTP worker waits for the main thread to run a tool before
        // giving up. Guards against hanging every client if the game loop stalls.
        int MainThreadDispatchTimeoutMs = 10000;

        // --- Security -------------------------------------------------------
        // When set, every request must carry `Authorization: Bearer <token>`.
        std::string AuthToken;
        // DNS-rebinding protection (required by the MCP spec for local HTTP
        // servers): a browser on any page can reach 127.0.0.1, so requests that
        // carry a browser Origin are rejected unless the origin is listed here.
        // Non-browser clients send no Origin and are unaffected.
        std::vector<std::string> AllowedOrigins;
        // Reject oversized bodies before parsing them.
        size_t MaxRequestBodyBytes = 8u * 1024u * 1024u;
    };

    // Connection state for an MCP session
    struct MCPSession {
        bool Initialized = false;
        ClientInfo Client;
        LogLevel CurrentLogLevel = LogLevel::Info;
        std::unordered_set<std::string> GrantedCapabilities;
    };

    // MCP Server implementation
    class MCPServer {
    public:
        MCPServer();
        explicit MCPServer(const MCPServerConfig& config);
        ~MCPServer();

        // Delete copy
        MCPServer(const MCPServer&) = delete;
        MCPServer& operator=(const MCPServer&) = delete;

        // Configuration
        void SetConfig(const MCPServerConfig& config);
        const MCPServerConfig& GetConfig() const { return m_Config; }

        // Scene binding (set the active scene for tools to operate on)
        void SetActiveScene(ECS::Scene* scene);
        ECS::Scene* GetActiveScene() const { return m_ActiveScene; }

        // Tool management
        void RegisterTool(MCPToolPtr tool);
        void UnregisterTool(const std::string& name);
        bool HasTool(const std::string& name) const;
        MCPToolPtr GetTool(const std::string& name) const;
        std::vector<ToolDefinition> GetToolDefinitions() const;

        // Server lifecycle
        bool Start();
        void Stop();
        bool IsRunning() const { return m_Running; }

        // Server information
        std::string GetHost() const { return m_Config.Host; }
        int GetPort() const { return m_Config.Port; }
        std::string GetEndpointUrl() const;

        // Logging (sends log notifications to connected clients)
        void Log(LogLevel level, const std::string& logger, const std::string& message);
        void LogDebug(const std::string& message) { Log(LogLevel::Debug, "engine", message); }
        void LogInfo(const std::string& message) { Log(LogLevel::Info, "engine", message); }
        void LogWarning(const std::string& message) { Log(LogLevel::Warning, "engine", message); }
        void LogError(const std::string& message) { Log(LogLevel::Error, "engine", message); }

        // Statistics
        size_t GetRequestCount() const { return m_RequestCount; }
        size_t GetToolCallCount() const { return m_ToolCallCount; }

        // Thread-safe request queue for main thread processing
        // This allows tool calls that need main thread access to be queued
        struct PendingRequest {
            JsonRpcRequest Request;
            std::function<void(JsonRpcResponse)> Callback;
        };

        // Queue a request to be processed on the main thread
        void QueueMainThreadRequest(PendingRequest request);

        // Process pending main thread requests (call from game loop)
        void ProcessPendingRequests();

        // Check if there are pending requests
        bool HasPendingRequests() const;

    private:
        // HTTP handlers
        HttpResponse HandleMCPRequest(const HttpRequest& request);
        HttpResponse HandleHealthCheck(const HttpRequest& request);
        HttpResponse HandleListTools(const HttpRequest& request);

        // JSON-RPC method handlers
        JsonRpcResponse HandleJsonRpcRequest(const JsonRpcRequest& request);

        // Runs a request on the main thread and blocks the caller until it completes.
        // Tool execution mutates the ECS registry, so it must not run concurrently
        // with the game loop.
        JsonRpcResponse DispatchOnMainThread(const JsonRpcRequest& request);
        JsonRpcResponse HandleInitialize(const JsonRpcRequest& request);
        JsonRpcResponse HandlePing(const JsonRpcRequest& request);
        JsonRpcResponse HandleToolsList(const JsonRpcRequest& request);
        JsonRpcResponse HandleToolsCall(const JsonRpcRequest& request);
        JsonRpcResponse HandleSetLogLevel(const JsonRpcRequest& request);
        JsonRpcResponse HandleResourcesList(const JsonRpcRequest& request);
        JsonRpcResponse HandleResourcesRead(const JsonRpcRequest& request);
        JsonRpcResponse HandlePromptsList(const JsonRpcRequest& request);
        bool HasGrantedCapability(const std::string& capability) const;
        void RebuildConfigCapabilities();

        // Returns an error response to send verbatim, or nullopt when the request
        // passes the transport-level checks (auth, origin, body size).
        std::optional<HttpResponse> RejectRequest(const HttpRequest& request) const;

        // Setup HTTP routes
        void SetupRoutes();

        // Send notification to clients (placeholder for SSE/WebSocket)
        void SendNotification(const std::string& method, const Json& params);

    private:
        MCPServerConfig m_Config;
        std::unique_ptr<HttpServer> m_HttpServer;
        std::atomic<bool> m_Running{ false };

        // Active scene for tool operations
        ECS::Scene* m_ActiveScene = nullptr;

        // Registered tools
        mutable std::mutex m_ToolsMutex;
        std::unordered_map<std::string, MCPToolPtr> m_Tools;

        // Session state
        mutable std::mutex m_SessionMutex;
        MCPSession m_Session;
        std::unordered_set<std::string> m_ConfigCapabilities;

        // Main thread request queue
        mutable std::mutex m_RequestQueueMutex;
        std::queue<PendingRequest> m_PendingRequests;
        std::condition_variable m_RequestQueueCV;
        // Set once the game loop has pumped at least one time; until then requests
        // are executed inline so a host that never calls ProcessPendingRequests()
        // still works instead of timing out on every call.
        std::atomic<bool> m_MainThreadPumpSeen{ false };
        std::atomic<bool> m_AcceptingQueuedRequests{ false };

        // Statistics
        std::atomic<size_t> m_RequestCount{ 0 };
        std::atomic<size_t> m_ToolCallCount{ 0 };

        // Server info
        ServerInfo m_ServerInfo;
        ServerCapabilities m_Capabilities;
    };

} // namespace MCP
} // namespace Core
