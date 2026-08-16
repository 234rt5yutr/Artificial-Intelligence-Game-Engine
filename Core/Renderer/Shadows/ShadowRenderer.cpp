#include "ShadowRenderer.h"

#include "Core/Log.h"
#include "Core/Renderer/RenderMath.h"
#include "Core/RHI/ShaderCompiler.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Renderer/GPUDriven/ClusterCullShader.h"
#include "Core/Renderer/GPUDriven/GPUScene.h"
#include "Core/ECS/Systems/RenderSystem.h"
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

        // Same attachments, LOAD instead of CLEAR, so a partial cascade redraw
        // keeps the pages it is not touching. Load/store ops do not affect
        // render pass compatibility, so the pipeline built above works with both.
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        if (vkCreateRenderPass(m_Context->GetDevice(), &info, nullptr, &m_RenderPassLoad) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ShadowRenderer: cached-cascade render pass creation failed");
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

        const std::string shadowCullSource = ResolvedClusterCullShader();
        m_CullPipeline = RHI::CreateComputePipeline(device, m_Context->GetPipelineCache(),
                                                    shadowCullSource.c_str(), "shadow_cluster_cull",
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

        // Page grid. A resolution change reshapes it, so every cached page is
        // meaningless and the next frame redraws in full.
        m_PagesPerSide = std::clamp(std::max(1u, resolution / kShadowPageSize),
                                    1u, kMaxShadowPagesPerSide);
        for (uint32_t cascade = 0; cascade < kMaxShadowCascades; ++cascade) {
            m_CascadePages[cascade].DirtyPages.assign(
                static_cast<std::size_t>(m_PagesPerSide) * m_PagesPerSide, 1u);
            m_CascadePages[cascade].EverDrawn = false;
            m_CascadePages[cascade].LastMatrix = Math::Mat4(0.0f);
        }
        m_PreviousOccluders.clear();
        m_ForceFullRedraw = true;
        m_Stats.PagesPerSide = m_PagesPerSide;
        m_Stats.TotalPages = m_PagesPerSide * m_PagesPerSide * cascades;

        m_TargetsDirty = false;
        return true;
    }

    void ShadowRenderer::MarkPagesForBounds(uint32_t cascade, const Math::Vec3& center, float radius) {
        if (m_PagesPerSide == 0 || cascade >= kMaxShadowCascades) {
            return;
        }
        auto& pages = m_CascadePages[cascade].DirtyPages;
        if (pages.empty()) {
            return;
        }

        // Project the occluder's world bounding sphere into the cascade and take
        // the page rectangle it covers. Conservative on purpose: a page that
        // might have changed is redrawn, never skipped.
        const Math::Mat4& matrix = m_CascadeMatrices[cascade];
        Math::Vec2 uvMin(1e9f);
        Math::Vec2 uvMax(-1e9f);

        for (int corner = 0; corner < 8; ++corner) {
            const Math::Vec3 point(center.x + ((corner & 1) ? radius : -radius),
                                   center.y + ((corner & 2) ? radius : -radius),
                                   center.z + ((corner & 4) ? radius : -radius));
            const Math::Vec4 clip = matrix * Math::Vec4(point, 1.0f);
            if (clip.w <= 1e-5f) {
                // Straddles the light's near plane; cannot bound it, so treat
                // the whole cascade as dirty rather than guess.
                std::fill(pages.begin(), pages.end(), 1u);
                return;
            }
            const Math::Vec3 ndc = Math::Vec3(clip) / clip.w;
            const Math::Vec2 uv = Math::Vec2(ndc) * 0.5f + 0.5f;
            uvMin = glm::min(uvMin, uv);
            uvMax = glm::max(uvMax, uv);
        }

        uvMin = glm::clamp(uvMin, Math::Vec2(0.0f), Math::Vec2(1.0f));
        uvMax = glm::clamp(uvMax, Math::Vec2(0.0f), Math::Vec2(1.0f));
        if (uvMax.x < uvMin.x || uvMax.y < uvMin.y) {
            return;
        }

        const float pages_f = static_cast<float>(m_PagesPerSide);
        const int32_t minX = std::max(0, static_cast<int32_t>(std::floor(uvMin.x * pages_f)) - 1);
        const int32_t minY = std::max(0, static_cast<int32_t>(std::floor(uvMin.y * pages_f)) - 1);
        const int32_t maxX = std::min(static_cast<int32_t>(m_PagesPerSide) - 1,
                                      static_cast<int32_t>(std::floor(uvMax.x * pages_f)) + 1);
        const int32_t maxY = std::min(static_cast<int32_t>(m_PagesPerSide) - 1,
                                      static_cast<int32_t>(std::floor(uvMax.y * pages_f)) + 1);

        for (int32_t y = minY; y <= maxY; ++y) {
            for (int32_t x = minX; x <= maxX; ++x) {
                pages[static_cast<std::size_t>(y) * m_PagesPerSide + x] = 1u;
            }
        }
    }

    void ShadowRenderer::UpdateCascadePages(const FrameRenderData& frame, GPUScene& scene) {
        const uint32_t cascades = std::clamp(m_Settings.CascadeCount, 1u, kMaxShadowCascades);

        // A cascade whose matrix moved has nothing reusable in it.
        for (uint32_t cascade = 0; cascade < cascades; ++cascade) {
            auto& state = m_CascadePages[cascade];
            const bool matrixChanged = state.LastMatrix != m_CascadeMatrices[cascade];
            if (m_ForceFullRedraw || matrixChanged || !state.EverDrawn ||
                !m_Settings.CacheCascades) {
                std::fill(state.DirtyPages.begin(), state.DirtyPages.end(), 1u);
            }
            state.LastMatrix = m_CascadeMatrices[cascade];
        }

        // Identity by content hash rather than by index: the draw list is
        // rebuilt every frame and its order is not stable, so an index tells you
        // nothing about whether the same object is still there.
        m_CurrentOccluders.clear();
        for (const auto& command : frame.DrawCommands) {
            if (!command.Mesh || !command.CastShadows) {
                continue;
            }
            const GpuMeshRecord* record = scene.EnsureResident(command.Mesh);
            if (!record) {
                continue;
            }

            uint64_t hash = reinterpret_cast<uint64_t>(command.Mesh);
            const float* matrix = &command.Transform[0][0];
            for (int i = 0; i < 16; ++i) {
                uint32_t bits = 0;
                std::memcpy(&bits, &matrix[i], sizeof(bits));
                hash ^= static_cast<uint64_t>(bits) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
            }

            OccluderBounds bounds;
            bounds.Center = Math::Vec3(command.Transform *
                                       Math::Vec4(Math::Vec3(record->BoundsCenterRadius), 1.0f));
            const float scale = std::max({glm::length(Math::Vec3(command.Transform[0])),
                                          glm::length(Math::Vec3(command.Transform[1])),
                                          glm::length(Math::Vec3(command.Transform[2]))});
            bounds.Radius = record->BoundsCenterRadius.w * scale;
            m_CurrentOccluders[hash] = bounds;
        }

        uint32_t moved = 0;
        // Appeared or moved: its new footprint has to be drawn.
        for (const auto& [hash, bounds] : m_CurrentOccluders) {
            if (m_PreviousOccluders.find(hash) != m_PreviousOccluders.end()) {
                continue;
            }
            ++moved;
            for (uint32_t cascade = 0; cascade < cascades; ++cascade) {
                MarkPagesForBounds(cascade, bounds.Center, bounds.Radius);
            }
        }
        // Disappeared or moved: the shadow it left behind has to be cleared, so
        // its *old* footprint is dirty too. Missing this is what leaves a
        // shadow standing where the object used to be.
        for (const auto& [hash, bounds] : m_PreviousOccluders) {
            if (m_CurrentOccluders.find(hash) != m_CurrentOccluders.end()) {
                continue;
            }
            ++moved;
            for (uint32_t cascade = 0; cascade < cascades; ++cascade) {
                MarkPagesForBounds(cascade, bounds.Center, bounds.Radius);
            }
        }

        m_PreviousOccluders = m_CurrentOccluders;
        m_ForceFullRedraw = false;
        m_Stats.MovedInstances = moved;
        m_Stats.TotalOccluderChanges += moved;

        uint32_t dirty = 0;
        for (uint32_t cascade = 0; cascade < cascades; ++cascade) {
            for (uint8_t page : m_CascadePages[cascade].DirtyPages) {
                dirty += page ? 1u : 0u;
            }
        }
        m_Stats.DirtyPages = dirty;
        m_Stats.TotalPages = m_PagesPerSide * m_PagesPerSide * cascades;
    }

    void ShadowRenderer::BuildDirtyRects(uint32_t cascade, std::vector<VkRect2D>& outRects) const {
        outRects.clear();
        if (cascade >= kMaxShadowCascades || m_PagesPerSide == 0) {
            return;
        }
        const auto& pages = m_CascadePages[cascade].DirtyPages;
        if (pages.empty()) {
            return;
        }

        // Contiguous runs within a page row. Cheap, and it collapses the common
        // case - one object's footprint - into a handful of rectangles.
        const int32_t pageTexels = static_cast<int32_t>(m_Stats.Resolution / m_PagesPerSide);
        for (uint32_t y = 0; y < m_PagesPerSide; ++y) {
            uint32_t x = 0;
            while (x < m_PagesPerSide) {
                if (!pages[static_cast<std::size_t>(y) * m_PagesPerSide + x]) {
                    ++x;
                    continue;
                }
                const uint32_t runStart = x;
                while (x < m_PagesPerSide && pages[static_cast<std::size_t>(y) * m_PagesPerSide + x]) {
                    ++x;
                }
                VkRect2D rect{};
                rect.offset = {static_cast<int32_t>(runStart) * pageTexels,
                               static_cast<int32_t>(y) * pageTexels};
                rect.extent = {static_cast<uint32_t>((x - runStart) * pageTexels),
                               static_cast<uint32_t>(pageTexels)};
                outRects.push_back(rect);
            }
        }
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

        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        if (!Math::ExtractNearFar(frame.Projection, nearPlane, farPlane)) {
            nearPlane = 0.1f;
            farPlane = 1000.0f;
        }
        // Cascades follow the shadow distance, not the camera's far plane, which
        // is usually far enough out to spend every cascade on empty sky.
        farPlane = std::min(farPlane, std::max(nearPlane + 1.0f, m_Settings.MaxShadowDistance));

        // Fit against the *unjittered* projection. The temporal upscaler offsets
        // [2][0] and [2][1] by a sub-pixel amount every frame; left in, that
        // moves every frustum corner slightly, which reshapes the cascade, which
        // invalidates the whole page cache every single frame. The shadow map
        // does not want the jitter anyway - it is a main-view sampling trick.
        Math::Mat4 unjitteredProjection = frame.Projection;
        unjitteredProjection[2][0] = 0.0f;
        unjitteredProjection[2][1] = 0.0f;

        // Unproject the full frustum once; each cascade is a slice along it.
        const Math::Mat4 inverseViewProjection =
            glm::inverse(unjitteredProjection * frame.View);
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
        float splits[kMaxShadowCascades] = {};
        ComputeCascadeSplits(nearPlane, farPlane, cascades, m_Settings.CascadeSplitLambda, splits);
        float previousSplit = nearPlane;

        for (uint32_t cascade = 0; cascade < cascades; ++cascade) {
            const float split = splits[cascade];
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

    void ShadowRenderer::FitPunctualShadows(const FrameRenderData& frame) {
        m_SpotSlots.clear();
        m_PointSlots.clear();
        m_Stats.SpotShadowCount = 0;
        m_Stats.PointShadowCount = 0;
        m_Stats.AtlasTilesUsed = 0;

        const uint32_t tilesPerRow = std::max(1u, m_Settings.AtlasTilesPerRow);
        const uint32_t tileCapacity = tilesPerRow * tilesPerRow;
        m_Stats.AtlasTilesTotal = tileCapacity;

        if (m_Atlas.Image == VK_NULL_HANDLE) {
            return;
        }

        // Rank both punctual kinds together by how much of the screen the light
        // plausibly covers - intensity and reach over distance - so tiles go to
        // the lights a viewer is most likely to notice rather than to whichever
        // kind happens to be enumerated first.
        struct Candidate {
            int32_t LightIndex;
            float Score;
            bool IsPoint;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(frame.SpotLights.size() + frame.PointLights.size());

        auto scoreFor = [&](const Math::Vec3& position, float intensity, float radius) {
            const float distance = std::max(0.5f, glm::length(position - frame.CameraPosition));
            return (intensity * radius) / distance;
        };

        if (m_Settings.SpotShadowsEnabled) {
            for (std::size_t i = 0; i < frame.SpotLights.size() && i < kMaxSpotShadows; ++i) {
                const auto& light = frame.SpotLights[i];
                if (light.CastShadows <= 0.5f || light.Radius <= 0.0f) {
                    continue;
                }
                candidates.push_back({static_cast<int32_t>(i),
                                      scoreFor(light.Position, light.Intensity, light.Radius), false});
            }
        }
        if (m_Settings.PointShadowsEnabled) {
            for (std::size_t i = 0; i < frame.PointLights.size() && i < kMaxPointShadows; ++i) {
                const auto& light = frame.PointLights[i];
                if (light.CastShadows <= 0.5f || light.Radius <= 0.0f) {
                    continue;
                }
                candidates.push_back({static_cast<int32_t>(i),
                                      scoreFor(light.Position, light.Intensity, light.Radius), true});
            }
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& lhs, const Candidate& rhs) { return lhs.Score > rhs.Score; });

        // Tile-run allocator. A point light needs six contiguous tiles; a light
        // that does not fit is skipped whole, because a partial cube reads as
        // fully lit on its missing faces - worse than no shadow at all.
        uint32_t nextTile = 0;

        for (const Candidate& candidate : candidates) {
            if (candidate.IsPoint) {
                if (m_PointSlots.size() >= kMaxPointShadows ||
                    nextTile + kCubeFaceCount > tileCapacity) {
                    continue;
                }
                const auto& light = frame.PointLights[candidate.LightIndex];

                PointShadowSlot slot;
                slot.LightIndex = candidate.LightIndex;
                slot.BaseTile = nextTile;

                // The six standard cube directions, in the order the shader's
                // major-axis pick produces: +X, -X, +Y, -Y, +Z, -Z.
                const Math::Vec3 faceDirections[kCubeFaceCount] = {
                    Math::Vec3( 1.0f,  0.0f,  0.0f), Math::Vec3(-1.0f,  0.0f,  0.0f),
                    Math::Vec3( 0.0f,  1.0f,  0.0f), Math::Vec3( 0.0f, -1.0f,  0.0f),
                    Math::Vec3( 0.0f,  0.0f,  1.0f), Math::Vec3( 0.0f,  0.0f, -1.0f),
                };
                // Up vectors chosen so no face's forward is parallel to its up,
                // which would make lookAt degenerate.
                const Math::Vec3 faceUps[kCubeFaceCount] = {
                    Math::Vec3(0.0f, 1.0f, 0.0f), Math::Vec3(0.0f, 1.0f, 0.0f),
                    Math::Vec3(0.0f, 0.0f, 1.0f), Math::Vec3(0.0f, 0.0f, 1.0f),
                    Math::Vec3(0.0f, 1.0f, 0.0f), Math::Vec3(0.0f, 1.0f, 0.0f),
                };

                // 90 degrees exactly: anything wider overlaps the neighbouring
                // face, anything narrower leaves a gap along the seam.
                Math::Mat4 faceProjection = glm::perspective(
                    1.5707963f, 1.0f, 0.05f, std::max(light.Radius, 0.1f));
                faceProjection[1][1] *= -1.0f;

                for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
                    const Math::Mat4 faceView =
                        glm::lookAt(light.Position, light.Position + faceDirections[face], faceUps[face]);
                    slot.FaceViewProjection[face] = faceProjection * faceView;
                }

                m_PointSlots.push_back(slot);
                nextTile += kCubeFaceCount;
                continue;
            }

            if (m_SpotSlots.size() >= kMaxSpotShadows || nextTile >= tileCapacity) {
                continue;
            }
            const auto& light = frame.SpotLights[candidate.LightIndex];

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
            slot.LightIndex = candidate.LightIndex;
            slot.Tile = nextTile;
            m_SpotSlots.push_back(slot);
            ++nextTile;
        }

        m_Stats.SpotShadowCount = static_cast<uint32_t>(m_SpotSlots.size());
        m_Stats.PointShadowCount = static_cast<uint32_t>(m_PointSlots.size());
        m_Stats.AtlasTilesUsed = nextTile;
    }

    bool ShadowRenderer::BeginFrame(const FrameRenderData& frame, GPUScene& scene) {
        m_Stats.Active = false;
        m_ShadowLightIndex = -1;
        m_Stats.ShadowLightIndex = -1;
        m_Stats.SpotShadowCount = 0;
        m_Stats.PointShadowCount = 0;
        m_SpotSlots.clear();
        m_PointSlots.clear();
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

        // Page invalidation has to follow the fit: it compares this frame's
        // cascade matrices against last frame's, and projects occluders through
        // them.
        if (m_ShadowLightIndex >= 0) {
            UpdateCascadePages(frame, scene);
        }

        // Punctual tiles are independent of the directional cascades: a scene
        // with no sun can still have shadowed spots and points.
        FitPunctualShadows(frame);

        m_Stats.ShadowLightIndex = m_ShadowLightIndex;
        m_Stats.Active = m_ClusterSlots > 0 &&
                         (m_ShadowLightIndex >= 0 || !m_SpotSlots.empty() || !m_PointSlots.empty());
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
        const bool hasPunctual = !m_SpotSlots.empty() || !m_PointSlots.empty();
        if (!hasCascades && !hasPunctual) {
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
        // Cube views come after the spot views in the same slot space, six per
        // light, so a view index maps to exactly one draw buffer.
        for (std::size_t slot = 0; slot < m_PointSlots.size(); ++slot) {
            for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
                const uint32_t viewIndex = kMaxShadowCascades + kMaxSpotShadows +
                                           static_cast<uint32_t>(slot) * kCubeFaceCount + face;
                DispatchCull(cmd, scene, viewIndex,
                             m_PointSlots[slot].FaceViewProjection[face], m_LightDirection);
            }
        }

        VkClearValue clear{};
        clear.depthStencil = {1.0f, 0};

        // Same draw, but the viewport still covers the whole cascade while the
        // scissor restricts it to the dirty region - the projection must not
        // change, or the depth written would not line up with what is already
        // in the untouched pages.
        auto recordViewScissored = [&](uint32_t viewIndex, const Math::Mat4& viewProjection,
                                       const VkRect2D& scissorRect) {
            VkViewport viewport{};
            viewport.width = static_cast<float>(m_Stats.Resolution);
            viewport.height = static_cast<float>(m_Stats.Resolution);
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissorRect);

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
            vkCmdDrawIndexedIndirect(cmd, m_DrawBuffers[viewIndex].Buffer, 0, m_ClusterSlots, 20);
        };

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

        m_Stats.CascadesRedrawn = 0;
        m_Stats.CascadesSkipped = 0;
        m_Stats.DirtyRects = 0;

        const uint32_t pagesPerCascade = m_PagesPerSide * m_PagesPerSide;
        std::vector<VkRect2D> dirtyRects;

        for (uint32_t cascade = 0; cascade < cascades; ++cascade) {
            auto& pageState = m_CascadePages[cascade];

            uint32_t dirtyPages = 0;
            for (uint8_t page : pageState.DirtyPages) {
                dirtyPages += page ? 1u : 0u;
            }

            // Nothing in this cascade changed: last frame's depth is still
            // correct, so there is nothing to do at all.
            if (m_Settings.CacheCascades && dirtyPages == 0 && pageState.EverDrawn) {
                ++m_Stats.CascadesSkipped;
                ++m_Stats.TotalCascadeSkips;
                continue;
            }

            BuildDirtyRects(cascade, dirtyRects);
            const bool fullRedraw =
                !m_Settings.CacheCascades || !pageState.EverDrawn ||
                pagesPerCascade == 0 ||
                static_cast<float>(dirtyPages) / static_cast<float>(pagesPerCascade) >=
                    m_Settings.CascadeFullRedrawFraction ||
                dirtyRects.size() > m_Settings.MaxCascadeDirtyRects;

            VkRenderPassBeginInfo passBegin{};
            passBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            // A full redraw clears; a partial one must load, or the pages it is
            // not touching would come back as cleared depth.
            passBegin.renderPass = fullRedraw ? m_RenderPass : m_RenderPassLoad;
            passBegin.framebuffer = m_Framebuffers[cascade];
            passBegin.renderArea.extent = {m_Stats.Resolution, m_Stats.Resolution};
            passBegin.clearValueCount = fullRedraw ? 1u : 0u;
            passBegin.pClearValues = fullRedraw ? &clear : nullptr;

            vkCmdBeginRenderPass(cmd, &passBegin, VK_SUBPASS_CONTENTS_INLINE);
            if (fullRedraw) {
                recordView(cascade, m_CascadeMatrices[cascade], 0, 0, m_Stats.Resolution);
            } else {
                // One draw per dirty rectangle, scissored to it. The geometry is
                // the same each time; the scissor is what makes it cheap.
                for (const VkRect2D& rect : dirtyRects) {
                    recordViewScissored(cascade, m_CascadeMatrices[cascade], rect);
                }
                m_Stats.DirtyRects += static_cast<uint32_t>(dirtyRects.size());
            }
            vkCmdEndRenderPass(cmd);

            std::fill(pageState.DirtyPages.begin(), pageState.DirtyPages.end(), 0u);
            pageState.EverDrawn = true;
            ++m_Stats.CascadesRedrawn;
            ++m_Stats.TotalCascadeRedraws;
        }
        if (cascades > 0) {
            m_CascadeArray.Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }

        // The atlas is one render pass instance for every tile: cleared once,
        // then each light draws into its own viewport and scissor.
        if (hasPunctual && m_AtlasFramebuffer != VK_NULL_HANDLE) {
            VkRenderPassBeginInfo passBegin{};
            passBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            passBegin.renderPass = m_RenderPass;
            passBegin.framebuffer = m_AtlasFramebuffer;
            passBegin.renderArea.extent = {m_Stats.AtlasResolution, m_Stats.AtlasResolution};
            passBegin.clearValueCount = 1;
            passBegin.pClearValues = &clear;

            vkCmdBeginRenderPass(cmd, &passBegin, VK_SUBPASS_CONTENTS_INLINE);
            const uint32_t tilesPerRow = std::max(1u, m_Settings.AtlasTilesPerRow);
            auto tileOrigin = [&](uint32_t tile, int32_t& x, int32_t& y) {
                x = static_cast<int32_t>((tile % tilesPerRow) * m_Stats.AtlasTileSize);
                y = static_cast<int32_t>((tile / tilesPerRow) * m_Stats.AtlasTileSize);
            };

            for (std::size_t slot = 0; slot < m_SpotSlots.size(); ++slot) {
                const SpotShadowSlot& spot = m_SpotSlots[slot];
                int32_t x = 0;
                int32_t y = 0;
                tileOrigin(spot.Tile, x, y);
                recordView(kMaxShadowCascades + static_cast<uint32_t>(slot), spot.ViewProjection,
                           x, y, m_Stats.AtlasTileSize);
            }
            for (std::size_t slot = 0; slot < m_PointSlots.size(); ++slot) {
                const PointShadowSlot& point = m_PointSlots[slot];
                for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
                    const uint32_t viewIndex = kMaxShadowCascades + kMaxSpotShadows +
                                               static_cast<uint32_t>(slot) * kCubeFaceCount + face;
                    int32_t x = 0;
                    int32_t y = 0;
                    tileOrigin(point.BaseTile + face, x, y);
                    recordView(viewIndex, point.FaceViewProjection[face], x, y, m_Stats.AtlasTileSize);
                }
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
        for (VkRenderPass* pass : {&m_RenderPass, &m_RenderPassLoad}) {
            if (*pass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, *pass, nullptr);
                *pass = VK_NULL_HANDLE;
            }
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
