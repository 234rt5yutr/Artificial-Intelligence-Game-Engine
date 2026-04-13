#pragma once

#include "Core/Math/Math.h"
#include "Core/RHI/RHIRenderPass.h"
#include "Core/RHI/RHIDevice.h"
#include "Core/RHI/RHIPipelineState.h"
#include "Core/RHI/RHIBuffer.h"
#include <cstdint>
#include <memory>
#include <string>

namespace Core {
namespace Renderer {

    struct DirectionalLight {
        glm::vec3 direction;
        glm::vec3 color;
        float intensity;
    };

    struct VirtualShadowFallbackDecision {
        bool useVirtualShadowCache = false;
        bool forceRasterShadowMap = false;
        uint64_t cacheKey = 0;
        std::string reason;
    };

    class ShadowPass {
    public:
        ShadowPass() = default;
        ~ShadowPass() = default;

        void Initialize(std::shared_ptr<RHI::RHIDevice> device, uint32_t resolution = 2048);
        void Shutdown();

        // Update the light space matrix for a directional light
        void UpdateLightMatrix(const DirectionalLight& light, const glm::vec3& cameraPos, const glm::vec3& cameraDir);

        std::shared_ptr<RHI::RHIRenderPass> GetRenderPass() const { return m_ShadowRenderPass; }
        std::shared_ptr<RHI::RHITexture> GetShadowMap() const { return m_ShadowMapTexture; }
        const glm::mat4& GetLightSpaceMatrix() const { return m_LightSpaceMatrix; }
        void SetVirtualShadowFallbackDecision(const VirtualShadowFallbackDecision& decision);
        const VirtualShadowFallbackDecision& GetVirtualShadowFallbackDecision() const { return m_VirtualShadowFallback; }
        bool ShouldRenderRasterShadowMap() const;

    private:
        std::shared_ptr<RHI::RHIDevice> m_Device;
        std::shared_ptr<RHI::RHITexture> m_ShadowMapTexture;
        std::shared_ptr<RHI::RHIRenderPass> m_ShadowRenderPass;
        std::shared_ptr<RHI::RHIPipelineState> m_ShadowPipeline;
        std::shared_ptr<RHI::RHIBuffer> m_LightSpaceBuffer;

        glm::mat4 m_LightSpaceMatrix{1.0f};
        uint32_t m_Resolution = 2048;
        VirtualShadowFallbackDecision m_VirtualShadowFallback{};
    };

} // namespace Renderer
} // namespace Core
