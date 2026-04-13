#pragma once

#include <steam/steamnetworkingsockets.h>

#include <cstdint>
#include <optional>
#include <string>

namespace Core {
namespace Network {

    constexpr const char* NET_REPLICATION_POLICY_CONFLICT = "NET_REPLICATION_POLICY_CONFLICT";

    enum class ReplicationRelevanceClass : uint8_t {
        Global = 0,
        Nearby,
        OwnerOnly,
        TeamOnly
    };

    enum class ReplicationQuantizationProfile : uint8_t {
        None = 0,
        Coarse,
        Normal,
        High
    };

    enum class ReplicationReliabilityClass : uint8_t {
        Unreliable = 0,
        Reliable,
        ReliableOrdered
    };

    struct ReplicatedPropertyPolicy {
        std::string PolicyId;
        std::string TargetComponent;
        std::string TargetProperty;
        float SendRateHz = 20.0f;
        ReplicationRelevanceClass RelevanceClass = ReplicationRelevanceClass::Global;
        ReplicationQuantizationProfile QuantizationProfile = ReplicationQuantizationProfile::Normal;
        ReplicationReliabilityClass ReliabilityClass = ReplicationReliabilityClass::Unreliable;
        uint64_t PolicyHash = 0;
    };

    struct ReplicatedPropertyPolicyRegistrationRequest {
        std::string PolicyId;
        std::string TargetComponent;
        std::string TargetProperty;
        float SendRateHz = 20.0f;
        ReplicationRelevanceClass RelevanceClass = ReplicationRelevanceClass::Global;
        ReplicationQuantizationProfile QuantizationProfile = ReplicationQuantizationProfile::Normal;
        ReplicationReliabilityClass ReliabilityClass = ReplicationReliabilityClass::Unreliable;
    };

    struct ReplicationPolicyRegistrationResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        bool ReplacedExisting = false;
        uint64_t PolicyHash = 0;
    };

    inline const char* ToString(ReplicationRelevanceClass value) {
        switch (value) {
            case ReplicationRelevanceClass::Global: return "global";
            case ReplicationRelevanceClass::Nearby: return "nearby";
            case ReplicationRelevanceClass::OwnerOnly: return "owner-only";
            case ReplicationRelevanceClass::TeamOnly: return "team-only";
            default: return "global";
        }
    }

    inline const char* ToString(ReplicationQuantizationProfile value) {
        switch (value) {
            case ReplicationQuantizationProfile::None: return "none";
            case ReplicationQuantizationProfile::Coarse: return "coarse";
            case ReplicationQuantizationProfile::Normal: return "normal";
            case ReplicationQuantizationProfile::High: return "high";
            default: return "normal";
        }
    }

    inline const char* ToString(ReplicationReliabilityClass value) {
        switch (value) {
            case ReplicationReliabilityClass::Unreliable: return "unreliable";
            case ReplicationReliabilityClass::Reliable: return "reliable";
            case ReplicationReliabilityClass::ReliableOrdered: return "reliable-ordered";
            default: return "unreliable";
        }
    }

    inline int ToSteamSendFlags(ReplicationReliabilityClass value) {
        switch (value) {
            case ReplicationReliabilityClass::Unreliable:
                return k_nSteamNetworkingSend_UnreliableNoDelay;
            case ReplicationReliabilityClass::Reliable:
            case ReplicationReliabilityClass::ReliableOrdered:
                return k_nSteamNetworkingSend_Reliable;
            default:
                return k_nSteamNetworkingSend_UnreliableNoDelay;
        }
    }

    inline std::optional<ReplicationRelevanceClass> ParseReplicationRelevanceClass(const std::string& value) {
        if (value == "global") return ReplicationRelevanceClass::Global;
        if (value == "nearby") return ReplicationRelevanceClass::Nearby;
        if (value == "owner-only") return ReplicationRelevanceClass::OwnerOnly;
        if (value == "team-only") return ReplicationRelevanceClass::TeamOnly;
        return std::nullopt;
    }

    inline std::optional<ReplicationQuantizationProfile> ParseReplicationQuantizationProfile(const std::string& value) {
        if (value == "none") return ReplicationQuantizationProfile::None;
        if (value == "coarse") return ReplicationQuantizationProfile::Coarse;
        if (value == "normal") return ReplicationQuantizationProfile::Normal;
        if (value == "high") return ReplicationQuantizationProfile::High;
        return std::nullopt;
    }

    inline std::optional<ReplicationReliabilityClass> ParseReplicationReliabilityClass(const std::string& value) {
        if (value == "unreliable") return ReplicationReliabilityClass::Unreliable;
        if (value == "reliable") return ReplicationReliabilityClass::Reliable;
        if (value == "reliable-ordered") return ReplicationReliabilityClass::ReliableOrdered;
        return std::nullopt;
    }

} // namespace Network
} // namespace Core

