#pragma once

#include "Core/Network/RPC/NetworkRPCTypes.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Network {

    class NetworkRPCRegistry {
    public:
        static NetworkRPCRegistry& Get();

        NetworkRPCRegistrationResult RegisterNetworkRPC(const NetworkRPCRegistrationRequest& request);
        NetworkRPCValidationResult ValidateInvocation(
            uint32_t rpcNameHash,
            bool callerAuthenticated,
            bool replayContext) const;

        std::optional<NetworkRPCDescriptor> FindByHash(uint32_t rpcNameHash) const;
        std::vector<NetworkRPCDescriptor> GetRegisteredRPCs() const;
        uint32_t GetRPCCount() const;
        uint64_t GetContractHash() const;
        void Clear();

    private:
        bool ValidateRequest(const NetworkRPCRegistrationRequest& request, std::string& errorMessage) const;
        uint64_t ComputeDescriptorHash(const NetworkRPCRegistrationRequest& request, uint32_t rpcNameHash) const;
        void RebuildContractHashLocked();
        void RebuildDispatchSnapshotLocked();

        mutable std::mutex m_Mutex;
        std::unordered_map<uint32_t, NetworkRPCDescriptor> m_RpcsByHash;
        std::unordered_map<std::string, uint32_t> m_NameToHash;
        uint64_t m_ContractHash = 0;
        std::shared_ptr<const std::unordered_map<uint32_t, NetworkRPCDescriptor>> m_DispatchSnapshot;
    };

} // namespace Network
} // namespace Core

