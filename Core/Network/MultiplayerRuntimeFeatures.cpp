#include "Core/Network/MultiplayerRuntimeFeatures.h"

namespace Core {
namespace Network {

    MultiplayerRuntimeFeatures& MultiplayerRuntimeFeatures::Get() {
        static MultiplayerRuntimeFeatures instance;
        return instance;
    }

    MultiplayerRuntimeFeatureGates MultiplayerRuntimeFeatures::GetFeatureGates() const {
        std::scoped_lock lock(m_Mutex);
        return m_Gates;
    }

    void MultiplayerRuntimeFeatures::SetFeatureGates(const MultiplayerRuntimeFeatureGates& gates) {
        std::scoped_lock lock(m_Mutex);
        m_Gates = gates;
    }

} // namespace Network
} // namespace Core

