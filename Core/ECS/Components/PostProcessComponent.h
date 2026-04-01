#pragma once

#include <string>
#include "Core/Math/Math.h"

namespace Core {
namespace ECS {

    struct PostProcessSettings {
        // Bloom
        bool bloomEnabled = true;
        float bloomIntensity = 1.0f;
        float bloomThreshold = 1.0f;
        float bloomSoftKnee = 0.5f;
        float bloomScatter = 0.7f;
        int bloomMipLevels = 6;

        // SSAO
        bool ssaoEnabled = true;
        float ssaoRadius = 0.5f;
        float ssaoIntensity = 1.0f;
        float ssaoBias = 0.025f;
        int ssaoKernelSize = 16;
        int ssaoBlurPasses = 2;

        // Depth of Field
        bool dofEnabled = false;
        float dofFocalDistance = 10.0f;
        float dofFocalRange = 5.0f;
        float dofMaxBlur = 1.0f;

        // Motion Blur
        bool motionBlurEnabled = false;
        float motionBlurScale = 1.0f;
        int motionBlurSamples = 8;

        // Color Grading
        bool colorGradingEnabled = true;
        float exposure = 1.0f;
        float contrast = 1.0f;
        float saturation = 1.0f;
        Math::Vec3 colorFilter{ 1.0f, 1.0f, 1.0f };
        float temperature = 0.0f;   // -1 to 1 (cool to warm)
        float tint = 0.0f;          // -1 to 1 (green to magenta)
        std::string lutTexturePath = "";

        // Tonemapping (0=ACES, 1=Reinhard, 2=Uncharted2)
        int tonemapOperator = 0;

        // Vignette
        bool vignetteEnabled = false;
        float vignetteIntensity = 0.5f;
        float vignetteSmoothness = 0.3f;

        PostProcessSettings() = default;
    };

    struct PostProcessComponent {
        PostProcessSettings settings;

        // Profile blending (for smooth transitions)
        PostProcessSettings targetSettings;
        float blendProgress = 1.0f;
        float blendDuration = 0.0f;
        bool isBlending = false;

        PostProcessComponent() = default;

        // Start blending from current settings to target over duration seconds
        void StartBlend(const PostProcessSettings& target, float duration) {
            targetSettings = target;
            blendDuration = duration;
            blendProgress = 0.0f;
            isBlending = true;
        }

        // Instantly apply new settings without blending
        void ApplySettings(const PostProcessSettings& newSettings) {
            settings = newSettings;
            isBlending = false;
            blendProgress = 1.0f;
        }
    };

} // namespace ECS
} // namespace Core
