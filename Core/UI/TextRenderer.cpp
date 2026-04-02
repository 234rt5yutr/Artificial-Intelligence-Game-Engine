#include "TextRenderer.h"
#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/RHI/ShaderCompiler.h"

#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cstring>
#include <fstream>

namespace Core {
namespace UI {

// ============================================================================
// Embedded MSDF Shaders (fallback if files not found)
// ============================================================================

static const char* MSDF_VERTEX_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants {
    mat4 projectionMatrix;
    vec4 textColor;
    vec4 outlineColor;
    vec4 shadowColor;
    float outlineWidth;
    float shadowOffset;
    float distanceRange;
    float fontSize;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragDistanceRange;

void main() {
    gl_Position = pc.projectionMatrix * vec4(inPosition, 0.0, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor * pc.textColor;
    fragDistanceRange = pc.distanceRange / pc.fontSize;
}
)";

static const char* MSDF_FRAGMENT_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants {
    mat4 projectionMatrix;
    vec4 textColor;
    vec4 outlineColor;
    vec4 shadowColor;
    float outlineWidth;
    float shadowOffset;
    float distanceRange;
    float fontSize;
} pc;

layout(set = 0, binding = 0) uniform sampler2D fontAtlas;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragDistanceRange;

layout(location = 0) out vec4 outColor;

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

float screenPxDistance(float signedDist) {
    return signedDist * fragDistanceRange * pc.fontSize;
}

float getOpacity(float signedDist) {
    float pxDist = screenPxDistance(signedDist);
    float pxRange = length(vec2(dFdx(pxDist), dFdy(pxDist)));
    float edgeWidth = max(pxRange, 1.0);
    return clamp(pxDist / edgeWidth + 0.5, 0.0, 1.0);
}

void main() {
    vec3 msdf = texture(fontAtlas, fragTexCoord).rgb;
    float signedDist = median(msdf.r, msdf.g, msdf.b) - 0.5;
    float textOpacity = getOpacity(signedDist);
    
    vec4 result = vec4(fragColor.rgb, fragColor.a * textOpacity);
    
    if (pc.outlineWidth > 0.0) {
        float outlineSignedDist = signedDist + (pc.outlineWidth / pc.fontSize / fragDistanceRange);
        float outlineOpacity = getOpacity(outlineSignedDist);
        vec4 outlineResult = vec4(pc.outlineColor.rgb, pc.outlineColor.a * outlineOpacity);
        result = mix(outlineResult, result, textOpacity);
    }
    
    if (pc.shadowOffset > 0.0) {
        vec2 shadowUV = fragTexCoord - vec2(pc.shadowOffset / pc.fontSize * 0.01, -pc.shadowOffset / pc.fontSize * 0.01);
        vec3 shadowMsdf = texture(fontAtlas, shadowUV).rgb;
        float shadowSignedDist = median(shadowMsdf.r, shadowMsdf.g, shadowMsdf.b) - 0.5;
        float shadowOpacity = getOpacity(shadowSignedDist);
        vec4 shadowResult = vec4(pc.shadowColor.rgb, pc.shadowColor.a * shadowOpacity);
        float combinedOpacity = max(textOpacity, pc.outlineWidth > 0.0 ? getOpacity(signedDist + (pc.outlineWidth / pc.fontSize / fragDistanceRange)) : 0.0);
        result = mix(shadowResult, result, combinedOpacity);
    }
    
    outColor = result;
    
    if (outColor.a < 0.01) {
        discard;
    }
}
)";

// ============================================================================
// TextVertex Implementation
// ============================================================================

VkVertexInputBindingDescription TextVertex::GetBindingDescription() {
    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(TextVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::array<VkVertexInputAttributeDescription, 3> TextVertex::GetAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 3> attributes = {};

    // Position (vec2)
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = offsetof(TextVertex, position);

    // TexCoord (vec2)
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = offsetof(TextVertex, texCoord);

    // Color (vec4)
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[2].offset = offsetof(TextVertex, color);

    return attributes;
}

// ============================================================================
// TextRenderer Implementation
// ============================================================================

TextRenderer::~TextRenderer() {
    if (m_Initialized) {
        Shutdown();
    }
}

TextRenderer::TextRenderer(TextRenderer&& other) noexcept
    : m_Initialized(other.m_Initialized)
    , m_VulkanContext(other.m_VulkanContext)
    , m_ViewportSize(other.m_ViewportSize)
    , m_PipelineLayout(other.m_PipelineLayout)
    , m_Pipeline(other.m_Pipeline)
    , m_DescriptorSetLayout(other.m_DescriptorSetLayout)
    , m_DescriptorPool(other.m_DescriptorPool)
    , m_DescriptorSets(std::move(other.m_DescriptorSets))
    , m_VertexBuffer(other.m_VertexBuffer)
    , m_VertexAllocation(other.m_VertexAllocation)
    , m_VertexBufferCapacity(other.m_VertexBufferCapacity)
    , m_IndexBuffer(other.m_IndexBuffer)
    , m_IndexAllocation(other.m_IndexAllocation)
    , m_IndexBufferCapacity(other.m_IndexBufferCapacity)
    , m_PendingDraws(std::move(other.m_PendingDraws))
{
    other.m_Initialized = false;
    other.m_PipelineLayout = VK_NULL_HANDLE;
    other.m_Pipeline = VK_NULL_HANDLE;
    other.m_DescriptorSetLayout = VK_NULL_HANDLE;
    other.m_DescriptorPool = VK_NULL_HANDLE;
    other.m_VertexBuffer = VK_NULL_HANDLE;
    other.m_VertexAllocation = VK_NULL_HANDLE;
    other.m_IndexBuffer = VK_NULL_HANDLE;
    other.m_IndexAllocation = VK_NULL_HANDLE;
}

TextRenderer& TextRenderer::operator=(TextRenderer&& other) noexcept {
    if (this != &other) {
        if (m_Initialized) {
            Shutdown();
        }

        m_Initialized = other.m_Initialized;
        m_VulkanContext = other.m_VulkanContext;
        m_ViewportSize = other.m_ViewportSize;
        m_PipelineLayout = other.m_PipelineLayout;
        m_Pipeline = other.m_Pipeline;
        m_DescriptorSetLayout = other.m_DescriptorSetLayout;
        m_DescriptorPool = other.m_DescriptorPool;
        m_DescriptorSets = std::move(other.m_DescriptorSets);
        m_VertexBuffer = other.m_VertexBuffer;
        m_VertexAllocation = other.m_VertexAllocation;
        m_VertexBufferCapacity = other.m_VertexBufferCapacity;
        m_IndexBuffer = other.m_IndexBuffer;
        m_IndexAllocation = other.m_IndexAllocation;
        m_IndexBufferCapacity = other.m_IndexBufferCapacity;
        m_PendingDraws = std::move(other.m_PendingDraws);

        other.m_Initialized = false;
        other.m_PipelineLayout = VK_NULL_HANDLE;
        other.m_Pipeline = VK_NULL_HANDLE;
        other.m_DescriptorSetLayout = VK_NULL_HANDLE;
        other.m_DescriptorPool = VK_NULL_HANDLE;
        other.m_VertexBuffer = VK_NULL_HANDLE;
        other.m_VertexAllocation = VK_NULL_HANDLE;
        other.m_IndexBuffer = VK_NULL_HANDLE;
        other.m_IndexAllocation = VK_NULL_HANDLE;
    }
    return *this;
}

void TextRenderer::Initialize(RHI::VulkanContext* vulkanContext, VkRenderPass renderPass) {
    if (m_Initialized) {
        ENGINE_CORE_WARN("TextRenderer::Initialize - Already initialized");
        return;
    }

    m_VulkanContext = vulkanContext;

    // Create descriptor resources first
    if (!CreateDescriptorResources()) {
        ENGINE_CORE_ERROR("TextRenderer::Initialize - Failed to create descriptor resources");
        return;
    }

    // Create graphics pipeline
    if (!CreatePipeline(renderPass)) {
        ENGINE_CORE_ERROR("TextRenderer::Initialize - Failed to create pipeline");
        Shutdown();
        return;
    }

    // Create initial vertex/index buffers with reasonable capacity
    constexpr size_t INITIAL_VERTEX_CAPACITY = 4096;  // ~1000 glyphs
    constexpr size_t INITIAL_INDEX_CAPACITY = 6144;   // ~1000 glyphs
    if (!CreateBuffers(INITIAL_VERTEX_CAPACITY, INITIAL_INDEX_CAPACITY)) {
        ENGINE_CORE_ERROR("TextRenderer::Initialize - Failed to create buffers");
        Shutdown();
        return;
    }

    m_Initialized = true;
    ENGINE_CORE_INFO("TextRenderer initialized");
}

void TextRenderer::Shutdown() {
    if (!m_VulkanContext) {
        return;
    }

    VkDevice device = m_VulkanContext->GetDevice();
    vkDeviceWaitIdle(device);

    // Destroy buffers
    VmaAllocator allocator = m_VulkanContext->GetAllocator();
    if (m_VertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, m_VertexBuffer, m_VertexAllocation);
        m_VertexBuffer = VK_NULL_HANDLE;
        m_VertexAllocation = VK_NULL_HANDLE;
    }

    if (m_IndexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, m_IndexBuffer, m_IndexAllocation);
        m_IndexBuffer = VK_NULL_HANDLE;
        m_IndexAllocation = VK_NULL_HANDLE;
    }

    // Destroy pipeline
    if (m_Pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_Pipeline, nullptr);
        m_Pipeline = VK_NULL_HANDLE;
    }

    if (m_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
    }

    // Destroy descriptor resources
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }

    m_DescriptorSets.clear();
    m_PendingDraws.clear();
    m_Initialized = false;
    m_VulkanContext = nullptr;

    ENGINE_CORE_INFO("TextRenderer shut down");
}

// ============================================================================
// Text Drawing API
// ============================================================================

void TextRenderer::DrawText(std::string_view text, glm::vec2 position, const TextStyle& style) {
    if (!m_Initialized || text.empty()) {
        return;
    }

    // Resolve font atlas
    FontAtlas* fontAtlas = FontManager::Get().GetFont(style.fontFamily);
    if (!fontAtlas) {
        fontAtlas = FontManager::Get().GetDefaultFont();
    }
    if (!fontAtlas || !fontAtlas->IsLoaded()) {
        ENGINE_CORE_WARN("TextRenderer::DrawText - No font available: {}", style.fontFamily);
        return;
    }

    // Queue draw command
    TextDrawCommand cmd;
    cmd.text = std::string(text);
    cmd.position = position;
    cmd.style = style;
    cmd.fontAtlas = fontAtlas;
    m_PendingDraws.push_back(std::move(cmd));
}

void TextRenderer::DrawTextAnchored(std::string_view text, Anchor anchor, glm::vec2 offset, 
                                    const TextStyle& style) {
    if (!m_Initialized || text.empty()) {
        return;
    }

    // Calculate position from anchor
    glm::vec2 anchorPos = AnchorUtils::GetAnchorPosition(anchor, m_ViewportSize);
    glm::vec2 textSize = MeasureText(text, style);
    
    // Apply alignment based on anchor position
    HorizontalAlign hAlign = style.hAlign;
    VerticalAlign vAlign = style.vAlign;
    
    // Auto-adjust alignment based on anchor for convenience
    glm::vec2 normalizedAnchor = AnchorUtils::GetNormalizedAnchor(anchor);
    
    // Determine horizontal alignment from anchor
    if (normalizedAnchor.x < 0.25f) {
        hAlign = HorizontalAlign::Left;
    } else if (normalizedAnchor.x > 0.75f) {
        hAlign = HorizontalAlign::Right;
    } else {
        hAlign = HorizontalAlign::Center;
    }
    
    // Determine vertical alignment from anchor
    if (normalizedAnchor.y < 0.25f) {
        vAlign = VerticalAlign::Top;
    } else if (normalizedAnchor.y > 0.75f) {
        vAlign = VerticalAlign::Bottom;
    } else {
        vAlign = VerticalAlign::Center;
    }
    
    glm::vec2 position = AnchorUtils::CalculatePositionWithSize(
        anchor, offset, textSize, m_ViewportSize, hAlign, vAlign);
    
    // Create modified style with computed alignment
    TextStyle adjustedStyle = style;
    adjustedStyle.hAlign = HorizontalAlign::Left;  // Position already accounts for alignment
    adjustedStyle.vAlign = VerticalAlign::Top;
    
    DrawText(text, position, adjustedStyle);
}

void TextRenderer::DrawText3D(std::string_view text, glm::vec3 worldPosition, 
                               const glm::mat4& viewProj, const TextStyle& style, 
                               bool billboard) {
    if (!m_Initialized || text.empty()) {
        return;
    }
    
    // Project world position to clip space
    glm::vec4 clipPos = viewProj * glm::vec4(worldPosition, 1.0f);
    
    // Behind camera check
    if (clipPos.w <= 0.0f) {
        return;
    }
    
    // Perspective divide to NDC
    glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
    
    // Check if within visible frustum
    if (ndc.x < -1.0f || ndc.x > 1.0f || 
        ndc.y < -1.0f || ndc.y > 1.0f || 
        ndc.z < 0.0f || ndc.z > 1.0f) {
        return;
    }
    
    // Convert NDC to screen space
    // NDC: x=-1 is left, x=1 is right, y=-1 is bottom, y=1 is top
    // Screen: x=0 is left, y=0 is top (inverted Y)
    glm::vec2 screenPos;
    screenPos.x = (ndc.x + 1.0f) * 0.5f * m_ViewportSize.x;
    screenPos.y = (1.0f - ndc.y) * 0.5f * m_ViewportSize.y;  // Flip Y
    
    // Center the text at the projected position
    glm::vec2 textSize = MeasureText(text, style);
    screenPos.x -= textSize.x * 0.5f;
    screenPos.y -= textSize.y * 0.5f;
    
    // Optional: Scale text based on distance (billboard mode)
    TextStyle adjustedStyle = style;
    if (billboard) {
        // Distance-based scaling (closer = larger)
        float distance = clipPos.w;
        float scaleFactor = glm::clamp(1.0f / distance * 5.0f, 0.5f, 2.0f);
        adjustedStyle.fontSize = style.fontSize * scaleFactor;
    }
    
    DrawText(text, screenPos, adjustedStyle);
}

// ============================================================================
// Text Measurement
// ============================================================================

glm::vec2 TextRenderer::MeasureText(std::string_view text, const TextStyle& style) {
    // Resolve font atlas
    FontAtlas* fontAtlas = FontManager::Get().GetFont(style.fontFamily);
    if (!fontAtlas) {
        fontAtlas = FontManager::Get().GetDefaultFont();
    }
    if (!fontAtlas || !fontAtlas->IsLoaded()) {
        return {0.0f, 0.0f};
    }
    
    const auto& metrics = fontAtlas->GetMetrics();
    float scale = style.fontSize / metrics.emSize;
    
    // Calculate width using FontAtlas helper (in em units)
    float widthEm = fontAtlas->CalculateTextWidth(text);
    float width = widthEm * style.fontSize;
    
    // Height is based on line height
    float height = metrics.lineHeight * style.fontSize;
    
    return {width, height};
}

// ============================================================================
// Viewport Management
// ============================================================================

void TextRenderer::SetViewportSize(uint32_t width, uint32_t height) {
    m_ViewportSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
}

// ============================================================================
// Rendering
// ============================================================================

void TextRenderer::Flush(VkCommandBuffer cmdBuffer) {
    if (!m_Initialized || m_PendingDraws.empty()) {
        m_LastFrameDrawCalls = 0;
        m_LastFrameVertexCount = 0;
        return;
    }
    
    // Organize draws by font atlas
    std::unordered_map<FontAtlas*, TextBatch> batches;
    
    for (const auto& cmd : m_PendingDraws) {
        auto& batch = batches[cmd.fontAtlas];
        batch.fontAtlas = cmd.fontAtlas;
        GenerateTextVertices(cmd, batch.vertices, batch.indices);
    }
    
    // Calculate total vertices and indices needed
    size_t totalVertices = 0;
    size_t totalIndices = 0;
    for (const auto& [atlas, batch] : batches) {
        totalVertices += batch.vertices.size();
        totalIndices += batch.indices.size();
    }
    
    if (totalVertices == 0) {
        Clear();
        return;
    }
    
    // Ensure buffers are large enough
    if (totalVertices > m_VertexBufferCapacity || totalIndices > m_IndexBufferCapacity) {
        // Grow buffers with some headroom
        size_t newVertexCapacity = std::max(totalVertices * 2, m_VertexBufferCapacity * 2);
        size_t newIndexCapacity = std::max(totalIndices * 2, m_IndexBufferCapacity * 2);
        
        if (!CreateBuffers(newVertexCapacity, newIndexCapacity)) {
            ENGINE_CORE_ERROR("TextRenderer::Flush - Failed to resize buffers");
            Clear();
            return;
        }
    }
    
    // Bind pipeline
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    
    // Bind vertex buffer
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &m_VertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmdBuffer, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    // Render each batch
    size_t vertexOffset = 0;
    size_t indexOffset = 0;
    size_t drawCalls = 0;
    
    for (auto& [atlas, batch] : batches) {
        if (batch.vertices.empty()) continue;
        
        // Upload this batch's data
        UploadBatchData(batch.vertices, batch.indices);
        
        // Get or create descriptor set for this atlas
        VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(atlas);
        if (descriptorSet == VK_NULL_HANDLE) {
            ENGINE_CORE_WARN("TextRenderer::Flush - No descriptor set for font atlas");
            continue;
        }
        
        // Bind descriptor set
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                m_PipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        
        // For each unique style in the batch, we need separate push constants
        // For simplicity, we'll use the first draw command's style
        // In production, you might want to batch by style as well
        auto it = std::find_if(m_PendingDraws.begin(), m_PendingDraws.end(),
                               [atlas](const TextDrawCommand& cmd) { 
                                   return cmd.fontAtlas == atlas; 
                               });
        
        if (it != m_PendingDraws.end()) {
            TextPushConstants pc = {};
            pc.projectionMatrix = CreateOrthographicProjection();
            pc.textColor = it->style.color;
            pc.outlineColor = it->style.outlineColor;
            pc.shadowColor = it->style.shadowColor;
            pc.outlineWidth = it->style.outlineWidth;
            pc.shadowOffset = it->style.shadowOffset;
            pc.distanceRange = atlas->GetMetrics().distanceRange;
            pc.fontSize = it->style.fontSize;
            
            vkCmdPushConstants(cmdBuffer, m_PipelineLayout, 
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(TextPushConstants), &pc);
        }
        
        // Draw
        vkCmdDrawIndexed(cmdBuffer, static_cast<uint32_t>(batch.indices.size()), 
                         1, 0, 0, 0);
        
        drawCalls++;
    }
    
    m_LastFrameDrawCalls = drawCalls;
    m_LastFrameVertexCount = totalVertices;
    
    // Clear pending draws
    Clear();
}

void TextRenderer::Clear() {
    m_PendingDraws.clear();
}

// ============================================================================
// Pipeline Creation
// ============================================================================

bool TextRenderer::CreatePipeline(VkRenderPass renderPass) {
    VkDevice device = m_VulkanContext->GetDevice();
    
    // Compile shaders from embedded source
    auto vertSpirv = RHI::ShaderCompiler::CompileToSPIRV(
        MSDF_VERTEX_SHADER, RHI::ShaderStage::Vertex, "msdf_text.vert");
    auto fragSpirv = RHI::ShaderCompiler::CompileToSPIRV(
        MSDF_FRAGMENT_SHADER, RHI::ShaderStage::Fragment, "msdf_text.frag");
    
    if (vertSpirv.empty() || fragSpirv.empty()) {
        ENGINE_CORE_ERROR("TextRenderer::CreatePipeline - Failed to compile shaders");
        return false;
    }
    
    // Create shader modules
    VkShaderModule vertModule = m_VulkanContext->CreateShaderModule(vertSpirv);
    VkShaderModule fragModule = m_VulkanContext->CreateShaderModule(fragSpirv);
    
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        ENGINE_CORE_ERROR("TextRenderer::CreatePipeline - Failed to create shader modules");
        if (vertModule) m_VulkanContext->DestroyShaderModule(vertModule);
        if (fragModule) m_VulkanContext->DestroyShaderModule(fragModule);
        return false;
    }
    
    // Shader stages
    VkPipelineShaderStageCreateInfo vertStageInfo = {};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertModule;
    vertStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo fragStageInfo = {};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragModule;
    fragStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};
    
    // Vertex input
    auto bindingDesc = TextVertex::GetBindingDescription();
    auto attribDescs = TextVertex::GetAttributeDescriptions();
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attribDescs.data();
    
    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Viewport and scissor (dynamic)
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for 2D quads
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Depth stencil (disabled for 2D text)
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    // Color blending (alpha blend for text)
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Dynamic state
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // Push constants
    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TextPushConstants);
    
    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        ENGINE_CORE_ERROR("TextRenderer::CreatePipeline - Failed to create pipeline layout");
        m_VulkanContext->DestroyShaderModule(vertModule);
        m_VulkanContext->DestroyShaderModule(fragModule);
        return false;
    }
    
    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_PipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, 
                                                &pipelineInfo, nullptr, &m_Pipeline);
    
    // Clean up shader modules
    m_VulkanContext->DestroyShaderModule(vertModule);
    m_VulkanContext->DestroyShaderModule(fragModule);
    
    if (result != VK_SUCCESS) {
        ENGINE_CORE_ERROR("TextRenderer::CreatePipeline - Failed to create graphics pipeline");
        return false;
    }
    
    ENGINE_CORE_INFO("TextRenderer pipeline created successfully");
    return true;
}

bool TextRenderer::CreateDescriptorResources() {
    VkDevice device = m_VulkanContext->GetDevice();
    
    // Descriptor set layout - single sampler for font atlas
    VkDescriptorSetLayoutBinding samplerBinding = {};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
        ENGINE_CORE_ERROR("TextRenderer::CreateDescriptorResources - Failed to create descriptor set layout");
        return false;
    }
    
    // Descriptor pool - allow for multiple font atlases
    constexpr uint32_t MAX_FONT_ATLASES = 16;
    
    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAX_FONT_ATLASES;
    
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = MAX_FONT_ATLASES;
    
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        ENGINE_CORE_ERROR("TextRenderer::CreateDescriptorResources - Failed to create descriptor pool");
        return false;
    }
    
    return true;
}

VkDescriptorSet TextRenderer::GetOrCreateDescriptorSet(FontAtlas* atlas) {
    if (!atlas || !atlas->IsLoaded()) {
        return VK_NULL_HANDLE;
    }
    
    // Check cache
    auto it = m_DescriptorSets.find(atlas);
    if (it != m_DescriptorSets.end()) {
        return it->second;
    }
    
    // Allocate new descriptor set
    VkDevice device = m_VulkanContext->GetDevice();
    
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_DescriptorSetLayout;
    
    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        ENGINE_CORE_ERROR("TextRenderer::GetOrCreateDescriptorSet - Failed to allocate descriptor set");
        return VK_NULL_HANDLE;
    }
    
    // Update descriptor with atlas texture
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = atlas->GetAtlasImageView();
    imageInfo.sampler = atlas->GetAtlasSampler();
    
    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    
    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    
    // Cache and return
    m_DescriptorSets[atlas] = descriptorSet;
    return descriptorSet;
}

// ============================================================================
// Vertex Buffer Management
// ============================================================================

bool TextRenderer::CreateBuffers(size_t vertexCount, size_t indexCount) {
    VmaAllocator allocator = m_VulkanContext->GetAllocator();
    VkDevice device = m_VulkanContext->GetDevice();
    
    // Destroy old buffers
    if (m_VertexBuffer != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        vmaDestroyBuffer(allocator, m_VertexBuffer, m_VertexAllocation);
        m_VertexBuffer = VK_NULL_HANDLE;
    }
    
    if (m_IndexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, m_IndexBuffer, m_IndexAllocation);
        m_IndexBuffer = VK_NULL_HANDLE;
    }
    
    // Create vertex buffer
    VkBufferCreateInfo vertexBufferInfo = {};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.size = vertexCount * sizeof(TextVertex);
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo vertexAllocInfo = {};
    vertexAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;  // Host visible for dynamic updates
    
    if (vmaCreateBuffer(allocator, &vertexBufferInfo, &vertexAllocInfo, 
                        &m_VertexBuffer, &m_VertexAllocation, nullptr) != VK_SUCCESS) {
        ENGINE_CORE_ERROR("TextRenderer::CreateBuffers - Failed to create vertex buffer");
        return false;
    }
    m_VertexBufferCapacity = vertexCount;
    
    // Create index buffer
    VkBufferCreateInfo indexBufferInfo = {};
    indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indexBufferInfo.size = indexCount * sizeof(uint32_t);
    indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    indexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo indexAllocInfo = {};
    indexAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    
    if (vmaCreateBuffer(allocator, &indexBufferInfo, &indexAllocInfo, 
                        &m_IndexBuffer, &m_IndexAllocation, nullptr) != VK_SUCCESS) {
        ENGINE_CORE_ERROR("TextRenderer::CreateBuffers - Failed to create index buffer");
        vmaDestroyBuffer(allocator, m_VertexBuffer, m_VertexAllocation);
        m_VertexBuffer = VK_NULL_HANDLE;
        return false;
    }
    m_IndexBufferCapacity = indexCount;
    
    return true;
}

void TextRenderer::UploadBatchData(const std::vector<TextVertex>& vertices, 
                                   const std::vector<uint32_t>& indices) {
    VmaAllocator allocator = m_VulkanContext->GetAllocator();
    
    // Upload vertices
    if (!vertices.empty()) {
        void* data;
        vmaMapMemory(allocator, m_VertexAllocation, &data);
        memcpy(data, vertices.data(), vertices.size() * sizeof(TextVertex));
        vmaUnmapMemory(allocator, m_VertexAllocation);
    }
    
    // Upload indices
    if (!indices.empty()) {
        void* data;
        vmaMapMemory(allocator, m_IndexAllocation, &data);
        memcpy(data, indices.data(), indices.size() * sizeof(uint32_t));
        vmaUnmapMemory(allocator, m_IndexAllocation);
    }
}

// ============================================================================
// Text Layout
// ============================================================================

void TextRenderer::GenerateTextVertices(const TextDrawCommand& cmd, 
                                         std::vector<TextVertex>& outVertices,
                                         std::vector<uint32_t>& outIndices) {
    FontAtlas* atlas = cmd.fontAtlas;
    if (!atlas || !atlas->IsLoaded()) {
        return;
    }
    
    const auto& metrics = atlas->GetMetrics();
    float scale = cmd.style.fontSize / metrics.emSize;
    
    // Measure text for alignment
    glm::vec2 textSize = MeasureText(cmd.text, cmd.style);
    glm::vec2 position = ApplyAlignment(cmd.position, textSize, 
                                        cmd.style.hAlign, cmd.style.vAlign);
    
    float cursorX = position.x;
    float baselineY = position.y + metrics.ascender * cmd.style.fontSize;
    
    uint32_t prevChar = 0;
    uint32_t baseIndex = static_cast<uint32_t>(outVertices.size());
    
    // Iterate through UTF-8 text
    for (size_t i = 0; i < cmd.text.size();) {
        uint32_t codepoint = 0;
        size_t consumed = DecodeUTF8(cmd.text, i, codepoint);
        i += consumed;
        
        // Handle newlines
        if (codepoint == '\n') {
            cursorX = position.x;
            baselineY += metrics.lineHeight * cmd.style.fontSize;
            prevChar = 0;
            continue;
        }
        
        // Handle tabs
        if (codepoint == '\t') {
            const GlyphMetrics* spaceGlyph = atlas->GetGlyph(' ');
            if (spaceGlyph) {
                cursorX += spaceGlyph->advance * cmd.style.fontSize * 4.0f;
            }
            prevChar = codepoint;
            continue;
        }
        
        // Skip non-printable characters
        if (codepoint < 32) {
            continue;
        }
        
        // Apply kerning
        if (prevChar != 0) {
            float kerning = atlas->GetKerning(prevChar, codepoint);
            cursorX += kerning * cmd.style.fontSize;
        }
        
        // Get glyph metrics
        const GlyphMetrics* glyph = atlas->GetGlyph(codepoint);
        if (!glyph) {
            // Try fallback to '?' for missing glyphs
            glyph = atlas->GetGlyph('?');
            if (!glyph) {
                cursorX += cmd.style.fontSize * 0.5f;  // Default advance
                prevChar = codepoint;
                continue;
            }
        }
        
        // Calculate quad corners
        // Plane bounds are in em units relative to baseline
        float left = cursorX + glyph->planeLeft * cmd.style.fontSize;
        float right = cursorX + glyph->planeRight * cmd.style.fontSize;
        float top = baselineY - glyph->planeTop * cmd.style.fontSize;    // Flip Y (top is smaller)
        float bottom = baselineY - glyph->planeBottom * cmd.style.fontSize;
        
        // UV coordinates from atlas bounds (already normalized 0-1)
        float uvLeft = glyph->atlasLeft;
        float uvRight = glyph->atlasRight;
        float uvTop = 1.0f - glyph->atlasTop;     // Flip V for Vulkan
        float uvBottom = 1.0f - glyph->atlasBottom;
        
        // Generate 4 vertices for quad
        glm::vec4 color = cmd.style.color;
        
        uint32_t vertexIndex = static_cast<uint32_t>(outVertices.size());
        
        // Top-left
        outVertices.push_back({
            {left, top},
            {uvLeft, uvTop},
            color
        });
        
        // Top-right
        outVertices.push_back({
            {right, top},
            {uvRight, uvTop},
            color
        });
        
        // Bottom-right
        outVertices.push_back({
            {right, bottom},
            {uvRight, uvBottom},
            color
        });
        
        // Bottom-left
        outVertices.push_back({
            {left, bottom},
            {uvLeft, uvBottom},
            color
        });
        
        // Generate indices for two triangles
        // Triangle 1: TL, TR, BR
        outIndices.push_back(vertexIndex + 0);
        outIndices.push_back(vertexIndex + 1);
        outIndices.push_back(vertexIndex + 2);
        
        // Triangle 2: TL, BR, BL
        outIndices.push_back(vertexIndex + 0);
        outIndices.push_back(vertexIndex + 2);
        outIndices.push_back(vertexIndex + 3);
        
        // Advance cursor
        cursorX += glyph->advance * cmd.style.fontSize;
        prevChar = codepoint;
    }
}

glm::vec2 TextRenderer::ApplyAlignment(glm::vec2 position, glm::vec2 textSize, 
                                       HorizontalAlign hAlign, VerticalAlign vAlign) {
    glm::vec2 result = position;
    
    // Horizontal alignment
    switch (hAlign) {
        case HorizontalAlign::Left:
            // No adjustment needed
            break;
        case HorizontalAlign::Center:
            result.x -= textSize.x * 0.5f;
            break;
        case HorizontalAlign::Right:
            result.x -= textSize.x;
            break;
    }
    
    // Vertical alignment
    switch (vAlign) {
        case VerticalAlign::Top:
            // No adjustment needed
            break;
        case VerticalAlign::Center:
            result.y -= textSize.y * 0.5f;
            break;
        case VerticalAlign::Bottom:
            result.y -= textSize.y;
            break;
    }
    
    return result;
}

size_t TextRenderer::DecodeUTF8(std::string_view text, size_t offset, uint32_t& outCodepoint) {
    if (offset >= text.size()) {
        outCodepoint = 0;
        return 0;
    }
    
    unsigned char c = static_cast<unsigned char>(text[offset]);
    
    if ((c & 0x80) == 0) {
        // ASCII (1 byte)
        outCodepoint = c;
        return 1;
    } else if ((c & 0xE0) == 0xC0) {
        // 2-byte UTF-8
        outCodepoint = (c & 0x1F) << 6;
        if (offset + 1 < text.size()) {
            outCodepoint |= (static_cast<unsigned char>(text[offset + 1]) & 0x3F);
        }
        return 2;
    } else if ((c & 0xF0) == 0xE0) {
        // 3-byte UTF-8
        outCodepoint = (c & 0x0F) << 12;
        if (offset + 1 < text.size()) {
            outCodepoint |= (static_cast<unsigned char>(text[offset + 1]) & 0x3F) << 6;
        }
        if (offset + 2 < text.size()) {
            outCodepoint |= (static_cast<unsigned char>(text[offset + 2]) & 0x3F);
        }
        return 3;
    } else if ((c & 0xF8) == 0xF0) {
        // 4-byte UTF-8
        outCodepoint = (c & 0x07) << 18;
        if (offset + 1 < text.size()) {
            outCodepoint |= (static_cast<unsigned char>(text[offset + 1]) & 0x3F) << 12;
        }
        if (offset + 2 < text.size()) {
            outCodepoint |= (static_cast<unsigned char>(text[offset + 2]) & 0x3F) << 6;
        }
        if (offset + 3 < text.size()) {
            outCodepoint |= (static_cast<unsigned char>(text[offset + 3]) & 0x3F);
        }
        return 4;
    }
    
    // Invalid UTF-8, skip byte
    outCodepoint = 0xFFFD;  // Replacement character
    return 1;
}

glm::mat4 TextRenderer::CreateOrthographicProjection() const {
    // Create orthographic projection for screen-space rendering
    // Origin at top-left, Y increases downward (standard screen coordinates)
    return glm::ortho(
        0.0f, m_ViewportSize.x,     // Left, Right
        m_ViewportSize.y, 0.0f,     // Bottom, Top (flipped for screen coords)
        -1.0f, 1.0f                  // Near, Far
    );
}

} // namespace UI
} // namespace Core
