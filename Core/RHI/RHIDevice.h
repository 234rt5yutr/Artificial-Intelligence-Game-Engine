#pragma once

#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"
#include "RHICommandList.h"
#include "RHIPipelineState.h"
#include <memory>

namespace Core {
namespace RHI {

    class RHIDevice {
    public:
        virtual ~RHIDevice() = default;

        virtual std::shared_ptr<RHIBuffer> CreateBuffer(const BufferDescriptor& desc) = 0;
        virtual std::shared_ptr<RHITexture> CreateTexture(const TextureDescriptor& desc) = 0;
        virtual std::shared_ptr<RHISampler> CreateSampler(const SamplerDescriptor& desc) = 0;
        virtual std::shared_ptr<RHICommandList> CreateCommandList() = 0;
        virtual std::shared_ptr<RHIPipelineState> CreateGraphicsPipelineState(const GraphicsPipelineDescriptor& desc) = 0;

        virtual void SubmitCommandList(std::shared_ptr<RHICommandList> commandList) = 0;
        virtual void WaitIdle() = 0;
    };

} // namespace RHI
} // namespace Core
