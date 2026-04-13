#pragma once

#include "Core/Network/Policies/ReplicationPolicyTypes.h"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {
namespace Network {

    class ReplicationPolicyRegistry {
    public:
        static ReplicationPolicyRegistry& Get();

        ReplicationPolicyRegistrationResult RegisterReplicatedPropertyPolicy(
            const ReplicatedPropertyPolicyRegistrationRequest& request);

        std::optional<ReplicatedPropertyPolicy> FindPolicy(
            const std::string& targetComponent,
            const std::string& targetProperty) const;

        std::vector<ReplicatedPropertyPolicy> GetRegisteredPolicies() const;
        uint32_t GetPolicyCount() const;
        uint64_t GetContractHash() const;
        void Clear();

    private:
        bool ValidateRequest(const ReplicatedPropertyPolicyRegistrationRequest& request, std::string& errorMessage) const;
        std::string BuildTargetKey(const std::string& targetComponent, const std::string& targetProperty) const;
        uint64_t ComputePolicyHash(const ReplicatedPropertyPolicyRegistrationRequest& request) const;
        void RebuildContractHashLocked();

        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, ReplicatedPropertyPolicy> m_PoliciesById;
        std::unordered_map<std::string, std::string> m_TargetToPolicyId;
        uint64_t m_ContractHash = 0;
    };

} // namespace Network
} // namespace Core

