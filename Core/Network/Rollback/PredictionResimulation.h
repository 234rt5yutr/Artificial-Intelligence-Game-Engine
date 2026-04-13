#pragma once

#include "Core/Network/Rollback/RollbackTypes.h"

namespace Core {
namespace Network {

    class PredictionResimulationService {
    public:
        static PredictionResimulationService& Get();

        ResimulationResult ResimulatePredictedFrames(const ResimulationRequest& request);
    };

} // namespace Network
} // namespace Core

