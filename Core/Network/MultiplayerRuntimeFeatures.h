#pragma once

#include <mutex>

namespace Core {
namespace Network {

    constexpr const char* NET_FEATURE_GATE_DISABLED = "NET_FEATURE_GATE_DISABLED";

    struct MultiplayerRuntimeFeatureGates {
        bool ReplayEnabled = true;
        bool RollbackEnabled = true;
        bool ResimulationEnabled = true;
        bool HostMigrationEnabled = true;
    };

    class MultiplayerRuntimeFeatures {
    public:
        static MultiplayerRuntimeFeatures& Get();

        MultiplayerRuntimeFeatureGates GetFeatureGates() const;
        void SetFeatureGates(const MultiplayerRuntimeFeatureGates& gates);

    private:
        mutable std::mutex m_Mutex;
        MultiplayerRuntimeFeatureGates m_Gates;
    };

} // namespace Network
} // namespace Core

