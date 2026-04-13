#include "Core/Network/RPC/NetworkRPCRegistry.h"

#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"
#include "Core/Network/NetworkContractState.h"
#include "Core/Network/NetworkHash.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace Core {
namespace Network {

    namespace {

        constexpr size_t MAX_RPC_NAME_LENGTH = 96;
        constexpr size_t MAX_SCHEMA_HASH_LENGTH = 128;

        std::string NormalizeToken(std::string_view token) {
            std::string normalized(token);
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return normalized;
        }

    } // namespace

    NetworkRPCRegistry& NetworkRPCRegistry::Get() {
        static NetworkRPCRegistry instance;
        return instance;
    }

    NetworkRPCRegistrationResult NetworkRPCRegistry::RegisterNetworkRPC(const NetworkRPCRegistrationRequest& request) {
        std::scoped_lock lock(m_Mutex);

        std::string validationError;
        if (!ValidateRequest(request, validationError)) {
            NetworkRPCRegistrationResult result;
            result.Success = false;
            result.ErrorCode = NET_RPC_CONTRACT_INVALID;
            result.Message = validationError;
            return result;
        }

        const std::string normalizedRpcName = NormalizeToken(request.RPCName);
        const uint32_t rpcNameHash = HashStringTo32(normalizedRpcName, false);
        const uint64_t descriptorHash = ComputeDescriptorHash(request, rpcNameHash);

        bool replacedExisting = false;
        auto existingIt = m_RpcsByHash.find(rpcNameHash);
        if (existingIt != m_RpcsByHash.end()) {
            const NetworkRPCDescriptor& existingDescriptor = existingIt->second;
            const bool equivalent =
                existingDescriptor.RPCName == request.RPCName &&
                existingDescriptor.TargetEntityScope == request.TargetEntityScope &&
                existingDescriptor.ReliabilityClass == request.ReliabilityClass &&
                existingDescriptor.RequiresAuth == request.RequiresAuth &&
                existingDescriptor.ReplayAllowed == request.ReplayAllowed &&
                existingDescriptor.PayloadSchemaHash == request.PayloadSchemaHash;

            if (!equivalent) {
                NetworkRPCRegistrationResult result;
                result.Success = false;
                result.ErrorCode = NET_RPC_CONTRACT_INVALID;
                result.Message = "RPC hash collision/conflict detected for '" + request.RPCName + "'.";
                return result;
            }
            replacedExisting = true;
        }

        NetworkRPCDescriptor descriptor;
        descriptor.RPCName = request.RPCName;
        descriptor.RPCNameHash = rpcNameHash;
        descriptor.PayloadSchemaHash = request.PayloadSchemaHash;
        descriptor.TargetEntityScope = request.TargetEntityScope;
        descriptor.ReliabilityClass = request.ReliabilityClass;
        descriptor.RequiresAuth = request.RequiresAuth;
        descriptor.ReplayAllowed = request.ReplayAllowed;
        descriptor.ContractHash = descriptorHash;

        m_RpcsByHash[rpcNameHash] = descriptor;
        m_NameToHash[normalizedRpcName] = rpcNameHash;
        RebuildContractHashLocked();
        RebuildDispatchSnapshotLocked();

        SetRPCContractHash(m_ContractHash);
        NetworkDiagnosticsState& diagnostics = NetworkDiagnosticsState::Get();
        diagnostics.SetRegisteredRPCContracts(static_cast<uint32_t>(m_RpcsByHash.size()));
        diagnostics.SetContractHash(GetNetworkContractHash());
        diagnostics.RecordEvent("NetworkRPCRegistered: " + request.RPCName);

        NetworkRPCRegistrationResult result;
        result.Success = true;
        result.Message = replacedExisting ? "RPC contract refreshed." : "RPC contract registered.";
        result.ReplacedExisting = replacedExisting;
        result.RPCNameHash = rpcNameHash;
        result.ContractHash = m_ContractHash;
        return result;
    }

    NetworkRPCValidationResult NetworkRPCRegistry::ValidateInvocation(
        uint32_t rpcNameHash,
        bool callerAuthenticated,
        bool replayContext) const {
        std::shared_ptr<const std::unordered_map<uint32_t, NetworkRPCDescriptor>> snapshot;
        {
            std::scoped_lock lock(m_Mutex);
            snapshot = m_DispatchSnapshot;
        }

        NetworkRPCValidationResult result;
        if (!snapshot) {
            result.Allowed = false;
            result.ErrorCode = NET_RPC_CONTRACT_INVALID;
            result.Message = "RPC dispatch snapshot unavailable.";
            return result;
        }

        auto descriptorIt = snapshot->find(rpcNameHash);
        if (descriptorIt == snapshot->end()) {
            result.Allowed = false;
            result.ErrorCode = NET_RPC_CONTRACT_INVALID;
            result.Message = "RPC is not registered.";
            return result;
        }

        const NetworkRPCDescriptor& descriptor = descriptorIt->second;
        result.Descriptor = descriptor;

        if (descriptor.RequiresAuth && !callerAuthenticated) {
            result.Allowed = false;
            result.ErrorCode = NET_RPC_AUTH_FAILED;
            result.Message = "RPC requires authenticated caller.";
            return result;
        }

        if (replayContext && !descriptor.ReplayAllowed) {
            result.Allowed = false;
            result.ErrorCode = NET_RPC_CONTRACT_INVALID;
            result.Message = "RPC is not replay-authorized.";
            return result;
        }

        result.Allowed = true;
        return result;
    }

    std::optional<NetworkRPCDescriptor> NetworkRPCRegistry::FindByHash(uint32_t rpcNameHash) const {
        std::scoped_lock lock(m_Mutex);
        auto it = m_RpcsByHash.find(rpcNameHash);
        if (it == m_RpcsByHash.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::vector<NetworkRPCDescriptor> NetworkRPCRegistry::GetRegisteredRPCs() const {
        std::scoped_lock lock(m_Mutex);

        std::vector<NetworkRPCDescriptor> descriptors;
        descriptors.reserve(m_RpcsByHash.size());
        for (const auto& [rpcHash, descriptor] : m_RpcsByHash) {
            (void)rpcHash;
            descriptors.push_back(descriptor);
        }
        std::sort(descriptors.begin(), descriptors.end(), [](const NetworkRPCDescriptor& left, const NetworkRPCDescriptor& right) {
            return left.RPCName < right.RPCName;
        });
        return descriptors;
    }

    uint32_t NetworkRPCRegistry::GetRPCCount() const {
        std::scoped_lock lock(m_Mutex);
        return static_cast<uint32_t>(m_RpcsByHash.size());
    }

    uint64_t NetworkRPCRegistry::GetContractHash() const {
        std::scoped_lock lock(m_Mutex);
        return m_ContractHash;
    }

    void NetworkRPCRegistry::Clear() {
        std::scoped_lock lock(m_Mutex);
        m_RpcsByHash.clear();
        m_NameToHash.clear();
        m_ContractHash = 0;
        m_DispatchSnapshot.reset();
        SetRPCContractHash(0);
        NetworkDiagnosticsState::Get().SetRegisteredRPCContracts(0);
    }

    bool NetworkRPCRegistry::ValidateRequest(
        const NetworkRPCRegistrationRequest& request,
        std::string& errorMessage) const {
        if (request.RPCName.empty()) {
            errorMessage = "rpcName is required.";
            return false;
        }
        if (request.RPCName.size() > MAX_RPC_NAME_LENGTH) {
            errorMessage = "rpcName exceeds maximum length.";
            return false;
        }
        if (request.PayloadSchemaHash.empty()) {
            errorMessage = "payloadSchemaHash is required.";
            return false;
        }
        if (request.PayloadSchemaHash.size() > MAX_SCHEMA_HASH_LENGTH) {
            errorMessage = "payloadSchemaHash exceeds maximum length.";
            return false;
        }
        if (request.ReplayAllowed && request.ReliabilityClass == ReplicationReliabilityClass::Unreliable) {
            errorMessage = "replayAllowed RPCs must use reliable transport.";
            return false;
        }
        if (request.ReplayAllowed && !request.RequiresAuth) {
            errorMessage = "replayAllowed RPCs must require auth.";
            return false;
        }
        return true;
    }

    uint64_t NetworkRPCRegistry::ComputeDescriptorHash(
        const NetworkRPCRegistrationRequest& request,
        uint32_t rpcNameHash) const {
        uint64_t hash = NETWORK_FNV_OFFSET_BASIS;
        hash = HashValueFNV1a(rpcNameHash, hash);
        hash = HashStringFNV1a(NormalizeToken(request.PayloadSchemaHash), false) ^ HashCombineFNV1a(hash, 0x11ULL);
        hash = HashValueFNV1a(static_cast<uint8_t>(request.TargetEntityScope), HashCombineFNV1a(hash, 0x12ULL));
        hash = HashValueFNV1a(static_cast<uint8_t>(request.ReliabilityClass), HashCombineFNV1a(hash, 0x13ULL));
        hash = HashValueFNV1a(request.RequiresAuth, HashCombineFNV1a(hash, 0x14ULL));
        hash = HashValueFNV1a(request.ReplayAllowed, HashCombineFNV1a(hash, 0x15ULL));
        return hash;
    }

    void NetworkRPCRegistry::RebuildContractHashLocked() {
        std::vector<uint32_t> sortedHashes;
        sortedHashes.reserve(m_RpcsByHash.size());
        for (const auto& [rpcHash, descriptor] : m_RpcsByHash) {
            (void)descriptor;
            sortedHashes.push_back(rpcHash);
        }
        std::sort(sortedHashes.begin(), sortedHashes.end());

        uint64_t hash = 0;
        for (uint32_t rpcHash : sortedHashes) {
            const auto it = m_RpcsByHash.find(rpcHash);
            if (it != m_RpcsByHash.end()) {
                hash = HashCombineFNV1a(hash, it->second.ContractHash);
            }
        }
        m_ContractHash = hash;
    }

    void NetworkRPCRegistry::RebuildDispatchSnapshotLocked() {
        m_DispatchSnapshot = std::make_shared<const std::unordered_map<uint32_t, NetworkRPCDescriptor>>(m_RpcsByHash);
    }

} // namespace Network
} // namespace Core

