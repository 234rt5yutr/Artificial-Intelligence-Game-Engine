#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Core::Animation {

class BlendTreeEvaluationCache {
public:
    bool HasReusableEvaluation(
        uint64_t entityId,
        const std::string& treeId,
        float parameterX,
        float parameterY,
        float quantizationStep = 0.01f) const;

    void MarkEvaluated(
        uint64_t entityId,
        const std::string& treeId,
        float parameterX,
        float parameterY,
        float quantizationStep = 0.01f);

    void Clear();

private:
    struct CacheEntry {
        std::string TreeId;
        int32_t QuantizedX = 0;
        int32_t QuantizedY = 0;
        uint64_t HitCount = 0;
    };

    static int32_t Quantize(float value, float step);

    std::unordered_map<uint64_t, CacheEntry> m_Entries;
};

} // namespace Core::Animation
