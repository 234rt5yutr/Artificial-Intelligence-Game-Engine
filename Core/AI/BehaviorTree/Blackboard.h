#pragma once

// Blackboard - Shared knowledge store for behavior tree nodes
// Provides type-safe value storage with hierarchical support (parent chain lookup)

#include <string>
#include <unordered_map>
#include <any>
#include <optional>
#include <vector>
#include <typeinfo>
#include <sstream>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

namespace Core {
namespace AI {

    using Json = nlohmann::json;

    // Value type identifiers for serialization
    enum class BlackboardValueType : uint8_t {
        None = 0,
        Bool,
        Int,
        Float,
        String,
        Vec3,
        EntityId,
        Custom
    };

    // Blackboard entry for debug inspection
    struct BlackboardEntry {
        std::string Key;
        BlackboardValueType Type;
        std::string ValueString;  // String representation for debugging
    };

    // Blackboard - hierarchical shared memory for AI decisions
    class Blackboard {
    public:
        Blackboard() = default;
        ~Blackboard() = default;

        // Non-copyable to prevent accidental copies
        Blackboard(const Blackboard&) = delete;
        Blackboard& operator=(const Blackboard&) = delete;
        Blackboard(Blackboard&&) = default;
        Blackboard& operator=(Blackboard&&) = default;

        // Set a value in the blackboard
        template<typename T>
        void Set(const std::string& key, const T& value) {
            m_Data[key] = value;
            m_Types[key] = GetValueType<T>();
        }

        // Get a value from the blackboard (with default if not found)
        // Searches parent chain if value not found locally
        template<typename T>
        T Get(const std::string& key, const T& defaultValue = T{}) const {
            auto it = m_Data.find(key);
            if (it != m_Data.end()) {
                try {
                    return std::any_cast<T>(it->second);
                } catch (const std::bad_any_cast&) {
                    return defaultValue;
                }
            }
            
            // Search parent chain
            if (m_Parent) {
                return m_Parent->Get<T>(key, defaultValue);
            }
            
            return defaultValue;
        }

        // Try to get a value, returns std::nullopt if not found or wrong type
        template<typename T>
        std::optional<T> TryGet(const std::string& key) const {
            auto it = m_Data.find(key);
            if (it != m_Data.end()) {
                try {
                    return std::any_cast<T>(it->second);
                } catch (const std::bad_any_cast&) {
                    return std::nullopt;
                }
            }
            
            if (m_Parent) {
                return m_Parent->TryGet<T>(key);
            }
            
            return std::nullopt;
        }

        // Check if key exists (including parent chain)
        bool Has(const std::string& key) const {
            if (m_Data.find(key) != m_Data.end()) {
                return true;
            }
            if (m_Parent) {
                return m_Parent->Has(key);
            }
            return false;
        }

        // Check if key exists with specific type
        template<typename T>
        bool HasType(const std::string& key) const {
            auto it = m_Data.find(key);
            if (it != m_Data.end()) {
                return it->second.type() == typeid(T);
            }
            if (m_Parent) {
                return m_Parent->HasType<T>(key);
            }
            return false;
        }

        // Remove a key from the blackboard
        void Remove(const std::string& key) {
            m_Data.erase(key);
            m_Types.erase(key);
        }

        // Clear all local entries (does not affect parent)
        void Clear() {
            m_Data.clear();
            m_Types.clear();
        }

        // Hierarchical blackboard support
        void SetParent(Blackboard* parent) { m_Parent = parent; }
        Blackboard* GetParent() const { return m_Parent; }

        // Get all keys (local only)
        std::vector<std::string> GetAllKeys() const {
            std::vector<std::string> keys;
            keys.reserve(m_Data.size());
            for (const auto& [key, value] : m_Data) {
                (void)value;
                keys.push_back(key);
            }
            return keys;
        }

        // Get all keys including parent chain
        std::vector<std::string> GetAllKeysRecursive() const {
            std::vector<std::string> keys = GetAllKeys();
            if (m_Parent) {
                auto parentKeys = m_Parent->GetAllKeysRecursive();
                for (const auto& key : parentKeys) {
                    if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                        keys.push_back(key);
                    }
                }
            }
            return keys;
        }

        // Get value as string for debugging
        std::string GetValueAsString(const std::string& key) const {
            auto it = m_Data.find(key);
            if (it == m_Data.end()) {
                if (m_Parent) return m_Parent->GetValueAsString(key);
                return "<not found>";
            }

            return AnyToString(it->second);
        }

        // Get all entries for debug display
        std::vector<BlackboardEntry> GetEntries() const {
            std::vector<BlackboardEntry> entries;
            entries.reserve(m_Data.size());
            
            for (const auto& [key, value] : m_Data) {
                BlackboardEntry entry;
                entry.Key = key;
                
                auto typeIt = m_Types.find(key);
                entry.Type = (typeIt != m_Types.end()) ? typeIt->second : BlackboardValueType::Custom;
                entry.ValueString = AnyToString(value);
                
                entries.push_back(entry);
            }
            
            return entries;
        }

        // Entry count (local only)
        size_t Size() const { return m_Data.size(); }

        // Serialization
        Json ToJson() const {
            Json j = Json::object();
            
            for (const auto& [key, value] : m_Data) {
                auto typeIt = m_Types.find(key);
                BlackboardValueType type = (typeIt != m_Types.end()) 
                    ? typeIt->second : BlackboardValueType::Custom;
                
                j[key] = SerializeValue(value, type);
            }
            
            return j;
        }

        void FromJson(const Json& json) {
            Clear();
            
            if (!json.is_object()) return;
            
            for (auto& [key, value] : json.items()) {
                DeserializeValue(key, value);
            }
        }

    private:
        std::unordered_map<std::string, std::any> m_Data;
        std::unordered_map<std::string, BlackboardValueType> m_Types;
        Blackboard* m_Parent = nullptr;

        // Get value type enum for a type
        template<typename T>
        static BlackboardValueType GetValueType() {
            if constexpr (std::is_same_v<T, bool>) return BlackboardValueType::Bool;
            else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int32_t>) return BlackboardValueType::Int;
            else if constexpr (std::is_same_v<T, float>) return BlackboardValueType::Float;
            else if constexpr (std::is_same_v<T, std::string>) return BlackboardValueType::String;
            else if constexpr (std::is_same_v<T, glm::vec3>) return BlackboardValueType::Vec3;
            else if constexpr (std::is_same_v<T, uint32_t>) return BlackboardValueType::EntityId;
            else return BlackboardValueType::Custom;
        }

        // Convert any value to string
        static std::string AnyToString(const std::any& value) {
            std::stringstream ss;
            
            if (value.type() == typeid(bool)) {
                ss << (std::any_cast<bool>(value) ? "true" : "false");
            } else if (value.type() == typeid(int)) {
                ss << std::any_cast<int>(value);
            } else if (value.type() == typeid(int32_t)) {
                ss << std::any_cast<int32_t>(value);
            } else if (value.type() == typeid(uint32_t)) {
                ss << std::any_cast<uint32_t>(value);
            } else if (value.type() == typeid(float)) {
                ss << std::any_cast<float>(value);
            } else if (value.type() == typeid(double)) {
                ss << std::any_cast<double>(value);
            } else if (value.type() == typeid(std::string)) {
                ss << "\"" << std::any_cast<std::string>(value) << "\"";
            } else if (value.type() == typeid(glm::vec3)) {
                auto v = std::any_cast<glm::vec3>(value);
                ss << "(" << v.x << ", " << v.y << ", " << v.z << ")";
            } else {
                ss << "<" << value.type().name() << ">";
            }
            
            return ss.str();
        }

        // Serialize value to JSON
        static Json SerializeValue(const std::any& value, BlackboardValueType type) {
            Json j = Json::object();
            j["type"] = static_cast<int>(type);
            
            switch (type) {
                case BlackboardValueType::Bool:
                    j["value"] = std::any_cast<bool>(value);
                    break;
                case BlackboardValueType::Int:
                    try {
                        j["value"] = std::any_cast<int>(value);
                    } catch (...) {
                        j["value"] = std::any_cast<int32_t>(value);
                    }
                    break;
                case BlackboardValueType::Float:
                    j["value"] = std::any_cast<float>(value);
                    break;
                case BlackboardValueType::String:
                    j["value"] = std::any_cast<std::string>(value);
                    break;
                case BlackboardValueType::Vec3: {
                    auto v = std::any_cast<glm::vec3>(value);
                    j["value"] = {v.x, v.y, v.z};
                    break;
                }
                case BlackboardValueType::EntityId:
                    j["value"] = std::any_cast<uint32_t>(value);
                    break;
                default:
                    j["value"] = AnyToString(value);
                    break;
            }
            
            return j;
        }

        // Deserialize value from JSON
        void DeserializeValue(const std::string& key, const Json& value) {
            if (!value.is_object() || !value.contains("type") || !value.contains("value")) {
                return;
            }
            
            auto type = static_cast<BlackboardValueType>(value["type"].get<int>());
            
            switch (type) {
                case BlackboardValueType::Bool:
                    Set(key, value["value"].get<bool>());
                    break;
                case BlackboardValueType::Int:
                    Set(key, value["value"].get<int>());
                    break;
                case BlackboardValueType::Float:
                    Set(key, value["value"].get<float>());
                    break;
                case BlackboardValueType::String:
                    Set(key, value["value"].get<std::string>());
                    break;
                case BlackboardValueType::Vec3: {
                    auto arr = value["value"];
                    if (arr.is_array() && arr.size() == 3) {
                        Set(key, glm::vec3(arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>()));
                    }
                    break;
                }
                case BlackboardValueType::EntityId:
                    Set(key, value["value"].get<uint32_t>());
                    break;
                default:
                    // Custom types cannot be deserialized
                    break;
            }
        }
    };

} // namespace AI
} // namespace Core
