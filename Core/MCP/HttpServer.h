#pragma once

// HTTP/WebSocket server wrapper for MCP communication
// Using cpp-httplib for lightweight HTTP server

#define CPPHTTPLIB_OPENSSL_SUPPORT 0
#include <httplib.h>

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>

namespace Core {
namespace MCP {

    // HTTP request/response wrappers
    struct HttpRequest {
        std::string Method;
        std::string Path;
        std::string Body;
        std::unordered_map<std::string, std::string> Headers;
        std::unordered_map<std::string, std::string> Params;
    };

    struct HttpResponse {
        int StatusCode = 200;
        std::string Body;
        std::string ContentType = "application/json";
        std::unordered_map<std::string, std::string> Headers;
    };

    // HTTP request handler callback
    using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

    // WebSocket message types
    enum class WebSocketOpcode : uint8_t {
        Continuation = 0x0,
        Text = 0x1,
        Binary = 0x2,
        Close = 0x8,
        Ping = 0x9,
        Pong = 0xA
    };

    struct WebSocketMessage {
        WebSocketOpcode Opcode = WebSocketOpcode::Text;
        std::string Data;
        bool IsFinal = true;
    };

    // WebSocket connection handle
    class WebSocketConnection {
    public:
        using MessageHandler = std::function<void(const WebSocketMessage&)>;
        using CloseHandler = std::function<void(int code, const std::string& reason)>;

        WebSocketConnection(int socketFd);
        ~WebSocketConnection();

        void Send(const std::string& message);
        void SendBinary(const std::vector<uint8_t>& data);
        void Close(int code = 1000, const std::string& reason = "");

        void SetOnMessage(MessageHandler handler) { m_OnMessage = std::move(handler); }
        void SetOnClose(CloseHandler handler) { m_OnClose = std::move(handler); }

        bool IsOpen() const { return m_IsOpen; }
        int GetId() const { return m_Id; }

    private:
        int m_SocketFd = -1;
        int m_Id;
        std::atomic<bool> m_IsOpen{ true };
        MessageHandler m_OnMessage;
        CloseHandler m_OnClose;
        std::mutex m_SendMutex;
        
        static std::atomic<int> s_NextId;
    };

    // Lightweight HTTP/WebSocket server
    class HttpServer {
    public:
        HttpServer();
        ~HttpServer();

        // Configuration
        void SetHost(const std::string& host) { m_Host = host; }
        void SetPort(int port) { m_Port = port; }
        void SetThreadPoolSize(size_t size) { m_ThreadPoolSize = size; }

        // Route registration
        void Get(const std::string& path, HttpHandler handler);
        void Post(const std::string& path, HttpHandler handler);
        void Put(const std::string& path, HttpHandler handler);
        void Delete(const std::string& path, HttpHandler handler);
        void Options(const std::string& path, HttpHandler handler);

        // WebSocket upgrade handler
        using WebSocketHandler = std::function<void(std::shared_ptr<WebSocketConnection>)>;
        void WebSocket(const std::string& path, WebSocketHandler handler);

        // Server lifecycle
        bool Start();
        void Stop();
        bool IsRunning() const { return m_Running; }

        // Get server info
        std::string GetHost() const { return m_Host; }
        int GetPort() const { return m_Port; }

    private:
        void ServerThread();
        HttpRequest ConvertRequest(const httplib::Request& req);
        void ApplyResponse(const HttpResponse& resp, httplib::Response& res);

    private:
        std::string m_Host = "127.0.0.1";
        int m_Port = 8080;
        size_t m_ThreadPoolSize = 4;

        std::unique_ptr<httplib::Server> m_Server;
        std::thread m_ServerThread;
        std::atomic<bool> m_Running{ false };

        std::unordered_map<std::string, WebSocketHandler> m_WebSocketHandlers;
        std::mutex m_ConnectionsMutex;
        std::vector<std::shared_ptr<WebSocketConnection>> m_Connections;
    };

    // Simple HTTP client for outgoing requests
    class HttpClient {
    public:
        HttpClient(const std::string& baseUrl);
        ~HttpClient();

        // HTTP methods
        HttpResponse Get(const std::string& path);
        HttpResponse Post(const std::string& path, const std::string& body,
                          const std::string& contentType = "application/json");
        HttpResponse Put(const std::string& path, const std::string& body,
                         const std::string& contentType = "application/json");
        HttpResponse Delete(const std::string& path);

        // Configuration
        void SetTimeout(int seconds) { m_TimeoutSeconds = seconds; }
        void SetHeader(const std::string& name, const std::string& value);

    private:
        std::string m_BaseUrl;
        std::string m_Host;
        int m_Port;
        int m_TimeoutSeconds = 30;
        std::unordered_map<std::string, std::string> m_DefaultHeaders;
        std::unique_ptr<httplib::Client> m_Client;
    };

} // namespace MCP
} // namespace Core
