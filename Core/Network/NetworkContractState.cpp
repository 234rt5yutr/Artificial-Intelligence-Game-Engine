#include "Core/Network/NetworkContractState.h"
#include "Core/Network/NetworkHash.h"

#include <atomic>

namespace Core {
namespace Network {

    namespace {

        std::atomic<uint64_t> g_ReplicationPolicyHash{ 0 };
        std::atomic<uint64_t> g_RPCContractHash{ 0 };
        std::atomic<uint64_t> g_NetworkContractHash{ 0 };
        std::atomic<bool> g_BackwardsCompatibilityMode{ true };

        void RefreshCombinedContractHash() {
            const uint64_t replicationHash = g_ReplicationPolicyHash.load(std::memory_order_relaxed);
            const uint64_t rpcHash = g_RPCContractHash.load(std::memory_order_relaxed);
            const uint64_t combinedHash = HashCombineFNV1a(replicationHash, rpcHash);
            g_NetworkContractHash.store(combinedHash, std::memory_order_relaxed);
        }

    } // namespace

    void SetReplicationPolicyContractHash(uint64_t hash) {
        g_ReplicationPolicyHash.store(hash, std::memory_order_relaxed);
        RefreshCombinedContractHash();
    }

    void SetRPCContractHash(uint64_t hash) {
        g_RPCContractHash.store(hash, std::memory_order_relaxed);
        RefreshCombinedContractHash();
    }

    uint64_t GetReplicationPolicyContractHash() {
        return g_ReplicationPolicyHash.load(std::memory_order_relaxed);
    }

    uint64_t GetRPCContractHash() {
        return g_RPCContractHash.load(std::memory_order_relaxed);
    }

    uint64_t GetNetworkContractHash() {
        return g_NetworkContractHash.load(std::memory_order_relaxed);
    }

    void SetBackwardsCompatibilityMode(bool enabled) {
        g_BackwardsCompatibilityMode.store(enabled, std::memory_order_relaxed);
    }

    bool IsBackwardsCompatibilityMode() {
        return g_BackwardsCompatibilityMode.load(std::memory_order_relaxed);
    }

} // namespace Network
} // namespace Core

