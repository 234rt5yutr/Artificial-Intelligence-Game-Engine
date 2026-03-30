#include "ShadowPass.h"
#include "Core/Log.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Core {
namespace Renderer {

    void ShadowPass::Initialize(std::shared_ptr<RHI::RHIDevice> device, uint32_t resolution) {
        m_Device = device;
        m_Resolution = resolution;

        // 1. Create Shadow Map Texture
        RHI::TextureDescriptor texDesc;
        texDesc.dimension = RHI::TextureDimension::Texture2D;
        texDesc.format = RHI::TextureFormat::D32_SFLOAT;
        texDesc.width = resolution;
        texDesc.height = resolution;
        texDesc.depth = 1;
        texDesc.mipLevels = 1;
        texDesc.arrayLayers = 1;
        texDesc.usage = RHI::TextureUsage::DepthStencil;
        
        m_ShadowMapTexture = m_Device->CreateTexture(texDesc);

        // 2. Create Render Pass
        RHI::RenderPassDescriptor passDesc;
        passDesc.name = "ShadowPass";
        passDesc.width = m_Resolution;
        passDesc.height = m_Resolution;
        passDesc.hasDepthStencil = true;
        
        passDesc.depthStencilAttachment.texture = m_ShadowMapTexture;
        passDesc.depthStencilAttachment.depthLoadOp = RHI::RenderPassLoadOp::Clear;
        passDesc.depthStencilAttachment.depthStoreOp = RHI::RenderPassStoreOp::Store;
        passDesc.depthStencilAttachment.stencilLoadOp = RHI::RenderPassLoadOp::DontCare;
        passDesc.depthStencilAttachment.stencilStoreOp = RHI::RenderPassStoreOp::DontCare;
        passDesc.depthStencilAttachment.clearDepth = 1.0f;
        passDesc.depthStencilAttachment.clearStencil = 0;
        
        m_ShadowRenderPass = m_Device->CreateRenderPass(passDesc);

        // 3. Create Uniform Buffer for Light Space Matrix
        RHI::BufferDescriptor bufferDesc;
        bufferDesc.size = sizeof(glm::mat4);
        bufferDesc.usage = RHI::BufferUsage::Uniform;
        bufferDesc.mapped = true;
        m_LightSpaceBuffer = m_Device->CreateBuffer(bufferDesc);

        ENGINE_CORE_INFO("ShadowPass Initialized with resolution: {0}x{0}", resolution);
    }

    void ShadowPass::Shutdown() {
        m_LightSpaceBuffer.reset();
        m_ShadowPipeline.reset();
        m_ShadowRenderPass.reset();
        m_ShadowMapTexture.reset();
        m_Device.reset();
    }

    void ShadowPass::UpdateLightMatrix(const DirectionalLight& light, const glm::vec3& cameraPos, const glm::vec3& cameraDir) {
        // Orthographic projection bounds for the directional light
        float orthoSize = 25.0f;
        float nearPlane = -15.0f;
        float farPlane = 35.0f;

        glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, nearPlane, farPlane);
        
        // Compute a stable target slightly ahead of the camera
        glm::vec3 target = cameraPos + cameraDir * 10.0f;
        glm::vec3 lightPos = target - glm::normalize(light.direction) * 15.0f;

        // Up vector formulation
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(glm::normalize(light.direction), up)) > 0.999f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        glm::mat4 lightView = glm::lookAt(lightPos, target, up);

        m_LightSpaceMatrix = lightProjection * lightView;

        // Update the buffer
        if (m_LightSpaceBuffer) {
            void* data = nullptr;
            m_LightSpaceBuffer->Map(&data);
            if (data) {
                std::memcpy(data, &m_LightSpaceMatrix, sizeof(glm::mat4));
                m_LightSpaceBuffer->Unmap();
            }
        }
    }

} // namespace Renderer
} // namespace Core