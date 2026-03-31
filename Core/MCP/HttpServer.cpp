#include "HttpServer.h"
#include "Core/Log.h"
#include <sstream>
#include <regex>

#define LOG_INFO(...) ENGINE_CORE_INFO(__VA_ARGS__)
#define LOG_WARN(...) ENGINE_CORE_WARN(__VA_ARGS__)
#define LOG_ERROR(...) ENGINE_CORE_ERROR(__VA_ARGS__)
#define LOG_TRACE(...) ENGINE_CORE_TRACE(__VA_ARGS__)

namespace Core {
namespace MCP {

    // WebSocketConnection implementation
    std::atomic<int> WebSocketConnection::s_NextId{ 1 };

    WebSocketConnection::WebSocketConnection(int socketFd)
        : m_SocketFd(socketFd)
        , m_Id(s_NextId.fetch_add(1)) {
    }

    WebSocketConnection::~WebSocketConnection() {
        Close();
    }

    void WebSocketConnection::Send(const std::string& message) {
        if (!m_IsOpen) return;
        
        std::lock_guard lock(m_SendMutex);
        // Note: In a full implementation, this would encode and send WebSocket frames
        // For now, this is a placeholder that would be implemented with actual socket I/O
        LOG_TRACE("WebSocket send: {} bytes", message.size());
    }

    void WebSocketConnection::SendBinary(const std::vector<uint8_t>& data) {
        if (!m_IsOpen) return;
        
        std::lock_guard lock(m_SendMutex);
        LOG_TRACE("WebSocket send binary: {} bytes", data.size());
    }

    void WebSocketConnection::Close(int code, const std::string& reason) {
        if (!m_IsOpen.exchange(false)) return;
        
        if (m_OnClose) {
            m_OnClose(code, reason);
        }
        
        LOG_TRACE("WebSocket connection {} closed: {} - {}", m_Id, code, reason);
    }

    // HttpServer implementation
    HttpServer::HttpServer() {
        m_Server = std::make_unique<httplib::Server>();
    }

    HttpServer::~HttpServer() {
        Stop();
    }

    HttpRequest HttpServer::ConvertRequest(const httplib::Request& req) {
        HttpRequest result;
        result.Method = req.method;
        result.Path = req.path;
        result.Body = req.body;
        
        for (const auto& [name, value] : req.headers) {
            result.Headers[name] = value;
        }
        
        for (const auto& [name, value] : req.params) {
            result.Params[name] = value;
        }
        
        return result;
    }

    void HttpServer::ApplyResponse(const HttpResponse& resp, httplib::Response& res) {
        res.status = resp.StatusCode;
        res.set_content(resp.Body, resp.ContentType);
        
        for (const auto& [name, value] : resp.Headers) {
            res.set_header(name, value);
        }
    }

    void HttpServer::Get(const std::string& path, HttpHandler handler) {
        m_Server->Get(path, [this, handler](const httplib::Request& req, httplib::Response& res) {
            HttpRequest httpReq = ConvertRequest(req);
            HttpResponse httpResp = handler(httpReq);
            ApplyResponse(httpResp, res);
        });
    }

    void HttpServer::Post(const std::string& path, HttpHandler handler) {
        m_Server->Post(path, [this, handler](const httplib::Request& req, httplib::Response& res) {
            HttpRequest httpReq = ConvertRequest(req);
            HttpResponse httpResp = handler(httpReq);
            ApplyResponse(httpResp, res);
        });
    }

    void HttpServer::Put(const std::string& path, HttpHandler handler) {
        m_Server->Put(path, [this, handler](const httplib::Request& req, httplib::Response& res) {
            HttpRequest httpReq = ConvertRequest(req);
            HttpResponse httpResp = handler(httpReq);
            ApplyResponse(httpResp, res);
        });
    }

    void HttpServer::Delete(const std::string& path, HttpHandler handler) {
        m_Server->Delete(path, [this, handler](const httplib::Request& req, httplib::Response& res) {
            HttpRequest httpReq = ConvertRequest(req);
            HttpResponse httpResp = handler(httpReq);
            ApplyResponse(httpResp, res);
        });
    }

    void HttpServer::Options(const std::string& path, HttpHandler handler) {
        m_Server->Options(path, [this, handler](const httplib::Request& req, httplib::Response& res) {
            HttpRequest httpReq = ConvertRequest(req);
            HttpResponse httpResp = handler(httpReq);
            ApplyResponse(httpResp, res);
        });
    }

    void HttpServer::WebSocket(const std::string& path, WebSocketHandler handler) {
        m_WebSocketHandlers[path] = std::move(handler);
        
        // Register upgrade handler
        m_Server->Get(path, [this, path](const httplib::Request& req, httplib::Response& res) {
            // Check for WebSocket upgrade
            auto upgrade = req.get_header_value("Upgrade");
            if (upgrade != "websocket") {
                res.status = 400;
                res.set_content("Expected WebSocket upgrade", "text/plain");
                return;
            }
            
            // Note: cpp-httplib doesn't natively support WebSocket
            // This would require additional implementation or a different library
            // For MCP over HTTP, we use Server-Sent Events as an alternative
            
            res.status = 501;
            res.set_content("WebSocket not implemented - use SSE endpoint", "text/plain");
        });
    }

    bool HttpServer::Start() {
        if (m_Running) {
            LOG_WARN("HTTP server already running");
            return false;
        }

        // Configure server
        m_Server->new_task_queue = [this] { 
            return new httplib::ThreadPool(m_ThreadPoolSize); 
        };

        // Add CORS headers for MCP clients
        m_Server->set_default_headers({
            {"Access-Control-Allow-Origin", "*"},
            {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
            {"Access-Control-Allow-Headers", "Content-Type, Authorization"}
        });

        m_Running = true;
        m_ServerThread = std::thread(&HttpServer::ServerThread, this);

        LOG_INFO("HTTP server starting on {}:{}", m_Host, m_Port);
        return true;
    }

    void HttpServer::Stop() {
        if (!m_Running) return;

        m_Running = false;
        m_Server->stop();

        if (m_ServerThread.joinable()) {
            m_ServerThread.join();
        }

        // Close all WebSocket connections
        {
            std::lock_guard lock(m_ConnectionsMutex);
            for (auto& conn : m_Connections) {
                conn->Close(1001, "Server shutting down");
            }
            m_Connections.clear();
        }

        LOG_INFO("HTTP server stopped");
    }

    void HttpServer::ServerThread() {
        LOG_INFO("HTTP server listening on {}:{}", m_Host, m_Port);
        
        if (!m_Server->listen(m_Host, m_Port)) {
            LOG_ERROR("Failed to start HTTP server on {}:{}", m_Host, m_Port);
            m_Running = false;
        }
    }

    // HttpClient implementation
    HttpClient::HttpClient(const std::string& baseUrl)
        : m_BaseUrl(baseUrl) {
        
        // Parse URL
        std::regex urlRegex(R"(^(https?://)?([^:/]+)(?::(\d+))?(.*)$)");
        std::smatch match;
        
        if (std::regex_match(baseUrl, match, urlRegex)) {
            m_Host = match[2].str();
            m_Port = match[3].length() > 0 ? std::stoi(match[3].str()) : 80;
        } else {
            m_Host = baseUrl;
            m_Port = 80;
        }
        
        m_Client = std::make_unique<httplib::Client>(m_Host, m_Port);
        m_Client->set_connection_timeout(m_TimeoutSeconds);
        m_Client->set_read_timeout(m_TimeoutSeconds);
    }

    HttpClient::~HttpClient() = default;

    void HttpClient::SetHeader(const std::string& name, const std::string& value) {
        m_DefaultHeaders[name] = value;
    }

    HttpResponse HttpClient::Get(const std::string& path) {
        httplib::Headers headers;
        for (const auto& [name, value] : m_DefaultHeaders) {
            headers.emplace(name, value);
        }
        
        auto result = m_Client->Get(path, headers);
        
        HttpResponse resp;
        if (result) {
            resp.StatusCode = result->status;
            resp.Body = result->body;
            resp.ContentType = result->get_header_value("Content-Type");
        } else {
            resp.StatusCode = 0;
            resp.Body = "Request failed";
        }
        
        return resp;
    }

    HttpResponse HttpClient::Post(const std::string& path, const std::string& body,
                                   const std::string& contentType) {
        httplib::Headers headers;
        for (const auto& [name, value] : m_DefaultHeaders) {
            headers.emplace(name, value);
        }
        
        auto result = m_Client->Post(path, headers, body, contentType);
        
        HttpResponse resp;
        if (result) {
            resp.StatusCode = result->status;
            resp.Body = result->body;
            resp.ContentType = result->get_header_value("Content-Type");
        } else {
            resp.StatusCode = 0;
            resp.Body = "Request failed";
        }
        
        return resp;
    }

    HttpResponse HttpClient::Put(const std::string& path, const std::string& body,
                                  const std::string& contentType) {
        httplib::Headers headers;
        for (const auto& [name, value] : m_DefaultHeaders) {
            headers.emplace(name, value);
        }
        
        auto result = m_Client->Put(path, headers, body, contentType);
        
        HttpResponse resp;
        if (result) {
            resp.StatusCode = result->status;
            resp.Body = result->body;
            resp.ContentType = result->get_header_value("Content-Type");
        } else {
            resp.StatusCode = 0;
            resp.Body = "Request failed";
        }
        
        return resp;
    }

    HttpResponse HttpClient::Delete(const std::string& path) {
        httplib::Headers headers;
        for (const auto& [name, value] : m_DefaultHeaders) {
            headers.emplace(name, value);
        }
        
        auto result = m_Client->Delete(path, headers);
        
        HttpResponse resp;
        if (result) {
            resp.StatusCode = result->status;
            resp.Body = result->body;
            resp.ContentType = result->get_header_value("Content-Type");
        } else {
            resp.StatusCode = 0;
            resp.Body = "Request failed";
        }
        
        return resp;
    }

} // namespace MCP
} // namespace Core
