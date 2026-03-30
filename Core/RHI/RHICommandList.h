#pragma once

#include "RHIBuffer.h"
#include "RHITexture.h"
#include <memory>
#include <cstdint>

namespace Core {
namespace RHI {

    class RHICommandList {
    public:
        virtual ~RHICommandList() = default;

        virtual void Begin() = 0;
        virtual void End() = 0;

        virtual void BeginRenderPass(std::shared_ptr<class RHIRenderPass> renderPass) = 0;
        virtual void EndRenderPass() = 0;

        virtual void CopyBuffer(std::shared_ptr<RHIBuffer> src, std::shared_ptr<RHIBuffer> dst, std::size_t size) = 0;
        virtual void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
        virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) = 0;
        virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
    };

} // namespace RHI
} // namespace Core
