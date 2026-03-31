#pragma once

// JSON serialization library wrapper
// Using nlohmann/json for MCP protocol and scene serialization

#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <variant>

namespace Core {
namespace MCP {

    // Convenient alias
    using Json = nlohmann::json;

    // JSON parsing utilities
    class JsonUtils {
    public:
        // Parse JSON string, returns nullopt on error
        static std::optional<Json> Parse(const std::string& jsonStr) {
            try {
                return Json::parse(jsonStr);
            } catch (const Json::parse_error&) {
                return std::nullopt;
            }
        }

        // Serialize to string
        static std::string Stringify(const Json& json, int indent = -1) {
            return json.dump(indent);
        }

        // Safe value extraction with default
        template<typename T>
        static T GetOr(const Json& json, const std::string& key, const T& defaultValue) {
            if (json.contains(key) && !json[key].is_null()) {
                try {
                    return json[key].get<T>();
                } catch (...) {
                    return defaultValue;
                }
            }
            return defaultValue;
        }

        // Check if JSON has required fields
        static bool HasFields(const Json& json, std::initializer_list<std::string> fields) {
            for (const auto& field : fields) {
                if (!json.contains(field)) {
                    return false;
                }
            }
            return true;
        }

        // Merge two JSON objects
        static Json Merge(const Json& base, const Json& overlay) {
            Json result = base;
            for (auto& [key, value] : overlay.items()) {
                if (result.contains(key) && result[key].is_object() && value.is_object()) {
                    result[key] = Merge(result[key], value);
                } else {
                    result[key] = value;
                }
            }
            return result;
        }
    };

    // JSON-RPC 2.0 message types (used by MCP)
    struct JsonRpcRequest {
        std::string jsonrpc = "2.0";
        std::string method;
        Json params;
        std::variant<int64_t, std::string> id;

        Json ToJson() const {
            Json j;
            j["jsonrpc"] = jsonrpc;
            j["method"] = method;
            if (!params.is_null()) {
                j["params"] = params;
            }
            if (std::holds_alternative<int64_t>(id)) {
                j["id"] = std::get<int64_t>(id);
            } else {
                j["id"] = std::get<std::string>(id);
            }
            return j;
        }

        static std::optional<JsonRpcRequest> FromJson(const Json& j) {
            if (!j.contains("jsonrpc") || !j.contains("method") || !j.contains("id")) {
                return std::nullopt;
            }
            
            JsonRpcRequest req;
            req.jsonrpc = j["jsonrpc"].get<std::string>();
            req.method = j["method"].get<std::string>();
            req.params = j.value("params", Json::object());
            
            if (j["id"].is_number_integer()) {
                req.id = j["id"].get<int64_t>();
            } else {
                req.id = j["id"].get<std::string>();
            }
            
            return req;
        }
    };

    struct JsonRpcResponse {
        std::string jsonrpc = "2.0";
        Json result;
        Json error;  // null if no error
        std::variant<int64_t, std::string> id;

        Json ToJson() const {
            Json j;
            j["jsonrpc"] = jsonrpc;
            
            if (std::holds_alternative<int64_t>(id)) {
                j["id"] = std::get<int64_t>(id);
            } else {
                j["id"] = std::get<std::string>(id);
            }
            
            if (!error.is_null()) {
                j["error"] = error;
            } else {
                j["result"] = result;
            }
            
            return j;
        }

        static JsonRpcResponse Success(const std::variant<int64_t, std::string>& reqId, 
                                        const Json& res) {
            JsonRpcResponse resp;
            resp.id = reqId;
            resp.result = res;
            return resp;
        }

        static JsonRpcResponse Error(const std::variant<int64_t, std::string>& reqId,
                                      int code, const std::string& message,
                                      const Json& data = nullptr) {
            JsonRpcResponse resp;
            resp.id = reqId;
            resp.error = {
                {"code", code},
                {"message", message}
            };
            if (!data.is_null()) {
                resp.error["data"] = data;
            }
            return resp;
        }
    };

    // Standard JSON-RPC error codes
    namespace JsonRpcError {
        constexpr int ParseError = -32700;
        constexpr int InvalidRequest = -32600;
        constexpr int MethodNotFound = -32601;
        constexpr int InvalidParams = -32602;
        constexpr int InternalError = -32603;
        // MCP-specific error codes (-32000 to -32099)
        constexpr int ServerNotInitialized = -32002;
        constexpr int RequestCancelled = -32800;
    }

} // namespace MCP
} // namespace Core
