#include "VulkanPipelineState.h"
#include "VulkanContext.h"
#include "Core/Log.h"
#include "Core/Assert.h"
#include <vector>

namespace Core {
namespace RHI {

    static VkPrimitiveTopology GetVulkanTopology(PrimitiveTopology topology) {
        switch (topology) {
            case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }
    }

    static VkPolygonMode GetVulkanPolygonMode(PolygonMode mode) {
        switch (mode) {
            case PolygonMode::Fill: return VK_POLYGON_MODE_FILL;
            case PolygonMode::Line: return VK_POLYGON_MODE_LINE;
            case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
            default: return VK_POLYGON_MODE_FILL;
        }
    }

    static VkCullModeFlags GetVulkanCullMode(CullMode mode) {
        switch (mode) {
            case CullMode::None: return VK_CULL_MODE_NONE;
            case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
            case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
            case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
            default: return VK_CULL_MODE_BACK_BIT;
        }
    }

    VulkanPipelineState::VulkanPipelineState(VulkanContext* context, const GraphicsPipelineDescriptor& desc, VkRenderPass renderPass, VkPipelineLayout pipelineLayout)
        : m_Context(context) {

        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        std::vector<VkShaderModule> modulesToDestroy;

        for (const auto& shaderDesc : desc.shaders) {
            VkShaderModule module = CreateShaderModule(shaderDesc.data, shaderDesc.size);
            modulesToDestroy.push_back(module);

            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            if (shaderDesc.stage == ShaderStage::Vertex) {
                stageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            } else if (shaderDesc.stage == ShaderStage::Fragment) {
                stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            } else if (shaderDesc.stage == ShaderStage::Compute) {
                stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            }
            stageInfo.module = module;
            stageInfo.pName = shaderDesc.entryPoint;

            shaderStages.push_back(stageInfo);
        }

        std::vector<VkVertexInputBindingDescription> bindingDescriptions;
        for (const auto& binding : desc.vertexBindings) {
            VkVertexInputBindingDescription vbd{};
            vbd.binding = binding.binding;
            vbd.stride = binding.stride;
            vbd.inputRate = binding.inputRateInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
            bindingDescriptions.push_back(vbd);
        }

        std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
        for (const auto& attr : desc.vertexAttributes) {
            VkVertexInputAttributeDescription vad{};
            vad.location = attr.location;
            vad.binding = attr.binding;
            vad.format = static_cast<VkFormat>(attr.format); // Assuming explicit cast for now
            vad.offset = attr.offset;
            attributeDescriptions.push_back(vad);
        }

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
        vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = GetVulkanTopology(desc.topology);
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = GetVulkanPolygonMode(desc.polygonMode);
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = GetVulkanCullMode(desc.cullMode);
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = desc.depthTestEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = desc.depthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
        for (uint32_t i = 0; i < desc.renderTargetCount; ++i) {
            VkPipelineColorBlendAttachmentState cba{};
            cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            if (i < desc.blendStates.size() && desc.blendStates[i].blendEnable) {
                cba.blendEnable = VK_TRUE;
                cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                cba.colorBlendOp = VK_BLEND_OP_ADD;
                cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                cba.alphaBlendOp = VK_BLEND_OP_ADD;
            } else {
                cba.blendEnable = VK_FALSE;
            }
            colorBlendAttachments.push_back(cba);
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        colorBlending.pAttachments = colorBlendAttachments.data();

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
ENGINE_
        if (vkCreateGraphicsPipelines(m_Context->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS) {
            CORE_ASSERT(false, "Failed to create graphics pipeline!");
        }

        for (VkShaderModule module : modulesToDestroy) {
            vkDestroyShaderModule(m_Context->GetDevice(), module, nullptr);
        }
    }

    VulkanPipelineState::~VulkanPipelineState() {
        if (m_Pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_Context->GetDevice(), m_Pipeline, nullptr);
            m_Pipeline = VK_NULL_HANDLE;
        }
    }

    VkShaderModule VulkanPipelineState::CreateShaderModule(const uint32_t* code, std::size_t size) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = size;
        createInfo.pCode = code;

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_Context->GetDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "Failed to create shader module!");
        }
        return shaderModule;
    }

} // namespace RHI
} // namespace Core
