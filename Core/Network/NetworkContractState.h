#pragma once

#include <cstdint>

namespace Core {
namespace Network {

    void SetReplicationPolicyContractHash(uint64_t hash);
    void SetRPCContractHash(uint64_t hash);

    uint64_t GetReplicationPolicyContractHash();
    uint64_t GetRPCContractHash();
    uint64_t GetNetworkContractHash();

    void SetBackwardsCompatibilityMode(bool enabled);
    bool IsBackwardsCompatibilityMode();

} // namespace Network
} // namespace Core

