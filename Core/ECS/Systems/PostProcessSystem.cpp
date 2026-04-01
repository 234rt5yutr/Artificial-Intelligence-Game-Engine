#include "PostProcessSystem.h"
#include "Core/ECS/Components/PostProcessComponent.h"
#include <cmath>
#include <algorithm>

namespace Core {
namespace ECS {

    void PostProcessSystem::Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                       VkExtent2D extent) {
        m_Manager.Initialize(device, physicalDevice, extent);
    }

    void PostProcessSystem::Update(entt::registry& registry, float deltaTime) {
        UpdateBlending(registry, deltaTime);
    }

    void PostProcessSystem::InterpolateSettings(PostProcessSettings& src,
                                                const PostProcessSettings& dst,
                                                float t) {
        // Clamp t to [0, 1]
        t = std::clamp(t, 0.0f, 1.0f);

        // Bloom
        src.bloomIntensity = src.bloomIntensity + t * (dst.bloomIntensity - src.bloomIntensity);
        src.bloomThreshold = src.bloomThreshold + t * (dst.bloomThreshold - src.bloomThreshold);
        src.bloomSoftKnee = src.bloomSoftKnee + t * (dst.bloomSoftKnee - src.bloomSoftKnee);
        src.bloomScatter = src.bloomScatter + t * (dst.bloomScatter - src.bloomScatter);

        // SSAO
        src.ssaoRadius = src.ssaoRadius + t * (dst.ssaoRadius - src.ssaoRadius);
        src.ssaoIntensity = src.ssaoIntensity + t * (dst.ssaoIntensity - src.ssaoIntensity);
        src.ssaoBias = src.ssaoBias + t * (dst.ssaoBias - src.ssaoBias);

        // Depth of Field
        src.dofFocalDistance = src.dofFocalDistance + t * (dst.dofFocalDistance - src.dofFocalDistance);
        src.dofFocalRange = src.dofFocalRange + t * (dst.dofFocalRange - src.dofFocalRange);
        src.dofMaxBlur = src.dofMaxBlur + t * (dst.dofMaxBlur - src.dofMaxBlur);

        // Motion Blur
        src.motionBlurScale = src.motionBlurScale + t * (dst.motionBlurScale - src.motionBlurScale);

        // Color Grading
        src.exposure = src.exposure + t * (dst.exposure - src.exposure);
        src.contrast = src.contrast + t * (dst.contrast - src.contrast);
        src.saturation = src.saturation + t * (dst.saturation - src.saturation);
        src.temperature = src.temperature + t * (dst.temperature - src.temperature);
        src.tint = src.tint + t * (dst.tint - src.tint);

        // Color filter (Vec3 interpolation)
        src.colorFilter.x = src.colorFilter.x + t * (dst.colorFilter.x - src.colorFilter.x);
        src.colorFilter.y = src.colorFilter.y + t * (dst.colorFilter.y - src.colorFilter.y);
        src.colorFilter.z = src.colorFilter.z + t * (dst.colorFilter.z - src.colorFilter.z);

        // Vignette
        src.vignetteIntensity = src.vignetteIntensity + t * (dst.vignetteIntensity - src.vignetteIntensity);
        src.vignetteSmoothness = src.vignetteSmoothness + t * (dst.vignetteSmoothness - src.vignetteSmoothness);
    }

    void PostProcessSystem::UpdateBlending(entt::registry& registry, float deltaTime) {
        auto view = registry.view<PostProcessComponent>();

        for (auto entity : view) {
            auto& pp = view.get<PostProcessComponent>(entity);

            if (pp.isBlending && pp.blendDuration > 0.0f) {
                pp.blendProgress += deltaTime / pp.blendDuration;

                if (pp.blendProgress >= 1.0f) {
                    pp.blendProgress = 1.0f;
                    pp.settings = pp.targetSettings;
                    pp.isBlending = false;
                } else {
                    // Calculate interpolation factor for this frame
                    // Using smooth step for smoother transitions
                    float t = pp.blendProgress;
                    float smoothT = t * t * (3.0f - 2.0f * t);  // Smooth step

                    InterpolateSettings(pp.settings, pp.targetSettings, smoothT);
                }
            }
        }
    }

    void PostProcessSystem::Render(VkCommandBuffer cmd, entt::registry& registry,
                                   VkImageView sceneColor, VkImageView sceneDepth) {
        // Default settings if no PostProcessComponent exists
        PostProcessSettings defaultSettings;
        const PostProcessSettings* activeSettings = &defaultSettings;

        // Find active post-process component (typically attached to camera entity)
        auto view = registry.view<PostProcessComponent>();
        if (!view.empty()) {
            auto entity = view.front();
            activeSettings = &view.get<PostProcessComponent>(entity).settings;
        }

        m_Manager.Execute(cmd, *activeSettings, sceneColor, sceneDepth);
    }

    void PostProcessSystem::Resize(VkExtent2D newExtent) {
        m_Manager.Resize(newExtent);
    }

    void PostProcessSystem::Cleanup() {
        m_Manager.Cleanup();
    }

} // namespace ECS
} // namespace Core
