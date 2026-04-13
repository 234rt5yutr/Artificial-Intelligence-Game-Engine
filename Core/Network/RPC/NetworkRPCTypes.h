#pragma once

#include "Core/Network/Policies/ReplicationPolicyTypes.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Core {
namespace Network {

    constexpr const char* NET_RPC_CONTRACT_INVALID = "NET_RPC_CONTRACT_INVALID";
    constexpr const char* NET_RPC_AUTH_FAILED = "NET_RPC_AUTH_FAILED";

    enum class RPCEntityScope : uint8_t {
        Global = 0,
        Entity
    };

    struct NetworkRPCDescriptor {
        std::string RPCName;
        uint32_t RPCNameHash = 0;
        std::string PayloadSchemaHash;
        RPCEntityScope TargetEntityScope = RPCEntityScope::Global;
        ReplicationReliabilityClass ReliabilityClass = ReplicationReliabilityClass::Reliable;
        bool RequiresAuth = false;
        bool ReplayAllowed = false;
        uint64_t ContractHash = 0;
    };

    struct NetworkRPCRegistrationRequest {
        std::string RPCName;
        RPCEntityScope TargetEntityScope = RPCEntityScope::Global;
        ReplicationReliabilityClass ReliabilityClass = ReplicationReliabilityClass::Reliable;
        bool RequiresAuth = false;
        bool ReplayAllowed = false;
        std::string PayloadSchemaHash;
    };

    struct NetworkRPCRegistrationResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        bool ReplacedExisting = false;
        uint32_t RPCNameHash = 0;
        uint64_t ContractHash = 0;
    };

    struct NetworkRPCValidationResult {
        bool Allowed = false;
        std::string ErrorCode;
        std::string Message;
        NetworkRPCDescriptor Descriptor;
    };

    inline const char* ToString(RPCEntityScope value) {
        switch (value) {
            case RPCEntityScope::Global: return "global";
            case RPCEntityScope::Entity: return "entity";
            default: return "global";
        }
    }

    inline std::optional<RPCEntityScope> ParseRPCEntityScope(const std::string& value) {
        if (value == "global") return RPCEntityScope::Global;
        if (value == "entity") return RPCEntityScope::Entity;
        return std::nullopt;
    }

} // namespace Network
} // namespace Core

