#include "ZPrepass.h"
#include "Core/Log.h"

namespace Core {
namespace Renderer {

    void ZPrepass::Initialize(std::shared_ptr<RHI::RHIDevice> device, uint32_t width, uint32_t height) {
        ENGINE_CORE_INFO("Initializing Z-Prepass...");
        CreateResources(device, width, height);
    }

    void ZPrepass::Shutdown() {
        ENGINE_CORE_INFO("Shutting down Z-Prepass...");
        m_RenderPass.reset();
        m_DepthTexture.reset();
    }

    void ZPrepass::Resize(std::shared_ptr<RHI::RHIDevice> device, uint32_t width, uint32_t height) {
        // Recreate resources on resize
        m_RenderPass.reset();
        m_DepthTexture.reset();
        CreateResources(device, width, height);
    }

    void ZPrepass::CreateResources(std::shared_ptr<RHI::RHIDevice> device, uint32_t width, uint32_t height) {
        // 1. Create Depth Texture
        RHI::TextureDescriptor depthDesc{};
        depthDesc.dimension = RHI::TextureDimension::Texture2D;
        depthDesc.width = width;
        depthDesc.height = height;
        depthDesc.depth = 1;
        depthDesc.mipLevels = 1;
        depthDesc.arrayLayers = 1;
        depthDesc.format = RHI::TextureFormat::D32_SFLOAT;
        depthDesc.usage = RHI::TextureUsage::DepthStencil;

        m_DepthTexture = device->CreateTexture(depthDesc);

        // 2. Create Render Pass
        RHI::RenderPassDescriptor passDesc{};
        passDesc.name = "Z-Prepass";
        passDesc.width = width;
        passDesc.height = height;
        passDesc.hasDepthStencil = true;
        
        passDesc.depthStencilAttachment.texture = m_DepthTexture;
        passDesc.depthStencilAttachment.depthLoadOp = RHI::RenderPassLoadOp::Clear;
        passDesc.depthStencilAttachment.depthStoreOp = RHI::RenderPassStoreOp::Store;
        // Don't care about stencil for simple Z-Prepass
        passDesc.depthStencilAttachment.stencilLoadOp = RHI::RenderPassLoadOp::DontCare;
        passDesc.depthStencilAttachment.stencilStoreOp = RHI::RenderPassStoreOp::DontCare;
        passDesc.depthStencilAttachment.clearDepth = 1.0f;
        passDesc.depthStencilAttachment.clearStencil = 0;

        m_RenderPass = device->CreateRenderPass(passDesc);
    }

    void ZPrepass::BeginPass(std::shared_ptr<RHI::RHICommandList> commandList) {
        if (m_RenderPass && commandList) {
            commandList->BeginRenderPass(m_RenderPass);
            // Engine will execute pipeline binding and draw calls here
        }
    }

    void ZPrepass::EndPass(std::shared_ptr<RHI::RHICommandList> commandList) {
        if (m_RenderPass && commandList) {
            commandList->EndRenderPass();
        }
    }

} // namespace Renderer
} // namespace Core