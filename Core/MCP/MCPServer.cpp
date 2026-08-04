#include "MCPServer.h"
#include "Core/Profile.h"
#include <algorithm>
#include <chrono>
#include <future>
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
        RebuildConfigCapabilities();
        
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
        RebuildConfigCapabilities();
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

        // MCP caps tool names at 64 characters. Rejecting here surfaces the
        // problem at startup instead of at a client's tools/list.
        if (name.empty() || name.size() > MCP_MAX_TOOL_NAME_LENGTH) {
            ENGINE_CORE_ERROR("MCP: Rejecting tool '{}': name must be 1-{} characters",
                              name, MCP_MAX_TOOL_NAME_LENGTH);
            return;
        }

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
        m_AcceptingQueuedRequests = true;
        ENGINE_CORE_INFO("MCP Server started at {}", GetEndpointUrl());

        return true;
    }

    void MCPServer::Stop() {
        if (!m_Running) return;

        m_Running = false;
        // Stop queueing before tearing down the transport so no worker parks on a
        // future that will never be completed.
        m_AcceptingQueuedRequests = false;
        m_HttpServer->Stop();

        // Fail anything still queued rather than dropping it: a dropped promise
        // would surface to the waiting worker as a broken_promise.
        std::queue<PendingRequest> abandoned;
        {
            std::lock_guard lock(m_RequestQueueMutex);
            std::swap(abandoned, m_PendingRequests);
        }
        while (!abandoned.empty()) {
            auto& req = abandoned.front();
            if (req.Callback) {
                req.Callback(JsonRpcResponse::Error(
                    req.Request.id,
                    JsonRpcError::InternalError,
                    "MCP server is shutting down"
                ));
            }
            abandoned.pop();
        }

        m_MainThreadPumpSeen.store(false);

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

        // Tells HTTP workers it is safe to hand work over instead of running it inline.
        m_MainThreadPumpSeen.store(true);

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
            if (auto rejection = RejectRequest(req)) {
                return *rejection;
            }
            return HandleHealthCheck(req);
        });

        // Tools list endpoint (convenience, not standard MCP)
        m_HttpServer->Get("/tools", [this](const HttpRequest& req) {
            if (auto rejection = RejectRequest(req)) {
                return *rejection;
            }
            return HandleListTools(req);
        });

        // CORS preflight. Only the explicitly allowed origins get an
        // Access-Control-Allow-Origin back; a wildcard here would hand any web page
        // a driver's seat on the running engine.
        if (m_Config.EnableCORS && !m_Config.AllowedOrigins.empty()) {
            auto preflight = [this](const HttpRequest& req) {
                HttpResponse resp;
                auto originIt = req.Headers.find("Origin");
                if (originIt == req.Headers.end()) {
                    originIt = req.Headers.find("origin");
                }
                const bool allowed = originIt != req.Headers.end() &&
                    std::find(m_Config.AllowedOrigins.begin(),
                              m_Config.AllowedOrigins.end(),
                              originIt->second) != m_Config.AllowedOrigins.end();
                if (!allowed) {
                    resp.StatusCode = 403;
                    resp.Body = Json{{"error", "Origin not allowed"}}.dump();
                    return resp;
                }
                resp.StatusCode = 204;
                resp.Headers["Access-Control-Allow-Origin"] = originIt->second;
                resp.Headers["Vary"] = "Origin";
                resp.Headers["Access-Control-Allow-Methods"] = "POST, GET, OPTIONS";
                resp.Headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";
                return resp;
            };

            m_HttpServer->Options("/mcp", preflight);
            m_HttpServer->Options("/", preflight);
        }
    }

    std::optional<HttpResponse> MCPServer::RejectRequest(const HttpRequest& request) const {
        auto deny = [](int status, const std::string& message) {
            HttpResponse resp;
            resp.StatusCode = status;
            resp.ContentType = "application/json";
            resp.Body = Json{{"error", message}}.dump();
            return resp;
        };

        if (request.Body.size() > m_Config.MaxRequestBodyBytes) {
            return deny(413, "Request body too large");
        }

        // DNS-rebinding protection. Only browsers attach Origin, so this cannot
        // break curl/CLI clients, but it does stop a malicious page from driving
        // the engine through the user's loopback interface.
        auto originIt = request.Headers.find("Origin");
        if (originIt == request.Headers.end()) {
            originIt = request.Headers.find("origin");
        }
        if (originIt != request.Headers.end() && !originIt->second.empty()) {
            const bool allowed = std::find(m_Config.AllowedOrigins.begin(),
                                           m_Config.AllowedOrigins.end(),
                                           originIt->second) != m_Config.AllowedOrigins.end();
            if (!allowed) {
                ENGINE_CORE_WARN("MCP: rejected request from disallowed origin '{}'", originIt->second);
                return deny(403, "Origin not allowed");
            }
        }

        if (!m_Config.AuthToken.empty()) {
            auto authIt = request.Headers.find("Authorization");
            if (authIt == request.Headers.end()) {
                authIt = request.Headers.find("authorization");
            }
            const std::string expected = "Bearer " + m_Config.AuthToken;
            if (authIt == request.Headers.end() || authIt->second != expected) {
                return deny(401, "Missing or invalid bearer token");
            }
        }

        return std::nullopt;
    }

    HttpResponse MCPServer::HandleMCPRequest(const HttpRequest& request) {
        PROFILE_FUNCTION();
        ++m_RequestCount;

        if (auto rejection = RejectRequest(request)) {
            return *rejection;
        }

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

        // Dispatches one parsed message; returns nullopt for notifications, which
        // JSON-RPC forbids answering.
        auto dispatch = [this](const Json& message) -> std::optional<Json> {
            auto rpcRequestOpt = JsonRpcRequest::FromJson(message);
            if (!rpcRequestOpt) {
                return JsonRpcResponse::Error(
                    std::variant<int64_t, std::string>(std::string("")),
                    JsonRpcError::InvalidRequest,
                    "Invalid JSON-RPC request"
                ).ToJson();
            }

            // Tool execution touches the ECS registry and other non-thread-safe
            // engine state, so it must run on the main thread rather than this HTTP
            // worker. Everything else only reads mutex-guarded server state.
            const bool needsMainThread = (rpcRequestOpt->method == MCPMethod::CallTool);
            auto rpcResponse = needsMainThread
                ? DispatchOnMainThread(*rpcRequestOpt)
                : HandleJsonRpcRequest(*rpcRequestOpt);

            if (rpcRequestOpt->IsNotification()) {
                return std::nullopt;
            }
            return rpcResponse.ToJson();
        };

        // JSON-RPC batch: an array of messages, answered with an array of the
        // responses that are not notifications.
        if (jsonOpt->is_array()) {
            if (jsonOpt->empty()) {
                response.Body = JsonRpcResponse::Error(
                    std::variant<int64_t, std::string>(std::string("")),
                    JsonRpcError::InvalidRequest,
                    "Empty batch"
                ).ToJson().dump();
                response.StatusCode = 400;
                return response;
            }

            Json results = Json::array();
            for (const auto& message : *jsonOpt) {
                if (auto result = dispatch(message)) {
                    results.push_back(std::move(*result));
                }
            }

            if (results.empty()) {
                response.StatusCode = 202;  // batch of notifications only
                response.Body.clear();
                return response;
            }
            response.Body = results.dump();
            return response;
        }

        auto result = dispatch(*jsonOpt);
        if (!result) {
            response.StatusCode = 202;  // notification accepted, nothing to return
            response.Body.clear();
            return response;
        }

        response.Body = result->dump();
        return response;
    }

    JsonRpcResponse MCPServer::DispatchOnMainThread(const JsonRpcRequest& request) {
        // No pump has ever run (embedded host that does not call
        // ProcessPendingRequests, or a request that beat the first frame).
        // Running inline is the only way to make progress.
        if (!m_MainThreadPumpSeen.load() || !m_AcceptingQueuedRequests.load()) {
            return HandleJsonRpcRequest(request);
        }

        auto promise = std::make_shared<std::promise<JsonRpcResponse>>();
        auto future = promise->get_future();

        PendingRequest pending;
        pending.Request = request;
        pending.Callback = [promise](JsonRpcResponse response) {
            promise->set_value(std::move(response));
        };
        QueueMainThreadRequest(std::move(pending));

        const auto timeout = std::chrono::milliseconds(
            m_Config.MainThreadDispatchTimeoutMs > 0 ? m_Config.MainThreadDispatchTimeoutMs : 10000);

        if (future.wait_for(timeout) != std::future_status::ready) {
            ENGINE_CORE_ERROR("MCP: main-thread dispatch timed out for method '{}'", request.method);
            return JsonRpcResponse::Error(
                request.id,
                JsonRpcError::InternalError,
                "Timed out waiting for the engine main thread"
            );
        }

        try {
            return future.get();
        }
        catch (const std::exception& e) {
            // Reached if the server shut down and dropped the queued request.
            return JsonRpcResponse::Error(
                request.id,
                JsonRpcError::InternalError,
                std::string("Request was not completed: ") + e.what()
            );
        }
    }

    HttpResponse MCPServer::HandleHealthCheck(const HttpRequest&) {
        HttpResponse response;
        response.ContentType = "application/json";
        
        // Runs on an HTTP worker: both fields are mutated from other threads.
        bool initialized = false;
        {
            std::lock_guard lock(m_SessionMutex);
            initialized = m_Session.Initialized;
        }
        size_t toolCount = 0;
        {
            std::lock_guard lock(m_ToolsMutex);
            toolCount = m_Tools.size();
        }

        Json health = {
            {"status", "ok"},
            {"server", m_ServerInfo.ToJson()},
            {"initialized", initialized},
            {"toolCount", toolCount},
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
        else if (method == MCPMethod::ListResources) {
            return HandleResourcesList(request);
        }
        else if (method == MCPMethod::ReadResource) {
            return HandleResourcesRead(request);
        }
        else if (method == MCPMethod::ListPrompts) {
            return HandlePromptsList(request);
        }
        else if (method.rfind("notifications/", 0) == 0) {
            // Client-side notifications (initialized, cancelled, progress). Nothing
            // to do beyond accepting them; the caller drops the response because
            // notifications carry no id.
            ENGINE_CORE_TRACE("MCP: received notification '{}'", method);
            return JsonRpcResponse::Success(request.id, Json::object());
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

    JsonRpcResponse MCPServer::HandleResourcesList(const JsonRpcRequest& request) {
        // Resources are not backed by an asset registry yet; report an empty list
        // rather than method-not-found so clients complete discovery cleanly.
        return JsonRpcResponse::Success(request.id, Json{{"resources", Json::array()}});
    }

    JsonRpcResponse MCPServer::HandleResourcesRead(const JsonRpcRequest& request) {
        const std::string uri = request.params.value("uri", std::string());
        return JsonRpcResponse::Error(
            request.id,
            JsonRpcError::InvalidParams,
            "Unknown resource: " + uri
        );
    }

    JsonRpcResponse MCPServer::HandlePromptsList(const JsonRpcRequest& request) {
        return JsonRpcResponse::Success(request.id, Json{{"prompts", Json::array()}});
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

        std::unordered_set<std::string> grantedCapabilities = m_ConfigCapabilities;
        if (request.params.contains("capabilityScopes") && request.params["capabilityScopes"].is_array()) {
            grantedCapabilities.clear();
            for (const auto& capability : request.params["capabilityScopes"]) {
                if (capability.is_string()) {
                    grantedCapabilities.insert(capability.get<std::string>());
                }
            }
        }

        // Update session state
        {
            std::lock_guard lock(m_SessionMutex);
            m_Session.Initialized = true;
            m_Session.Client = clientInfo;
            m_Session.GrantedCapabilities = grantedCapabilities;
        }

        ENGINE_CORE_INFO("MCP: Client initialized - {} v{}", 
                         clientInfo.Name, clientInfo.Version);

        Json capabilitiesArray = Json::array();
        for (const auto& capability : grantedCapabilities) {
            capabilitiesArray.push_back(capability);
        }

        // Protocol version negotiation: echo the client's version when we support
        // it, otherwise answer with ours and let the client decide.
        std::string negotiatedVersion = MCP_PROTOCOL_VERSION;
        if (request.params.contains("protocolVersion") && request.params["protocolVersion"].is_string()) {
            const std::string requested = request.params["protocolVersion"].get<std::string>();
            if (std::find(std::begin(MCP_SUPPORTED_PROTOCOL_VERSIONS),
                          std::end(MCP_SUPPORTED_PROTOCOL_VERSIONS),
                          requested) != std::end(MCP_SUPPORTED_PROTOCOL_VERSIONS)) {
                negotiatedVersion = requested;
            } else {
                ENGINE_CORE_WARN("MCP: client requested unsupported protocol version '{}', offering '{}'",
                                 requested, negotiatedVersion);
            }
        }

        // Build response
        Json result = {
            {"protocolVersion", negotiatedVersion},
            {"capabilities", m_Capabilities.ToJson()},
            {"serverInfo", m_ServerInfo.ToJson()},
            {"capabilityScopes", capabilitiesArray},
            {"capabilityScopeEnforced", m_Config.EnforceCapabilityScopes}
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

        if (m_Config.EnforceCapabilityScopes) {
            const auto requiredCapabilities = tool->RequiredCapabilities();
            for (const std::string& capability : requiredCapabilities) {
                if (!HasGrantedCapability(capability)) {
                    return JsonRpcResponse::Error(
                        request.id,
                        JsonRpcError::InvalidParams,
                        "Missing capability scope: " + capability
                    );
                }
            }
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

    bool MCPServer::HasGrantedCapability(const std::string& capability) const {
        std::lock_guard lock(m_SessionMutex);

        if (!m_Session.GrantedCapabilities.empty()) {
            return m_Session.GrantedCapabilities.contains(capability);
        }

        return m_ConfigCapabilities.contains(capability);
    }

    void MCPServer::RebuildConfigCapabilities() {
        m_ConfigCapabilities.clear();
        for (const std::string& capability : m_Config.GrantedCapabilities) {
            m_ConfigCapabilities.insert(capability);
        }
    }

} // namespace MCP
} // namespace Core
