#include "Core/Animation/Blend/BlendTreeEvaluationCache.h"

#include <cmath>

namespace Core::Animation {

int32_t BlendTreeEvaluationCache::Quantize(const float value, const float step) {
    const float safeStep = step > 0.0f ? step : 0.01f;
    return static_cast<int32_t>(std::llround(value / safeStep));
}

bool BlendTreeEvaluationCache::HasReusableEvaluation(
    const uint64_t entityId,
    const std::string& treeId,
    const float parameterX,
    const float parameterY,
    const float quantizationStep) const {
    const auto it = m_Entries.find(entityId);
    if (it == m_Entries.end()) {
        return false;
    }

    const CacheEntry& entry = it->second;
    return entry.TreeId == treeId &&
           entry.QuantizedX == Quantize(parameterX, quantizationStep) &&
           entry.QuantizedY == Quantize(parameterY, quantizationStep);
}

void BlendTreeEvaluationCache::MarkEvaluated(
    const uint64_t entityId,
    const std::string& treeId,
    const float parameterX,
    const float parameterY,
    const float quantizationStep) {
    CacheEntry& entry = m_Entries[entityId];
    entry.TreeId = treeId;
    entry.QuantizedX = Quantize(parameterX, quantizationStep);
    entry.QuantizedY = Quantize(parameterY, quantizationStep);
    ++entry.HitCount;
}

void BlendTreeEvaluationCache::Clear() {
    m_Entries.clear();
}

} // namespace Core::Animation
