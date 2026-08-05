#include "ShadowRenderer.h"

#include "Core/Log.h"
#include "Core/RHI/ShaderCompiler.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Renderer/GPUDriven/ClusterCullShader.h"
#include "Core/Renderer/GPUDriven/GPUScene.h"
#include "Core/Renderer/Mesh.h"
#include "Core/Renderer/SceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kCullGroupSize = 64;
        constexpr uint32_t kCounterSlots = 8;

        void ExtractFrustumPlanes(const Math::Mat4& viewProjection, Math::Vec4 outPlanes[6]) {
            const Math::Mat4& m = viewProjection;
            for (int i = 0; i < 3; ++i) {
                outPlanes[i * 2 + 0] = Math::Vec4(m[0][3] + m[0][i], m[1][3] + m[1][i],
                                                  m[2][3] + m[2][i], m[3][3] + m[3][i]);
                outPlanes[i * 2 + 1] = Math::Vec4(m[0][3] - m[0][i], m[1][3] - m[1][i],
                                                  m[2][3] - m[2][i], m[3][3] - m[3][i]);
            }
            for (int i = 0; i < 6; ++i) {
                const float length = glm::length(Math::Vec3(outPlanes[i]));
                if (length > 1e-6f) {
                    outPlanes[i] /= length;
                }
            }
        }

        // Depth-only. No fragment stage at all: there is no colour attachment to
        // write, and leaving one out lets the driver take its early-depth path.
        const char* kShadowVertexShader = R"GLSL(
#version 450

layout(location = 0) in vec3 inPosition;

struct Instance {
    mat4 transform;
    vec4 boundsCenterRadius;
    uint clusterBase;
    uint clusterCount;
    uint materialIndex;
    uint flags;
};
layout(std430, set = 0, binding = 0) readonly buffer Instances { Instance instances[]; };

layout(push_constant) uniform Push {
    mat4 lightViewProjection;
} push;

void main() {
    // Indirect draws tag each command with its instance through firstInstance,
    // exactly as the main geometry pass does.
    mat4 model = instances[gl_InstanceIndex].transform;
    gl_Position = push.lightViewProjection * model * vec4(inPosition, 1.0);
}
)GLSL";

    } // namespace

    ShadowRenderer::~ShadowRenderer() {
        Shutdown();
    }

    bool ShadowRenderer::Initialize(RHI::VulkanContext* context, uint32_t maxClusterSlots) {
        if (!context || context->GetDevice() == VK_NULL_HANDLE) {
            return false;
        }
        Shutdown();
        m_Context = context;
        m_MaxClusterSlots = maxClusterSlots;

        if (!CreateRenderPass() || !CreateCullResources(maxClusterSlots) ||
            !CreatePipeline() || !CreateCascadeTargets() || !CreateAtlasTargets()) {
            Shutdown();
            return false;
        }

        ENGINE_CORE_INFO("Shadow renderer ready: {} cascades at {}px, spot atlas {}px "
                         "({} tiles of {}px)",
                         m_Settings.CascadeCount, m_Settings.CascadeResolution,
                         m_Stats.AtlasResolution,
                         m_Settings.AtlasTilesPerRow * m_Settings.AtlasTilesPerRow,
                         m_Stats.AtlasTileSize);
        return true;
    }

    bool ShadowRenderer::CreateRenderPass() {
        VkAttachmentDescription depth{};
        depth.format = m_Context->GetDepthFormat();
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depthRef;

        // The lit pass samples this in the fragment stage, so the write has to
        // be visible to it before the geometry pass starts shading.
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &depth;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2;
        info.pDependencies = dependencies;

        if (vkCreateRenderPass(m_Context->GetDevice(), &info, nullptr, &m_RenderPass) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ShadowRenderer: render pass creation failed");
            return false;
        }
        return true;
    }

    bool ShadowRenderer::CreateCullResources(uint32_t maxClusterSlots) {
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();

        m_CullSetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        });
        if (m_CullSetLayout == VK_NULL_HANDLE) {
            return false;
        }

        m_CullPipeline = RHI::CreateComputePipeline(device, m_Context->GetPipelineCache(),
                                                    kClusterCullShaderSource, "shadow_cluster_cull",
                                                    {m_CullSetLayout}, 0);
        if (!m_CullPipeline.IsValid()) {
            return false;
        }

        const VkDeviceSize drawBytes = static_cast<VkDeviceSize>(maxClusterSlots) * 20;
        const VkBufferUsageFlags drawUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                             VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                             VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        for (uint32_t view = 0; view < kMaxShadowViews; ++view) {
            if (!RHI::CreateGpuBuffer(allocator, drawBytes, drawUsage, false, m_DrawBuffers[view]) ||
                !RHI::CreateGpuBuffer(allocator, sizeof(CullUniforms),
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_CullUniforms[view])) {
                return false;
            }
        }
        if (!RHI::CreateGpuBuffer(allocator, static_cast<VkDeviceSize>(maxClusterSlots) * sizeof(uint32_t),
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  false, m_RetestFlags) ||
            !RHI::CreateGpuBuffer(allocator, kCounterSlots * sizeof(uint32_t),
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  true, m_Counters)) {
            return false;
        }

        // 1x1 stand-in so the shared cull shader's HZB binding is always valid.
        // A shadow view passes occlusion = 0 and never samples it.
        RHI::GpuImageDesc dummy{};
        dummy.Width = 1;
        dummy.Height = 1;
        dummy.Format = VK_FORMAT_R32_SFLOAT;
        dummy.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        dummy.DebugName = "ShadowDummyHZB";
        if (!RHI::CreateGpuImage(device, allocator, dummy, m_DummyHZB)) {
            return false;
        }
        m_DummySampler = RHI::CreateClampedSampler(device, VK_FILTER_NEAREST);

        VkDescriptorSetLayoutBinding drawBinding{};
        drawBinding.binding = 0;
        drawBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        drawBinding.descriptorCount = 1;
        drawBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo drawLayout{};
        drawLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        drawLayout.bindingCount = 1;
        drawLayout.pBindings = &drawBinding;
        if (vkCreateDescriptorSetLayout(device, &drawLayout, nullptr, &m_DrawSetLayout) != VK_SUCCESS) {
            return false;
        }

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6 * kMaxShadowViews + 2},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxShadowViews},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxShadowViews},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = kMaxShadowViews + 2;
        poolInfo.poolSizeCount = 3;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            return false;
        }

        std::vector<VkDescriptorSetLayout> cullLayouts(kMaxShadowViews, m_CullSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = kMaxShadowViews;
        allocInfo.pSetLayouts = cullLayouts.data();
        if (vkAllocateDescriptorSets(device, &allocInfo, m_CullSets) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ShadowRenderer: cull descriptor allocation failed");
            return false;
        }

        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_DrawSetLayout;
        if (vkAllocateDescriptorSets(device, &allocInfo, &m_DrawSet) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ShadowRenderer: draw descriptor allocation failed");
            return false;
        }
        return true;
    }

    bool ShadowRenderer::CreatePipeline() {
        VkDevice device = m_Context->GetDevice();

        auto spirv = RHI::ShaderCompiler::CompileToSPIRV(kShadowVertexShader,
                                                         RHI::ShaderStage::Vertex, "shadow.vert");
        if (spirv.empty()) {
            ENGINE_CORE_ERROR("ShadowRenderer: depth shader failed to compile");
            return false;
        }
        VkShaderModule module = m_Context->CreateShaderModule(spirv);

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(Math::Mat4);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_DrawSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
            m_Context->DestroyShaderModule(module);
            return false;
        }

        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        stage.module = module;
        stage.pName = "main";

        // The merged arena is interleaved Vertex, so the stride is the full
        // vertex even though only the position is read.
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attribute{};
        attribute.location = 0;
        attribute.binding = 0;
        attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
        attribute.offset = offsetof(Vertex, position);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 1;
        vertexInput.pVertexAttributeDescriptions = &attribute;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        // Front-face culling for the shadow pass: it moves the acne to surfaces
        // the camera cannot see, which is the cheapest half of the fix.
        raster.cullMode = VK_CULL_MODE_FRONT_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        raster.depthBiasEnable = VK_TRUE;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 0;

        // Bias is dynamic so the settings can be tuned at runtime without
        // rebuilding the pipeline.
        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 3;
        dynamicState.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 1;
        pipelineInfo.pStages = &stage;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = m_RenderPass;
        pipelineInfo.subpass = 0;

        const VkResult result = vkCreateGraphicsPipelines(device, m_Context->GetPipelineCache(), 1,
                                                          &pipelineInfo, nullptr, &m_Pipeline);
        m_Context->DestroyShaderModule(module);
        if (result != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ShadowRenderer: depth pipeline creation failed");
            return false;
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        // White border: a lookup that leaves the cascade reads "fully lit"
        // rather than shadowing everything past the last cascade.
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.compareEnable = VK_TRUE;
        samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &m_ComparisonSampler) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ShadowRenderer: comparison sampler creation failed");
            return false;
        }
        return true;
    }

    bool ShadowRenderer::CreateCascadeTargets() {
        VkDevice device = m_Context->GetDevice();
        const uint32_t cascades = std::clamp(m_Settings.CascadeCount, 1u, kMaxShadowCascades);
        const uint32_t resolution = std::clamp(m_Settings.CascadeResolution, 256u, 8192u);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {resolution, resolution, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = cascades;
        imageInfo.format = m_Context->GetDepthFormat();
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        m_CascadeArray = RHI::GpuImage{};
        if (vmaCreateImage(m_Context->GetAllocator(), &imageInfo, &allocInfo,
                           &m_CascadeArray.Image, &m_CascadeArray.Allocation, nullptr) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ShadowRenderer: cascade array allocation failed ({} x {}px)",
                              cascades, resolution);
            return false;
        }
        m_CascadeArray.Format = imageInfo.format;
        m_CascadeArray.Extent = {resolution, resolution};
        m_CascadeArray.MipLevels = 1;
        m_CascadeArray.Aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        m_CascadeArray.Layout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImageViewCreateInfo arrayView{};
        arrayView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        arrayView.image = m_CascadeArray.Image;
        arrayView.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        arrayView.format = imageInfo.format;
        arrayView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        arrayView.subresourceRange.levelCount = 1;
        arrayView.subresourceRange.layerCount = cascades;
        if (vkCreateImageView(device, &arrayView, nullptr, &m_CascadeArray.View) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ShadowRenderer: cascade array view creation failed");
            return false;
        }

        // One single-layer view and framebuffer per cascade: a render pass
        // instance writes exactly one layer.
        m_LayerViews.assign(cascades, VK_NULL_HANDLE);
        m_Framebuffers.assign(cascades, VK_NULL_HANDLE);
        for (uint32_t cascade = 0; cascade < cascades; ++cascade) {
            VkImageViewCreateInfo layerView = arrayView;
            layerView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            layerView.subresourceRange.baseArrayLayer = cascade;
            layerView.subresourceRange.layerCount = 1;
            if (vkCreateImageView(device, &layerView, nullptr, &m_LayerViews[cascade]) != VK_SUCCESS) {
                ENGINE_CORE_ERROR("ShadowRenderer: cascade {} view creation failed", cascade);
                return false;
            }

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_RenderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &m_LayerViews[cascade];
            framebufferInfo.width = resolution;
            framebufferInfo.height = resolution;
            framebufferInfo.layers = 1;
            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_Framebuffers[cascade]) != VK_SUCCESS) {
                ENGINE_CORE_ERROR("ShadowRenderer: cascade {} framebuffer creation failed", cascade);
                return false;
            }
        }

        m_Stats.CascadeCount = cascades;
        m_Stats.Resolution = resolution;
        m_TargetsDirty = false;
        return true;
    }

    bool ShadowRenderer::CreateAtlasTargets() {
        VkDevice device = m_Context->GetDevice();
        const uint32_t resolution = std::clamp(m_Settings.AtlasResolution, 512u, 8192u);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {resolution, resolution, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_Context->GetDepthFormat();
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        m_Atlas = RHI::GpuImage{};
        if (vmaCreateImage(m_Context->GetAllocator(), &imageInfo, &allocInfo,
                           &m_Atlas.Image, &m_Atlas.Allocation, nullptr) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ShadowRenderer: shadow atlas allocation failed ({}px)", resolution);
            return false;
        }
        m_Atlas.Format = imageInfo.format;
        m_Atlas.Extent = {resolution, resolution};
        m_Atlas.MipLevels = 1;
        m_Atlas.Aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        m_Atlas.Layout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Atlas.Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = imageInfo.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &m_Atlas.View) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ShadowRenderer: shadow atlas view creation failed");
            return false;
        }

        // One framebuffer for the whole atlas: every tile is a viewport and
        // scissor inside a single render pass instance, so the atlas is cleared
        // once per frame rather than once per light.
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &m_Atlas.View;
        framebufferInfo.width = resolution;
        framebufferInfo.height = resolution;
        framebufferInfo.layers = 1;
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_AtlasFramebuffer) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ShadowRenderer: shadow atlas framebuffer creation failed");
            return false;
        }

        m_Stats.AtlasResolution = resolution;
        m_Stats.AtlasTileSize = resolution / std::max(1u, m_Settings.AtlasTilesPerRow);
        return true;
    }

    void ShadowRenderer::DestroyAtlasTargets() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        if (m_AtlasFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, m_AtlasFramebuffer, nullptr);
            m_AtlasFramebuffer = VK_NULL_HANDLE;
        }
        RHI::DestroyGpuImage(device, m_Context->GetAllocator(), m_Atlas);
        m_SpotSlots.clear();
    }

    Math::Vec4 ShadowRenderer::GetAtlasParams() const {
        const float resolution = static_cast<float>(std::max(1u, m_Stats.AtlasResolution));
        return Math::Vec4(resolution, resolution,
                          static_cast<float>(std::max(1u, m_Stats.AtlasTileSize)),
                          1.0f / resolution);
    }

    void ShadowRenderer::DestroyCascadeTargets() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        for (VkFramebuffer framebuffer : m_Framebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
        }
        m_Framebuffers.clear();
        for (VkImageView view : m_LayerViews) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, view, nullptr);
            }
        }
        m_LayerViews.clear();
        RHI::DestroyGpuImage(device, m_Context->GetAllocator(), m_CascadeArray);
    }

    bool ShadowRenderer::ApplySettings() {
        if (!m_Context) {
            return false;
        }
        vkDeviceWaitIdle(m_Context->GetDevice());
        DestroyCascadeTargets();
        DestroyAtlasTargets();
        return CreateCascadeTargets() && CreateAtlasTargets();
    }

    float ShadowRenderer::GetTexelSize() const {
        const uint32_t resolution = std::max(1u, m_Stats.Resolution);
        return static_cast<float>(m_Settings.PcfRadius) / static_cast<float>(resolution);
    }

    void ShadowRenderer::FitCascades(const FrameRenderData& frame, const Math::Vec3& lightDirection) {
        const uint32_t cascades = std::clamp(m_Settings.CascadeCount, 1u, kMaxShadowCascades);

        // Recover the camera's near and far from the projection. Jitter only
        // touches [2][0] and [2][1], so it does not disturb this.
        const float p22 = frame.Projection[2][2];
        const float p32 = frame.Projection[3][2];
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        if (std::abs(p22) > 1e-6f && std::abs(p22 + 1.0f) > 1e-6f) {
            nearPlane = p32 / p22;
            farPlane = p32 / (p22 + 1.0f);
        }
        if (!(nearPlane > 0.0f) || !(farPlane > nearPlane)) {
            nearPlane = 0.1f;
            farPlane = 1000.0f;
        }
        // Cascades follow the shadow distance, not the camera's far plane, which
        // is usually far enough out to spend every cascade on empty sky.
        farPlane = std::min(farPlane, std::max(nearPlane + 1.0f, m_Settings.MaxShadowDistance));

        // Unproject the full frustum once; each cascade is a slice along it.
        const Math::Mat4 inverseViewProjection = glm::inverse(frame.ViewProjection);
        Math::Vec3 nearCorners[4];
        Math::Vec3 farCorners[4];
        const Math::Vec2 ndc[4] = {
            Math::Vec2(-1.0f, -1.0f), Math::Vec2(1.0f, -1.0f),
            Math::Vec2(1.0f, 1.0f), Math::Vec2(-1.0f, 1.0f)
        };
        for (int corner = 0; corner < 4; ++corner) {
            Math::Vec4 nearPoint = inverseViewProjection * Math::Vec4(ndc[corner], 0.0f, 1.0f);
            Math::Vec4 farPoint = inverseViewProjection * Math::Vec4(ndc[corner], 1.0f, 1.0f);
            nearCorners[corner] = Math::Vec3(nearPoint) / nearPoint.w;
            farCorners[corner] = Math::Vec3(farPoint) / farPoint.w;
        }

        const float range = farPlane - nearPlane;
        const float ratio = farPlane / nearPlane;
        float previousSplit = nearPlane;

        for (uint32_t cascade = 0; cascade < cascades; ++cascade) {
            // Practical split scheme: blend a uniform and a logarithmic
            // distribution so the near cascades stay tight without the far ones
            // collapsing.
            const float fraction = static_cast<float>(cascade + 1) / static_cast<float>(cascades);
            const float logSplit = nearPlane * std::pow(ratio, fraction);
            const float uniformSplit = nearPlane + range * fraction;
            const float split = m_Settings.CascadeSplitLambda * logSplit +
                                (1.0f - m_Settings.CascadeSplitLambda) * uniformSplit;

            const float nearFraction = (previousSplit - nearPlane) / range;
            const float farFraction = (split - nearPlane) / range;

            Math::Vec3 sliceCorners[8];
            for (int corner = 0; corner < 4; ++corner) {
                const Math::Vec3 ray = farCorners[corner] - nearCorners[corner];
                sliceCorners[corner] = nearCorners[corner] + ray * nearFraction;
                sliceCorners[corner + 4] = nearCorners[corner] + ray * farFraction;
            }

            // A bounding sphere rather than a box: its size does not change as
            // the camera rotates, which is half of what stops the shadow
            // shimmering.
            Math::Vec3 center(0.0f);
            for (const Math::Vec3& corner : sliceCorners) {
                center += corner;
            }
            center /= 8.0f;

            float radius = 0.0f;
            for (const Math::Vec3& corner : sliceCorners) {
                radius = std::max(radius, glm::length(corner - center));
            }
            radius = std::ceil(radius * 16.0f) / 16.0f;

            const Math::Vec3 up = std::abs(lightDirection.y) > 0.99f
                                      ? Math::Vec3(0.0f, 0.0f, 1.0f)
                                      : Math::Vec3(0.0f, 1.0f, 0.0f);
            // Pull the eye back beyond the slice so casters behind it still
            // reach the map.
            const float extrusion = radius * 3.0f;
            Math::Mat4 lightView = glm::lookAt(center - lightDirection * extrusion, center, up);

            if (m_Settings.StabilizeCascades) {
                // Snap the centre to whole texels in light space. Without this
                // the projection slides by sub-texel amounts every frame and the
                // shadow edge crawls.
                const float texelsPerUnit = static_cast<float>(m_Stats.Resolution) / (radius * 2.0f);
                Math::Vec4 lightSpaceCenter = lightView * Math::Vec4(center, 1.0f);
                lightSpaceCenter.x = std::floor(lightSpaceCenter.x * texelsPerUnit) / texelsPerUnit;
                lightSpaceCenter.y = std::floor(lightSpaceCenter.y * texelsPerUnit) / texelsPerUnit;
                const Math::Vec3 snappedCenter = Math::Vec3(glm::inverse(lightView) * lightSpaceCenter);
                lightView = glm::lookAt(snappedCenter - lightDirection * extrusion, snappedCenter, up);
            }

            Math::Mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius,
                                                    0.0f, extrusion + radius * 2.0f);
            // Same clip-space Y flip the camera applies, so the shadow map is
            // oriented the way the sampling code expects.
            lightProjection[1][1] *= -1.0f;

            m_CascadeMatrices[cascade] = lightProjection * lightView;
            m_CascadeSplitsView[static_cast<int>(cascade)] = split;
            m_Stats.CascadeSplits[cascade] = split;
            previousSplit = split;
        }

        // Unused slots reuse the last cascade so a shader that indexes past the
        // count still samples something valid.
        for (uint32_t cascade = cascades; cascade < kMaxShadowCascades; ++cascade) {
            m_CascadeMatrices[cascade] = m_CascadeMatrices[cascades - 1];
            m_CascadeSplitsView[static_cast<int>(cascade)] = m_CascadeSplitsView[static_cast<int>(cascades - 1)];
        }
    }

    void ShadowRenderer::FitSpotShadows(const FrameRenderData& frame) {
        m_SpotSlots.clear();
        if (!m_Settings.SpotShadowsEnabled || m_Atlas.Image == VK_NULL_HANDLE) {
            return;
        }

        const uint32_t tilesPerRow = std::max(1u, m_Settings.AtlasTilesPerRow);
        const uint32_t tileCapacity = std::min(tilesPerRow * tilesPerRow, kMaxSpotShadows);

        // Rank by how much of the screen the light plausibly covers - intensity
        // and reach over distance - so the tiles go to the lights a viewer is
        // most likely to notice rather than to whatever came first in the list.
        struct Candidate {
            int32_t LightIndex;
            float Score;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(frame.SpotLights.size());

        for (std::size_t i = 0; i < frame.SpotLights.size(); ++i) {
            const auto& light = frame.SpotLights[i];
            if (light.CastShadows <= 0.5f || light.Radius <= 0.0f) {
                continue;
            }
            const float distance = std::max(0.5f, glm::length(light.Position - frame.CameraPosition));
            const float score = (light.Intensity * light.Radius) / distance;
            candidates.push_back({static_cast<int32_t>(i), score});
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& lhs, const Candidate& rhs) { return lhs.Score > rhs.Score; });
        if (candidates.size() > tileCapacity) {
            candidates.resize(tileCapacity);
        }

        for (std::size_t slotIndex = 0; slotIndex < candidates.size(); ++slotIndex) {
            const auto& light = frame.SpotLights[candidates[slotIndex].LightIndex];

            Math::Vec3 direction = light.Direction;
            const float directionLength = glm::length(direction);
            if (directionLength < 1e-4f) {
                continue;
            }
            direction /= directionLength;

            const Math::Vec3 up = std::abs(direction.y) > 0.99f ? Math::Vec3(0.0f, 0.0f, 1.0f)
                                                                : Math::Vec3(0.0f, 1.0f, 0.0f);
            const Math::Mat4 lightView = glm::lookAt(light.Position, light.Position + direction, up);

            // OuterCutoff is already a half-angle in radians. The projection
            // needs the full angle, widened slightly so PCF taps at the cone
            // edge still land inside the tile.
            const float outerAngle = std::clamp(light.OuterCutoff, 0.05f, 1.5f);
            const float fov = std::min(outerAngle * 2.2f, 3.0f);
            Math::Mat4 lightProjection = glm::perspective(fov, 1.0f, 0.05f, light.Radius);
            lightProjection[1][1] *= -1.0f;

            SpotShadowSlot slot;
            slot.ViewProjection = lightProjection * lightView;
            slot.LightIndex = candidates[slotIndex].LightIndex;
            slot.TileX = static_cast<uint32_t>(slotIndex) % tilesPerRow;
            slot.TileY = static_cast<uint32_t>(slotIndex) / tilesPerRow;
            m_SpotSlots.push_back(slot);
        }

        m_Stats.SpotShadowCount = static_cast<uint32_t>(m_SpotSlots.size());
    }

    bool ShadowRenderer::BeginFrame(const FrameRenderData& frame, GPUScene& scene) {
        m_Stats.Active = false;
        m_ShadowLightIndex = -1;
        m_Stats.ShadowLightIndex = -1;
        m_Stats.SpotShadowCount = 0;
        m_SpotSlots.clear();
        m_ClusterSlots = 0;

        if (!IsInitialized() || !m_Settings.Enabled) {
            return false;
        }
        if (m_TargetsDirty && !ApplySettings()) {
            return false;
        }

        m_ClusterSlots = scene.GetFrameClusterSlotCount();
        m_Stats.ClusterSlots = m_ClusterSlots;

        // Cascades are fitted to one directional light: the first that casts.
        for (std::size_t i = 0; i < frame.DirectionalLights.size() && i < 4; ++i) {
            if (frame.DirectionalLights[i].CastShadows > 0.5f) {
                m_ShadowLightIndex = static_cast<int32_t>(i);
                m_LightDirection = frame.DirectionalLights[i].Direction;
                break;
            }
        }

        if (m_ShadowLightIndex >= 0) {
            const float directionLength = glm::length(m_LightDirection);
            if (directionLength < 1e-4f) {
                m_ShadowLightIndex = -1;
            } else {
                m_LightDirection /= directionLength;
                FitCascades(frame, m_LightDirection);
            }
        }

        // Spot tiles are independent of the directional cascades: a scene with
        // no sun can still have shadowed spots.
        FitSpotShadows(frame);

        m_Stats.ShadowLightIndex = m_ShadowLightIndex;
        m_Stats.Active = m_ClusterSlots > 0 && (m_ShadowLightIndex >= 0 || !m_SpotSlots.empty());
        return m_Stats.Active;
    }

    void ShadowRenderer::DispatchCull(VkCommandBuffer cmd, GPUScene& scene, uint32_t viewIndex,
                                      const Math::Mat4& viewProjection, const Math::Vec3& viewDirection) {
        CullUniforms uniforms{};
        uniforms.ViewProjection = viewProjection;
        uniforms.View = viewProjection;
        uniforms.Projection = viewProjection;
        ExtractFrustumPlanes(viewProjection, uniforms.FrustumPlanes);
        uniforms.CameraPosition = Math::Vec4(viewDirection, 0.0f);
        uniforms.HZBParams = Math::Vec4(1.0f, 1.0f, 1.0f, 0.0f);
        // No HZB and no cone culling for a shadow view: a backfacing cluster
        // still occludes, and dropping it punches holes in the shadow.
        uniforms.Flags = Math::Vec4(0.0f, 0.0f, 1.0f, 0.0f);
        uniforms.Counts = Math::UVec4(m_ClusterSlots, scene.GetFrameInstanceCount(), 0, 0);
        if (m_CullUniforms[viewIndex].Mapped) {
            std::memcpy(m_CullUniforms[viewIndex].Mapped, &uniforms, sizeof(uniforms));
        }

        VkDescriptorBufferInfo bufferInfos[6]{};
        bufferInfos[0] = {scene.GetInstanceBuffer(), 0, VK_WHOLE_SIZE};
        bufferInfos[1] = {scene.GetClusterBuffer(), 0, VK_WHOLE_SIZE};
        bufferInfos[2] = {scene.GetInstanceOffsetBuffer(), 0, VK_WHOLE_SIZE};
        bufferInfos[3] = {m_DrawBuffers[viewIndex].Buffer, 0, VK_WHOLE_SIZE};
        bufferInfos[4] = {m_RetestFlags.Buffer, 0, VK_WHOLE_SIZE};
        bufferInfos[5] = {m_Counters.Buffer, 0, VK_WHOLE_SIZE};

        VkDescriptorImageInfo hzbInfo{};
        hzbInfo.sampler = m_DummySampler;
        hzbInfo.imageView = m_DummyHZB.View;
        hzbInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorBufferInfo uniformInfo{m_CullUniforms[viewIndex].Buffer, 0, sizeof(CullUniforms)};

        VkWriteDescriptorSet writes[8]{};
        for (uint32_t i = 0; i < 6; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_CullSets[viewIndex];
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &bufferInfos[i];
        }
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = m_CullSets[viewIndex];
        writes[6].dstBinding = 6;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].pImageInfo = &hzbInfo;
        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = m_CullSets[viewIndex];
        writes[7].dstBinding = 7;
        writes[7].descriptorCount = 1;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[7].pBufferInfo = &uniformInfo;
        vkUpdateDescriptorSets(m_Context->GetDevice(), 8, writes, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline.Pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline.Layout,
                                0, 1, &m_CullSets[viewIndex], 0, nullptr);
        vkCmdDispatch(cmd, (m_ClusterSlots + kCullGroupSize - 1) / kCullGroupSize, 1, 1);

        RHI::BufferBarrier(cmd, m_DrawBuffers[viewIndex].Buffer, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
    }

    void ShadowRenderer::Render(VkCommandBuffer cmd, GPUScene& scene) {
        if (!IsInitialized() || m_ClusterSlots == 0 ||
            scene.GetInstanceBuffer() == VK_NULL_HANDLE) {
            return;
        }
        const bool hasCascades = m_ShadowLightIndex >= 0;
        if (!hasCascades && m_SpotSlots.empty()) {
            return;
        }

        const uint32_t cascades = hasCascades
                                      ? std::clamp(m_Settings.CascadeCount, 1u, kMaxShadowCascades)
                                      : 0u;
        VkDevice device = m_Context->GetDevice();

        vkCmdFillBuffer(cmd, m_Counters.Buffer, 0, kCounterSlots * sizeof(uint32_t), 0);
        RHI::BufferBarrier(cmd, m_Counters.Buffer, VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        // The dummy HZB is only ever bound, never sampled, but it still has to
        // be in the layout its descriptor declares.
        RHI::TransitionImage(cmd, m_DummyHZB, VK_IMAGE_LAYOUT_GENERAL);

        // Bind the instance buffer once for every view's draws.
        VkDescriptorBufferInfo instanceInfo{scene.GetInstanceBuffer(), 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet drawWrite{};
        drawWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        drawWrite.dstSet = m_DrawSet;
        drawWrite.dstBinding = 0;
        drawWrite.descriptorCount = 1;
        drawWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        drawWrite.pBufferInfo = &instanceInfo;
        vkUpdateDescriptorSets(device, 1, &drawWrite, 0, nullptr);

        // All culling first, then all rasterisation: one barrier covers every
        // view instead of interleaving compute and graphics per cascade.
        for (uint32_t cascade = 0; cascade < cascades; ++cascade) {
            DispatchCull(cmd, scene, cascade, m_CascadeMatrices[cascade], m_LightDirection);
        }
        for (std::size_t slot = 0; slot < m_SpotSlots.size(); ++slot) {
            DispatchCull(cmd, scene, kMaxShadowCascades + static_cast<uint32_t>(slot),
                         m_SpotSlots[slot].ViewProjection, m_LightDirection);
        }

        VkClearValue clear{};
        clear.depthStencil = {1.0f, 0};

        auto recordView = [&](uint32_t viewIndex, const Math::Mat4& viewProjection,
                              int32_t offsetX, int32_t offsetY, uint32_t extent) {
            VkViewport viewport{};
            viewport.x = static_cast<float>(offsetX);
            viewport.y = static_cast<float>(offsetY);
            viewport.width = static_cast<float>(extent);
            viewport.height = static_cast<float>(extent);
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {offsetX, offsetY};
            scissor.extent = {extent, extent};
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            vkCmdSetDepthBias(cmd, m_Settings.DepthBias, 0.0f, m_Settings.SlopeBias);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                                    0, 1, &m_DrawSet, 0, nullptr);
            vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(Math::Mat4), &viewProjection[0][0]);

            VkBuffer vertexBuffer = scene.GetVertexBuffer();
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmd, scene.GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            // One multi-draw per view: shadows do not care about material, so
            // there is nothing to batch by.
            vkCmdDrawIndexedIndirect(cmd, m_DrawBuffers[viewIndex].Buffer, 0, m_ClusterSlots, 20);
        };

        for (uint32_t cascade = 0; cascade < cascades; ++cascade) {
            VkRenderPassBeginInfo passBegin{};
            passBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            passBegin.renderPass = m_RenderPass;
            passBegin.framebuffer = m_Framebuffers[cascade];
            passBegin.renderArea.extent = {m_Stats.Resolution, m_Stats.Resolution};
            passBegin.clearValueCount = 1;
            passBegin.pClearValues = &clear;

            vkCmdBeginRenderPass(cmd, &passBegin, VK_SUBPASS_CONTENTS_INLINE);
            recordView(cascade, m_CascadeMatrices[cascade], 0, 0, m_Stats.Resolution);
            vkCmdEndRenderPass(cmd);
        }
        if (cascades > 0) {
            m_CascadeArray.Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }

        // The atlas is one render pass instance for every tile: cleared once,
        // then each light draws into its own viewport and scissor.
        if (!m_SpotSlots.empty() && m_AtlasFramebuffer != VK_NULL_HANDLE) {
            VkRenderPassBeginInfo passBegin{};
            passBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            passBegin.renderPass = m_RenderPass;
            passBegin.framebuffer = m_AtlasFramebuffer;
            passBegin.renderArea.extent = {m_Stats.AtlasResolution, m_Stats.AtlasResolution};
            passBegin.clearValueCount = 1;
            passBegin.pClearValues = &clear;

            vkCmdBeginRenderPass(cmd, &passBegin, VK_SUBPASS_CONTENTS_INLINE);
            for (std::size_t slot = 0; slot < m_SpotSlots.size(); ++slot) {
                const SpotShadowSlot& spot = m_SpotSlots[slot];
                recordView(kMaxShadowCascades + static_cast<uint32_t>(slot), spot.ViewProjection,
                           static_cast<int32_t>(spot.TileX * m_Stats.AtlasTileSize),
                           static_cast<int32_t>(spot.TileY * m_Stats.AtlasTileSize),
                           m_Stats.AtlasTileSize);
            }
            vkCmdEndRenderPass(cmd);
            m_Atlas.Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        } else if (m_Atlas.Layout == VK_IMAGE_LAYOUT_UNDEFINED) {
            // Nothing rendered into it, but the lit shader still samples it, so
            // it has to reach a readable layout at least once.
            RHI::TransitionImage(cmd, m_Atlas, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }

        if (m_Counters.Mapped) {
            const auto* counters = static_cast<const uint32_t*>(m_Counters.Mapped);
            // Summed across views: the shader has one counter set, and a
            // per-view breakdown is not worth a dispatch parameter each.
            for (uint32_t cascade = 0; cascade < kMaxShadowCascades; ++cascade) {
                m_Stats.VisibleClusters[cascade] = cascade == 0 ? counters[0] : 0;
            }
        }
    }

    void ShadowRenderer::Shutdown() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();
        vkDeviceWaitIdle(device);

        DestroyCascadeTargets();
        DestroyAtlasTargets();
        RHI::DestroyGpuImage(device, allocator, m_DummyHZB);
        RHI::DestroyComputePipeline(device, m_CullPipeline);

        for (uint32_t view = 0; view < kMaxShadowViews; ++view) {
            RHI::DestroyGpuBuffer(allocator, m_DrawBuffers[view]);
            RHI::DestroyGpuBuffer(allocator, m_CullUniforms[view]);
        }
        RHI::DestroyGpuBuffer(allocator, m_RetestFlags);
        RHI::DestroyGpuBuffer(allocator, m_Counters);

        if (m_Pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, m_Pipeline, nullptr);
            m_Pipeline = VK_NULL_HANDLE;
        }
        if (m_PipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
            m_PipelineLayout = VK_NULL_HANDLE;
        }
        if (m_RenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
        }
        for (VkSampler* sampler : {&m_ComparisonSampler, &m_DummySampler}) {
            if (*sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, *sampler, nullptr);
                *sampler = VK_NULL_HANDLE;
            }
        }
        if (m_DescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }
        for (VkDescriptorSetLayout* layout : {&m_DrawSetLayout, &m_CullSetLayout}) {
            if (*layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, *layout, nullptr);
                *layout = VK_NULL_HANDLE;
            }
        }
        m_Context = nullptr;
    }

} // namespace Renderer
} // namespace Core
