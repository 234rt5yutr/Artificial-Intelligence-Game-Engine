#pragma once

// MCP (Model Context Protocol) Types and Structures
// Implements the MCP specification for AI agent communication

#include "JsonSerialization.h"
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <variant>

namespace Core {
namespace MCP {

    // MCP Protocol version
    constexpr const char* MCP_PROTOCOL_VERSION = "2024-11-05";

    // Server capabilities that can be advertised to clients
    struct ServerCapabilities {
        bool SupportsTools = true;
        bool SupportsResources = false;
        bool SupportsPrompts = false;
        bool SupportsLogging = true;
        bool SupportsSampling = false;

        Json ToJson() const {
            Json caps = Json::object();
            
            if (SupportsTools) {
                caps["tools"] = Json::object();
            }
            if (SupportsResources) {
                caps["resources"] = Json::object();
            }
            if (SupportsPrompts) {
                caps["prompts"] = Json::object();
            }
            if (SupportsLogging) {
                caps["logging"] = Json::object();
            }
            if (SupportsSampling) {
                caps["sampling"] = Json::object();
            }
            
            return caps;
        }
    };

    // Server information
    struct ServerInfo {
        std::string Name = "AIGameEngine-MCP";
        std::string Version = "1.0.0";

        Json ToJson() const {
            return {
                {"name", Name},
                {"version", Version}
            };
        }
    };

    // Client information received during initialization
    struct ClientInfo {
        std::string Name;
        std::string Version;

        static std::optional<ClientInfo> FromJson(const Json& j) {
            if (!j.contains("name") || !j.contains("version")) {
                return std::nullopt;
            }
            ClientInfo info;
            info.Name = j["name"].get<std::string>();
            info.Version = j["version"].get<std::string>();
            return info;
        }
    };

    // Tool input schema (JSON Schema format)
    struct ToolInputSchema {
        std::string Type = "object";
        Json Properties = Json::object();
        std::vector<std::string> Required;

        Json ToJson() const {
            Json schema = {
                {"type", Type},
                {"properties", Properties}
            };
            if (!Required.empty()) {
                schema["required"] = Required;
            }
            return schema;
        }
    };

    // Tool definition for listing available tools
    struct ToolDefinition {
        std::string Name;
        std::string Description;
        ToolInputSchema InputSchema;

        Json ToJson() const {
            return {
                {"name", Name},
                {"description", Description},
                {"inputSchema", InputSchema.ToJson()}
            };
        }
    };

    // Content types in tool results
    enum class ContentType {
        Text,
        Image,
        Resource
    };

    // Tool result content item
    struct ContentItem {
        ContentType Type = ContentType::Text;
        std::string Text;
        std::string MimeType;  // For images
        std::string Data;       // Base64 for images, URI for resources

        Json ToJson() const {
            Json item;
            switch (Type) {
                case ContentType::Text:
                    item["type"] = "text";
                    item["text"] = Text;
                    break;
                case ContentType::Image:
                    item["type"] = "image";
                    item["data"] = Data;
                    item["mimeType"] = MimeType;
                    break;
                case ContentType::Resource:
                    item["type"] = "resource";
                    item["resource"] = {
                        {"uri", Data},
                        {"text", Text},
                        {"mimeType", MimeType}
                    };
                    break;
            }
            return item;
        }

        static ContentItem TextContent(const std::string& text) {
            ContentItem item;
            item.Type = ContentType::Text;
            item.Text = text;
            return item;
        }

        static ContentItem ImageContent(const std::string& base64Data, 
                                         const std::string& mimeType = "image/png") {
            ContentItem item;
            item.Type = ContentType::Image;
            item.Data = base64Data;
            item.MimeType = mimeType;
            return item;
        }
    };

    // Tool execution result
    struct ToolResult {
        std::vector<ContentItem> Content;
        bool IsError = false;

        Json ToJson() const {
            Json contentArray = Json::array();
            for (const auto& item : Content) {
                contentArray.push_back(item.ToJson());
            }
            
            Json result = {
                {"content", contentArray}
            };
            
            if (IsError) {
                result["isError"] = true;
            }
            
            return result;
        }

        static ToolResult Success(const std::string& text) {
            ToolResult result;
            result.Content.push_back(ContentItem::TextContent(text));
            return result;
        }

        static ToolResult Error(const std::string& errorMessage) {
            ToolResult result;
            result.Content.push_back(ContentItem::TextContent(errorMessage));
            result.IsError = true;
            return result;
        }

        static ToolResult SuccessJson(const Json& data) {
            return Success(data.dump(2));
        }
    };

    // Tool call request from client
    struct ToolCallRequest {
        std::string Name;
        Json Arguments;

        static std::optional<ToolCallRequest> FromJson(const Json& j) {
            if (!j.contains("name")) {
                return std::nullopt;
            }
            ToolCallRequest req;
            req.Name = j["name"].get<std::string>();
            req.Arguments = j.value("arguments", Json::object());
            return req;
        }
    };

    // Log levels for MCP logging
    enum class LogLevel {
        Debug,
        Info,
        Notice,
        Warning,
        Error,
        Critical,
        Alert,
        Emergency
    };

    inline std::string LogLevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::Debug: return "debug";
            case LogLevel::Info: return "info";
            case LogLevel::Notice: return "notice";
            case LogLevel::Warning: return "warning";
            case LogLevel::Error: return "error";
            case LogLevel::Critical: return "critical";
            case LogLevel::Alert: return "alert";
            case LogLevel::Emergency: return "emergency";
            default: return "info";
        }
    }

    // Notification types
    namespace NotificationType {
        constexpr const char* Initialized = "notifications/initialized";
        constexpr const char* Progress = "notifications/progress";
        constexpr const char* ResourcesChanged = "notifications/resources/list_changed";
        constexpr const char* ToolsChanged = "notifications/tools/list_changed";
        constexpr const char* LogMessage = "notifications/message";
    }

    // MCP method names
    namespace MCPMethod {
        constexpr const char* Initialize = "initialize";
        constexpr const char* Ping = "ping";
        constexpr const char* ListTools = "tools/list";
        constexpr const char* CallTool = "tools/call";
        constexpr const char* ListResources = "resources/list";
        constexpr const char* ReadResource = "resources/read";
        constexpr const char* ListPrompts = "prompts/list";
        constexpr const char* GetPrompt = "prompts/get";
        constexpr const char* SetLogLevel = "logging/setLevel";
        constexpr const char* Complete = "completion/complete";
    }

    // Progress token for long-running operations
    using ProgressToken = std::variant<int64_t, std::string>;

    struct ProgressNotification {
        ProgressToken Token;
        double Progress;           // 0.0 to 1.0
        std::optional<double> Total;

        Json ToJson() const {
            Json j;
            if (std::holds_alternative<int64_t>(Token)) {
                j["progressToken"] = std::get<int64_t>(Token);
            } else {
                j["progressToken"] = std::get<std::string>(Token);
            }
            j["progress"] = Progress;
            if (Total.has_value()) {
                j["total"] = Total.value();
            }
            return j;
        }
    };

} // namespace MCP
} // namespace Core
