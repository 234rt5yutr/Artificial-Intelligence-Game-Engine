#pragma once

// Concrete RHIDevice backed by the existing VulkanContext.
//
// RHIDevice had no implementation anywhere in the tree, which is why three
// subsystems written against it (TerrainSystem, FoliageSystem, SkyboxSystem)
// could not be constructed and stayed out of the frame pipeline. This wires the
// interface to the Vulkan objects that already exist: VulkanBuffer and
// VulkanTexture both implement their RHI interfaces and only need the VMA
// allocator that VulkanContext already owns.
//
// Resource creation (buffers, textures, samplers) is fully implemented because
// that is what the blocked subsystems actually call. Pipeline/render-pass/command
// -list creation are not: the engine still submits through VulkanContext directly
// rather than through the RHI, so implementing them here would create a second,
// unused submission path. Those entry points log once and return null instead of
// pretending to work.

#include "Core/RHI/RHIDevice.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <vulkan/vulkan.h>
#include <memory>
#include <mutex>
#include <vector>

namespace Core {
namespace RHI {

    class VulkanDevice : public RHIDevice {
    public:
        explicit VulkanDevice(VulkanContext* context);
        ~VulkanDevice() override;

        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        // --- Implemented ----------------------------------------------------
        std::shared_ptr<RHIBuffer> CreateBuffer(const BufferDescriptor& desc) override;
        std::shared_ptr<RHITexture> CreateTexture(const TextureDescriptor& desc) override;
        std::shared_ptr<RHISampler> CreateSampler(const SamplerDescriptor& desc) override;
        void WaitIdle() override;

        // --- Not routed through the RHI yet ---------------------------------
        std::shared_ptr<RHICommandList> CreateCommandList() override;
        std::shared_ptr<RHIPipelineState> CreateGraphicsPipelineState(const GraphicsPipelineDescriptor& desc) override;
        std::shared_ptr<RHIRenderPass> CreateRenderPass(const RenderPassDescriptor& desc) override;
        void SubmitCommandList(std::shared_ptr<RHICommandList> commandList) override;
        void ExecuteRenderPass(std::shared_ptr<RHIRenderPass> renderPass) override;

        VulkanContext* GetContext() const { return m_Context; }

    private:
        // Warn once per unimplemented entry point rather than every frame.
        void WarnUnimplemented(const char* what);

        VulkanContext* m_Context = nullptr;

        std::mutex m_SamplerMutex;
        std::vector<VkSampler> m_Samplers;

        std::mutex m_WarnMutex;
        std::vector<const char*> m_WarnedAbout;
    };

} // namespace RHI
} // namespace Core
