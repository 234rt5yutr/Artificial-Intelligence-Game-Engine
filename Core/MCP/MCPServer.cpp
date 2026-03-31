#include "MCPServer.h"
#include "Core/Profile.h"
#include <sstream>

namespace Core {
namespace MCP {

    MCPServer::MCPServer()
        : MCPServer(MCPServerConfig{}) {
    }

    MCPServer::MCPServer(const MCPServerConfig& config)
        : m_Config(config) {
        m_HttpServer = std::make_unique<HttpServer>();
        m_ServerInfo.Name = config.ServerName;
        m_ServerInfo.Version = config.ServerVersion;
        
        ENGINE_CORE_INFO("MCP Server created: {}:{}", config.Host, config.Port);
    }

    MCPServer::~MCPServer() {
        Stop();
    }

    void MCPServer::SetConfig(const MCPServerConfig& config) {
        if (m_Running) {
            ENGINE_CORE_WARN("Cannot change MCP server config while running");
            return;
        }
        m_Config = config;
        m_ServerInfo.Name = config.ServerName;
        m_ServerInfo.Version = config.ServerVersion;
    }

    void MCPServer::SetActiveScene(ECS::Scene* scene) {
        m_ActiveScene = scene;
        if (scene) {
            ENGINE_CORE_INFO("MCP Server: Active scene set");
        } else {
            ENGINE_CORE_INFO("MCP Server: Active scene cleared");
        }
    }

    void MCPServer::RegisterTool(MCPToolPtr tool) {
        if (!tool) return;
        
        std::lock_guard lock(m_ToolsMutex);
        const auto& name = tool->GetName();
        
        if (m_Tools.contains(name)) {
            ENGINE_CORE_WARN("MCP: Replacing existing tool '{}'", name);
        }
        
        m_Tools[name] = std::move(tool);
        ENGINE_CORE_INFO("MCP: Registered tool '{}'", name);

        // Notify clients of tool list change
        if (m_Running) {
            SendNotification(NotificationType::ToolsChanged, Json::object());
        }
    }

    void MCPServer::UnregisterTool(const std::string& name) {
        std::lock_guard lock(m_ToolsMutex);
        
        if (m_Tools.erase(name) > 0) {
            ENGINE_CORE_INFO("MCP: Unregistered tool '{}'", name);
            
            if (m_Running) {
                SendNotification(NotificationType::ToolsChanged, Json::object());
            }
        }
    }

    bool MCPServer::HasTool(const std::string& name) const {
        std::lock_guard lock(m_ToolsMutex);
        return m_Tools.contains(name);
    }

    MCPToolPtr MCPServer::GetTool(const std::string& name) const {
        std::lock_guard lock(m_ToolsMutex);
        auto it = m_Tools.find(name);
        return (it != m_Tools.end()) ? it->second : nullptr;
    }

    std::vector<ToolDefinition> MCPServer::GetToolDefinitions() const {
        std::lock_guard lock(m_ToolsMutex);
        std::vector<ToolDefinition> definitions;
        definitions.reserve(m_Tools.size());
        
        for (const auto& [name, tool] : m_Tools) {
            if (tool->IsAvailable()) {
                definitions.push_back(tool->GetDefinition());
            }
        }
        
        return definitions;
    }

    bool MCPServer::Start() {
        if (m_Running) {
            ENGINE_CORE_WARN("MCP Server already running");
            return false;
        }

        // Configure HTTP server
        m_HttpServer->SetHost(m_Config.Host);
        m_HttpServer->SetPort(m_Config.Port);
        m_HttpServer->SetThreadPoolSize(m_Config.ThreadPoolSize);

        // Setup routes
        SetupRoutes();

        // Start HTTP server
        if (!m_HttpServer->Start()) {
            ENGINE_CORE_ERROR("Failed to start MCP HTTP server");
            return false;
        }

        m_Running = true;
        ENGINE_CORE_INFO("MCP Server started at {}", GetEndpointUrl());
        
        return true;
    }

    void MCPServer::Stop() {
        if (!m_Running) return;

        m_Running = false;
        m_HttpServer->Stop();

        // Clear session
        {
            std::lock_guard lock(m_SessionMutex);
            m_Session = MCPSession{};
        }

        ENGINE_CORE_INFO("MCP Server stopped");
    }

    std::string MCPServer::GetEndpointUrl() const {
        std::ostringstream oss;
        oss << "http://" << m_Config.Host << ":" << m_Config.Port;
        return oss.str();
    }

    void MCPServer::Log(LogLevel level, const std::string& logger, const std::string& message) {
        // Check if we should log based on current level
        {
            std::lock_guard lock(m_SessionMutex);
            if (static_cast<int>(level) < static_cast<int>(m_Session.CurrentLogLevel)) {
                return;
            }
        }

        // Create log notification
        Json params = {
            {"level", LogLevelToString(level)},
            {"logger", logger},
            {"data", message}
        };

        SendNotification(NotificationType::LogMessage, params);
    }

    void MCPServer::QueueMainThreadRequest(PendingRequest request) {
        std::lock_guard lock(m_RequestQueueMutex);
        m_PendingRequests.push(std::move(request));
        m_RequestQueueCV.notify_one();
    }

    void MCPServer::ProcessPendingRequests() {
        PROFILE_FUNCTION();

        std::queue<PendingRequest> toProcess;
        
        {
            std::lock_guard lock(m_RequestQueueMutex);
            std::swap(toProcess, m_PendingRequests);
        }

        while (!toProcess.empty()) {
            auto& req = toProcess.front();
            auto response = HandleJsonRpcRequest(req.Request);
            if (req.Callback) {
                req.Callback(response);
            }
            toProcess.pop();
        }
    }

    bool MCPServer::HasPendingRequests() const {
        std::lock_guard lock(m_RequestQueueMutex);
        return !m_PendingRequests.empty();
    }

    void MCPServer::SetupRoutes() {
        // Main MCP endpoint (JSON-RPC over HTTP POST)
        m_HttpServer->Post("/mcp", [this](const HttpRequest& req) {
            return HandleMCPRequest(req);
        });

        // Also support root endpoint for compatibility
        m_HttpServer->Post("/", [this](const HttpRequest& req) {
            return HandleMCPRequest(req);
        });

        // Health check endpoint
        m_HttpServer->Get("/health", [this](const HttpRequest& req) {
            return HandleHealthCheck(req);
        });

        // Tools list endpoint (convenience, not standard MCP)
        m_HttpServer->Get("/tools", [this](const HttpRequest& req) {
            return HandleListTools(req);
        });

        // CORS preflight handler
        if (m_Config.EnableCORS) {
            m_HttpServer->Options("/mcp", [](const HttpRequest&) {
                HttpResponse resp;
                resp.StatusCode = 204;
                resp.Headers["Access-Control-Allow-Origin"] = "*";
                resp.Headers["Access-Control-Allow-Methods"] = "POST, GET, OPTIONS";
                resp.Headers["Access-Control-Allow-Headers"] = "Content-Type";
                return resp;
            });
            
            m_HttpServer->Options("/", [](const HttpRequest&) {
                HttpResponse resp;
                resp.StatusCode = 204;
                resp.Headers["Access-Control-Allow-Origin"] = "*";
                resp.Headers["Access-Control-Allow-Methods"] = "POST, GET, OPTIONS";
                resp.Headers["Access-Control-Allow-Headers"] = "Content-Type";
                return resp;
            });
        }
    }

    HttpResponse MCPServer::HandleMCPRequest(const HttpRequest& request) {
        PROFILE_FUNCTION();
        ++m_RequestCount;

        HttpResponse response;
        response.ContentType = "application/json";

        // Parse JSON-RPC request
        auto jsonOpt = JsonUtils::Parse(request.Body);
        if (!jsonOpt) {
            auto errorResp = JsonRpcResponse::Error(
                std::variant<int64_t, std::string>(std::string("")), 
                JsonRpcError::ParseError,
                "Invalid JSON"
            );
            response.Body = errorResp.ToJson().dump();
            response.StatusCode = 400;
            return response;
        }

        auto rpcRequestOpt = JsonRpcRequest::FromJson(*jsonOpt);
        if (!rpcRequestOpt) {
            auto errorResp = JsonRpcResponse::Error(
                std::variant<int64_t, std::string>(std::string("")),
                JsonRpcError::InvalidRequest,
                "Invalid JSON-RPC request"
            );
            response.Body = errorResp.ToJson().dump();
            response.StatusCode = 400;
            return response;
        }

        // Handle the request
        auto rpcResponse = HandleJsonRpcRequest(*rpcRequestOpt);
        response.Body = rpcResponse.ToJson().dump();
        
        return response;
    }

    HttpResponse MCPServer::HandleHealthCheck(const HttpRequest&) {
        HttpResponse response;
        response.ContentType = "application/json";
        
        Json health = {
            {"status", "ok"},
            {"server", m_ServerInfo.ToJson()},
            {"initialized", m_Session.Initialized},
            {"toolCount", m_Tools.size()},
            {"requestCount", m_RequestCount.load()},
            {"toolCallCount", m_ToolCallCount.load()}
        };
        
        response.Body = health.dump();
        return response;
    }

    HttpResponse MCPServer::HandleListTools(const HttpRequest&) {
        HttpResponse response;
        response.ContentType = "application/json";
        
        Json tools = Json::array();
        for (const auto& def : GetToolDefinitions()) {
            tools.push_back(def.ToJson());
        }
        
        response.Body = tools.dump(2);
        return response;
    }

    JsonRpcResponse MCPServer::HandleJsonRpcRequest(const JsonRpcRequest& request) {
        const auto& method = request.method;

        ENGINE_CORE_TRACE("MCP: Handling method '{}'", method);

        // Route to appropriate handler
        if (method == MCPMethod::Initialize) {
            return HandleInitialize(request);
        } 
        else if (method == MCPMethod::Ping) {
            return HandlePing(request);
        }
        else if (method == MCPMethod::ListTools) {
            return HandleToolsList(request);
        }
        else if (method == MCPMethod::CallTool) {
            return HandleToolsCall(request);
        }
        else if (method == MCPMethod::SetLogLevel) {
            return HandleSetLogLevel(request);
        }
        else {
            // Unknown method
            return JsonRpcResponse::Error(
                request.id,
                JsonRpcError::MethodNotFound,
                "Method not found: " + method
            );
        }
    }

    JsonRpcResponse MCPServer::HandleInitialize(const JsonRpcRequest& request) {
        // Parse client info
        ClientInfo clientInfo;
        if (request.params.contains("clientInfo")) {
            auto infoOpt = ClientInfo::FromJson(request.params["clientInfo"]);
            if (infoOpt) {
                clientInfo = *infoOpt;
            }
        }

        // Update session state
        {
            std::lock_guard lock(m_SessionMutex);
            m_Session.Initialized = true;
            m_Session.Client = clientInfo;
        }

        ENGINE_CORE_INFO("MCP: Client initialized - {} v{}", 
                         clientInfo.Name, clientInfo.Version);

        // Build response
        Json result = {
            {"protocolVersion", MCP_PROTOCOL_VERSION},
            {"capabilities", m_Capabilities.ToJson()},
            {"serverInfo", m_ServerInfo.ToJson()}
        };

        return JsonRpcResponse::Success(request.id, result);
    }

    JsonRpcResponse MCPServer::HandlePing(const JsonRpcRequest& request) {
        return JsonRpcResponse::Success(request.id, Json::object());
    }

    JsonRpcResponse MCPServer::HandleToolsList(const JsonRpcRequest& request) {
        Json tools = Json::array();
        for (const auto& def : GetToolDefinitions()) {
            tools.push_back(def.ToJson());
        }

        Json result = {
            {"tools", tools}
        };

        return JsonRpcResponse::Success(request.id, result);
    }

    JsonRpcResponse MCPServer::HandleToolsCall(const JsonRpcRequest& request) {
        PROFILE_FUNCTION();
        ++m_ToolCallCount;

        // Parse tool call request
        auto callOpt = ToolCallRequest::FromJson(request.params);
        if (!callOpt) {
            return JsonRpcResponse::Error(
                request.id,
                JsonRpcError::InvalidParams,
                "Invalid tool call parameters - 'name' is required"
            );
        }

        const auto& toolName = callOpt->Name;
        const auto& arguments = callOpt->Arguments;

        ENGINE_CORE_INFO("MCP: Calling tool '{}' with args: {}", 
                         toolName, arguments.dump());

        // Find the tool
        auto tool = GetTool(toolName);
        if (!tool) {
            return JsonRpcResponse::Error(
                request.id,
                JsonRpcError::InvalidParams,
                "Unknown tool: " + toolName
            );
        }

        // Check if tool is available
        if (!tool->IsAvailable()) {
            return JsonRpcResponse::Error(
                request.id,
                JsonRpcError::InternalError,
                "Tool is not available: " + toolName
            );
        }

        // Validate arguments
        std::string validationError;
        if (!tool->ValidateArguments(arguments, validationError)) {
            return JsonRpcResponse::Error(
                request.id,
                JsonRpcError::InvalidParams,
                "Invalid arguments: " + validationError
            );
        }

        // Check scene requirement
        if (tool->RequiresScene() && !m_ActiveScene) {
            return JsonRpcResponse::Error(
                request.id,
                JsonRpcError::InternalError,
                "Tool requires active scene but none is set"
            );
        }

        // Execute the tool
        try {
            ToolResult result = tool->Execute(arguments, m_ActiveScene);
            return JsonRpcResponse::Success(request.id, result.ToJson());
        }
        catch (const std::exception& e) {
            ENGINE_CORE_ERROR("MCP: Tool '{}' threw exception: {}", toolName, e.what());
            return JsonRpcResponse::Error(
                request.id,
                JsonRpcError::InternalError,
                std::string("Tool execution failed: ") + e.what()
            );
        }
    }

    JsonRpcResponse MCPServer::HandleSetLogLevel(const JsonRpcRequest& request) {
        if (!request.params.contains("level")) {
            return JsonRpcResponse::Error(
                request.id,
                JsonRpcError::InvalidParams,
                "Missing 'level' parameter"
            );
        }

        std::string levelStr = request.params["level"].get<std::string>();
        LogLevel level = LogLevel::Info;

        if (levelStr == "debug") level = LogLevel::Debug;
        else if (levelStr == "info") level = LogLevel::Info;
        else if (levelStr == "notice") level = LogLevel::Notice;
        else if (levelStr == "warning") level = LogLevel::Warning;
        else if (levelStr == "error") level = LogLevel::Error;
        else if (levelStr == "critical") level = LogLevel::Critical;
        else if (levelStr == "alert") level = LogLevel::Alert;
        else if (levelStr == "emergency") level = LogLevel::Emergency;
        else {
            return JsonRpcResponse::Error(
                request.id,
                JsonRpcError::InvalidParams,
                "Invalid log level: " + levelStr
            );
        }

        {
            std::lock_guard lock(m_SessionMutex);
            m_Session.CurrentLogLevel = level;
        }

        ENGINE_CORE_INFO("MCP: Log level set to '{}'", levelStr);
        return JsonRpcResponse::Success(request.id, Json::object());
    }

    void MCPServer::SendNotification(const std::string& method, const Json& params) {
        // For HTTP-based MCP, notifications would typically be sent via:
        // 1. Server-Sent Events (SSE)
        // 2. WebSocket connection
        // 3. Long-polling
        //
        // For now, we log notifications for debugging
        // A full implementation would maintain a list of connected clients
        // and broadcast notifications to them
        
        if (m_Config.EnableLogging) {
            ENGINE_CORE_TRACE("MCP Notification: {} - {}", method, params.dump());
        }
    }

} // namespace MCP
} // namespace Core
