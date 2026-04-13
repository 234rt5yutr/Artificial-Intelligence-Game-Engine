#pragma once

#include "Core/RHI/RHIPipelineState.h"
#include <vulkan/vulkan.h>

namespace Core {
namespace RHI {

    class VulkanContext;

    class VulkanPipelineState : public RHIPipelineState {
    public:
        // Normally, a pipeline requires a pipeline layout and render pass.
        // We pass them directly here or through the descriptor if abstracted.
        VulkanPipelineState(VulkanContext* context,
                            const GraphicsPipelineDescriptor& desc,
                            VkRenderPass renderPass,
                            VkPipelineLayout pipelineLayout,
                            VkPipelineCache pipelineCache = VK_NULL_HANDLE);
        ~VulkanPipelineState() override;

        VkPipeline GetPipeline() const { return m_Pipeline; }

    private:
        VkShaderModule CreateShaderModule(const uint32_t* code, std::size_t size);

        VulkanContext* m_Context;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;
    };

} // namespace RHI
} // namespace Core
