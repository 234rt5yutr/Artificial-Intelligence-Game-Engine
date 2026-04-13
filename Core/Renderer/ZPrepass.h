#pragma once

#include "Core/RHI/RHIDevice.h"
#include "Core/RHI/RHIRenderPass.h"
#include "Core/RHI/RHICommandList.h"
#include "Core/RHI/RHITexture.h"
#include "Core/RHI/RHIPipelineState.h"
#include "Core/Renderer/RenderGraph/RenderGraphTypes.h"
#include <memory>
#include <vector>

namespace Core {
namespace Renderer {

    class ZPrepass {
    public:
        ZPrepass() = default;
        ~ZPrepass() = default;

        void Initialize(std::shared_ptr<RHI::RHIDevice> device, uint32_t width, uint32_t height);
        void Shutdown();

        void Resize(std::shared_ptr<RHI::RHIDevice> device, uint32_t width, uint32_t height);

        void BeginPass(std::shared_ptr<RHI::RHICommandList> commandList);
        void EndPass(std::shared_ptr<RHI::RHICommandList> commandList);
        Result<RenderGraphPassHandle> RegisterRenderGraphPassHook() const;

        std::shared_ptr<RHI::RHITexture> GetDepthTexture() const { return m_DepthTexture; }
        std::shared_ptr<RHI::RHIRenderPass> GetRenderPass() const { return m_RenderPass; }

    private:
        void CreateResources(std::shared_ptr<RHI::RHIDevice> device, uint32_t width, uint32_t height);

        std::shared_ptr<RHI::RHITexture> m_DepthTexture;
        std::shared_ptr<RHI::RHIRenderPass> m_RenderPass;
    };

} // namespace Renderer
} // namespace Core
