#include "Core/Network/Rollback/PredictionResimulation.h"

#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"
#include "Core/Network/NetworkHash.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Core {
namespace Network {

    namespace {

        uint64_t HashFloat(uint64_t seed, float value) {
            uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(float));
            return HashCombineFNV1a(seed, bits);
        }

    } // namespace

    PredictionResimulationService& PredictionResimulationService::Get() {
        static PredictionResimulationService instance;
        return instance;
    }

    ResimulationResult PredictionResimulationService::ResimulatePredictedFrames(const ResimulationRequest& request) {
        ResimulationResult result;

        if (request.SessionId.empty()) {
            result.Success = false;
            result.ErrorCode = NET_RESIM_DIVERGENCE_EXCEEDED;
            result.Message = "sessionId is required for resimulation.";
            return result;
        }

        if (request.ToFrameTick < request.FromFrameTick) {
            result.Success = false;
            result.ErrorCode = NET_RESIM_DIVERGENCE_EXCEEDED;
            result.Message = "toFrameTick must be >= fromFrameTick.";
            return result;
        }

        const uint32_t requestedFrameCount = request.ToFrameTick - request.FromFrameTick + 1U;
        const uint32_t maxFramesPerUpdate = std::max(1U, request.MaxFramesPerUpdate);
        const uint32_t processedFrames = std::min(requestedFrameCount, maxFramesPerUpdate);
        const uint32_t pendingFrames = requestedFrameCount - processedFrames;

        std::vector<ResimulationInputRecord> relevantInputs;
        relevantInputs.reserve(request.BufferedInputs.size());
        for (const ResimulationInputRecord& input : request.BufferedInputs) {
            if (input.FrameTick >= request.FromFrameTick &&
                input.FrameTick < request.FromFrameTick + processedFrames) {
                relevantInputs.push_back(input);
            }
        }

        std::sort(relevantInputs.begin(), relevantInputs.end(), [](const ResimulationInputRecord& left, const ResimulationInputRecord& right) {
            if (left.FrameTick == right.FrameTick) {
                return left.InputSequence < right.InputSequence;
            }
            return left.FrameTick < right.FrameTick;
        });

        uint64_t predictedHash = HashStringFNV1a(request.SessionId, false);
        predictedHash = HashCombineFNV1a(predictedHash, request.FromFrameTick);
        predictedHash = HashCombineFNV1a(predictedHash, request.FromFrameTick + processedFrames - 1U);

        for (const ResimulationInputRecord& inputRecord : relevantInputs) {
            predictedHash = HashCombineFNV1a(predictedHash, inputRecord.FrameTick);
            predictedHash = HashCombineFNV1a(predictedHash, inputRecord.InputSequence);
            predictedHash = HashCombineFNV1a(predictedHash, inputRecord.Input.InputSequence);
            predictedHash = HashCombineFNV1a(predictedHash, inputRecord.Input.Buttons);
            predictedHash = HashFloat(predictedHash, inputRecord.Input.MoveX);
            predictedHash = HashFloat(predictedHash, inputRecord.Input.MoveY);
            predictedHash = HashFloat(predictedHash, inputRecord.Input.LookYaw);
            predictedHash = HashFloat(predictedHash, inputRecord.Input.LookPitch);
            predictedHash = HashCombineFNV1a(predictedHash, inputRecord.Input.DeltaTimeMs);
        }

        const uint64_t authoritativeHash = request.AuthoritativeStateHash.value_or(predictedHash);
        const uint64_t xorDistance = predictedHash ^ authoritativeHash;
        const float divergenceMagnitude = static_cast<float>(xorDistance % 1000000ULL) / 1000000.0f;
        const bool divergenceExceeded = request.AuthoritativeStateHash.has_value() &&
            divergenceMagnitude > std::max(0.0f, request.DivergenceThreshold);

        uint32_t hardCorrectionCount = 0;
        if (divergenceExceeded && request.EnableHardCorrectionOnDivergence) {
            hardCorrectionCount = 1;
        }

        NetworkDiagnosticsState::Get().SetPendingResimFrames(pendingFrames);
        NetworkDiagnosticsState::Get().SetLastResimulatedFrames(processedFrames);
        NetworkDiagnosticsState::Get().RecordResimulation(hardCorrectionCount);

        result.FramesResimulated = processedFrames;
        result.HardCorrectionCount = hardCorrectionCount;
        result.DivergenceMagnitude = divergenceMagnitude;
        result.PredictedStateHash = predictedHash;
        result.AuthoritativeStateHash = authoritativeHash;

        if (divergenceExceeded && !request.EnableHardCorrectionOnDivergence) {
            result.Success = false;
            result.ErrorCode = NET_RESIM_DIVERGENCE_EXCEEDED;
            result.Message = "Divergence threshold exceeded and hard correction path is disabled.";
            NetworkDiagnosticsState::Get().RecordEvent("ResimulateFailedDivergence: " + request.SessionId);
            return result;
        }

        if (divergenceExceeded) {
            result.Success = true;
            result.ErrorCode = NET_RESIM_DIVERGENCE_EXCEEDED;
            result.Message = "Divergence threshold exceeded; hard correction path applied.";
            NetworkDiagnosticsState::Get().RecordEvent("ResimulateHardCorrection: " + request.SessionId);
            return result;
        }

        result.Success = true;
        result.Message = pendingFrames > 0
            ? "Resimulation processed a bounded frame window."
            : "Resimulation completed for requested frame range.";
        NetworkDiagnosticsState::Get().RecordEvent("ResimulateCompleted: " + request.SessionId);
        return result;
    }

} // namespace Network
} // namespace Core

