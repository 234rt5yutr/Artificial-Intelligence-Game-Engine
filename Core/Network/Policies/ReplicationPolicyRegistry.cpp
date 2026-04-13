#include "Core/Network/Policies/ReplicationPolicyRegistry.h"

#include "Core/Log.h"
#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"
#include "Core/Network/NetworkContractState.h"
#include "Core/Network/NetworkHash.h"

#include <algorithm>
#include <string_view>

namespace Core {
namespace Network {

    namespace {

        constexpr float MIN_POLICY_SEND_RATE = 1.0f;
        constexpr float MAX_POLICY_SEND_RATE = 240.0f;

        std::string NormalizeToken(std::string_view token) {
            std::string normalized(token);
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return normalized;
        }

    } // namespace

    ReplicationPolicyRegistry& ReplicationPolicyRegistry::Get() {
        static ReplicationPolicyRegistry instance;
        return instance;
    }

    ReplicationPolicyRegistrationResult ReplicationPolicyRegistry::RegisterReplicatedPropertyPolicy(
        const ReplicatedPropertyPolicyRegistrationRequest& request) {
        std::scoped_lock lock(m_Mutex);

        std::string validationError;
        if (!ValidateRequest(request, validationError)) {
            ReplicationPolicyRegistrationResult result;
            result.Success = false;
            result.ErrorCode = NET_REPLICATION_POLICY_CONFLICT;
            result.Message = validationError;
            return result;
        }

        const std::string targetKey = BuildTargetKey(request.TargetComponent, request.TargetProperty);
        const uint64_t policyHash = ComputePolicyHash(request);

        auto existingTargetIt = m_TargetToPolicyId.find(targetKey);
        if (existingTargetIt != m_TargetToPolicyId.end() && existingTargetIt->second != request.PolicyId) {
            ReplicationPolicyRegistrationResult result;
            result.Success = false;
            result.ErrorCode = NET_REPLICATION_POLICY_CONFLICT;
            result.Message = "Policy target already registered by policyId '" + existingTargetIt->second + "'.";
            return result;
        }

        bool replacedExisting = false;
        auto existingPolicyIt = m_PoliciesById.find(request.PolicyId);
        if (existingPolicyIt != m_PoliciesById.end()) {
            const std::string existingTarget = BuildTargetKey(
                existingPolicyIt->second.TargetComponent,
                existingPolicyIt->second.TargetProperty);
            if (existingTarget != targetKey) {
                ReplicationPolicyRegistrationResult result;
                result.Success = false;
                result.ErrorCode = NET_REPLICATION_POLICY_CONFLICT;
                result.Message = "Policy ID '" + request.PolicyId + "' is already bound to a different target.";
                return result;
            }
            replacedExisting = true;
        }

        ReplicatedPropertyPolicy policy;
        policy.PolicyId = request.PolicyId;
        policy.TargetComponent = request.TargetComponent;
        policy.TargetProperty = request.TargetProperty;
        policy.SendRateHz = request.SendRateHz;
        policy.RelevanceClass = request.RelevanceClass;
        policy.QuantizationProfile = request.QuantizationProfile;
        policy.ReliabilityClass = request.ReliabilityClass;
        policy.PolicyHash = policyHash;

        m_PoliciesById[request.PolicyId] = policy;
        m_TargetToPolicyId[targetKey] = request.PolicyId;

        RebuildContractHashLocked();
        SetReplicationPolicyContractHash(m_ContractHash);

        NetworkDiagnosticsState& diagnostics = NetworkDiagnosticsState::Get();
        diagnostics.SetRegisteredReplicationPolicies(static_cast<uint32_t>(m_PoliciesById.size()));
        diagnostics.SetContractHash(GetNetworkContractHash());
        diagnostics.RecordEvent("ReplicationPolicyRegistered: " + request.PolicyId);

        ReplicationPolicyRegistrationResult result;
        result.Success = true;
        result.Message = replacedExisting
            ? "Replication policy updated."
            : "Replication policy registered.";
        result.ReplacedExisting = replacedExisting;
        result.PolicyHash = policyHash;
        return result;
    }

    std::optional<ReplicatedPropertyPolicy> ReplicationPolicyRegistry::FindPolicy(
        const std::string& targetComponent,
        const std::string& targetProperty) const {
        std::scoped_lock lock(m_Mutex);

        const std::string targetKey = BuildTargetKey(targetComponent, targetProperty);
        auto targetIt = m_TargetToPolicyId.find(targetKey);
        if (targetIt == m_TargetToPolicyId.end()) {
            return std::nullopt;
        }

        auto policyIt = m_PoliciesById.find(targetIt->second);
        if (policyIt == m_PoliciesById.end()) {
            return std::nullopt;
        }

        return policyIt->second;
    }

    std::vector<ReplicatedPropertyPolicy> ReplicationPolicyRegistry::GetRegisteredPolicies() const {
        std::scoped_lock lock(m_Mutex);

        std::vector<ReplicatedPropertyPolicy> policies;
        policies.reserve(m_PoliciesById.size());
        for (const auto& [policyId, policy] : m_PoliciesById) {
            (void)policyId;
            policies.push_back(policy);
        }

        std::sort(policies.begin(), policies.end(), [](const ReplicatedPropertyPolicy& left, const ReplicatedPropertyPolicy& right) {
            return left.PolicyId < right.PolicyId;
        });
        return policies;
    }

    uint32_t ReplicationPolicyRegistry::GetPolicyCount() const {
        std::scoped_lock lock(m_Mutex);
        return static_cast<uint32_t>(m_PoliciesById.size());
    }

    uint64_t ReplicationPolicyRegistry::GetContractHash() const {
        std::scoped_lock lock(m_Mutex);
        return m_ContractHash;
    }

    void ReplicationPolicyRegistry::Clear() {
        std::scoped_lock lock(m_Mutex);
        m_PoliciesById.clear();
        m_TargetToPolicyId.clear();
        m_ContractHash = 0;
        SetReplicationPolicyContractHash(0);
        NetworkDiagnosticsState::Get().SetRegisteredReplicationPolicies(0);
    }

    bool ReplicationPolicyRegistry::ValidateRequest(
        const ReplicatedPropertyPolicyRegistrationRequest& request,
        std::string& errorMessage) const {
        if (request.PolicyId.empty()) {
            errorMessage = "policyId is required.";
            return false;
        }
        if (request.TargetComponent.empty()) {
            errorMessage = "targetComponent is required.";
            return false;
        }
        if (request.TargetProperty.empty()) {
            errorMessage = "targetProperty is required.";
            return false;
        }
        if (request.SendRateHz < MIN_POLICY_SEND_RATE || request.SendRateHz > MAX_POLICY_SEND_RATE) {
            errorMessage = "sendRateHz must be between 1 and 240.";
            return false;
        }
        return true;
    }

    std::string ReplicationPolicyRegistry::BuildTargetKey(
        const std::string& targetComponent,
        const std::string& targetProperty) const {
        return NormalizeToken(targetComponent) + "::" + NormalizeToken(targetProperty);
    }

    uint64_t ReplicationPolicyRegistry::ComputePolicyHash(const ReplicatedPropertyPolicyRegistrationRequest& request) const {
        uint64_t hash = NETWORK_FNV_OFFSET_BASIS;

        const std::string normalizedId = NormalizeToken(request.PolicyId);
        const std::string normalizedComponent = NormalizeToken(request.TargetComponent);
        const std::string normalizedProperty = NormalizeToken(request.TargetProperty);

        hash = HashStringFNV1a(normalizedId, false);
        hash = HashStringFNV1a(normalizedComponent, false) ^ HashCombineFNV1a(hash, 0x01ULL);
        hash = HashStringFNV1a(normalizedProperty, false) ^ HashCombineFNV1a(hash, 0x02ULL);
        hash = HashValueFNV1a(request.SendRateHz, HashCombineFNV1a(hash, 0x03ULL));
        hash = HashValueFNV1a(static_cast<uint8_t>(request.RelevanceClass), HashCombineFNV1a(hash, 0x04ULL));
        hash = HashValueFNV1a(static_cast<uint8_t>(request.QuantizationProfile), HashCombineFNV1a(hash, 0x05ULL));
        hash = HashValueFNV1a(static_cast<uint8_t>(request.ReliabilityClass), HashCombineFNV1a(hash, 0x06ULL));

        return hash;
    }

    void ReplicationPolicyRegistry::RebuildContractHashLocked() {
        std::vector<std::string> sortedPolicyIds;
        sortedPolicyIds.reserve(m_PoliciesById.size());
        for (const auto& [policyId, policy] : m_PoliciesById) {
            (void)policy;
            sortedPolicyIds.push_back(policyId);
        }
        std::sort(sortedPolicyIds.begin(), sortedPolicyIds.end());

        uint64_t hash = 0;
        for (const std::string& policyId : sortedPolicyIds) {
            const auto policyIt = m_PoliciesById.find(policyId);
            if (policyIt != m_PoliciesById.end()) {
                hash = HashCombineFNV1a(hash, policyIt->second.PolicyHash);
            }
        }

        m_ContractHash = hash;
    }

} // namespace Network
} // namespace Core

