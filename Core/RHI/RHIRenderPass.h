#pragma once

#include "RHITexture.h"
#include "RHICommandList.h"
#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace Core {
namespace RHI {

    enum class RenderPassLoadOp {
        Load,
        Clear,
        DontCare
    };

    enum class RenderPassStoreOp {
        Store,
        DontCare
    };

    struct RenderPassColorAttachment {
        std::shared_ptr<RHITexture> texture;
        RenderPassLoadOp loadOp = RenderPassLoadOp::Clear;
        RenderPassStoreOp storeOp = RenderPassStoreOp::Store;
        glm::vec4 clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    };

    struct RenderPassDepthStencilAttachment {
        std::shared_ptr<RHITexture> texture;
        RenderPassLoadOp depthLoadOp = RenderPassLoadOp::Clear;
        RenderPassStoreOp depthStoreOp = RenderPassStoreOp::Store;
        RenderPassLoadOp stencilLoadOp = RenderPassLoadOp::DontCare;
        RenderPassStoreOp stencilStoreOp = RenderPassStoreOp::DontCare;
        float clearDepth = 1.0f;
        uint32_t clearStencil = 0;
    };

    struct RenderPassDescriptor {
        std::string name;
        std::vector<RenderPassColorAttachment> colorAttachments;
        RenderPassDepthStencilAttachment depthStencilAttachment;
        bool hasDepthStencil = false;
        
        uint32_t width = 0;
        uint32_t height = 0;
    };

    class RHIRenderPass {
    public:
        virtual ~RHIRenderPass() = default;

        virtual const RenderPassDescriptor& GetDescriptor() const = 0;

        virtual void AddCommandList(std::shared_ptr<RHICommandList> commandList) = 0;
        virtual const std::vector<std::shared_ptr<RHICommandList>>& GetCommandLists() const = 0;
        virtual void ClearCommandLists() = 0;
    };

} // namespace RHI
} // namespace Core