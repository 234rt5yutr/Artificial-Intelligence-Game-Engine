#include "FontAtlas.h"
#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <cstring>

// For image loading (using stb_image)
#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#endif
#include <stb_image.h>

namespace Core {
namespace UI {

FontAtlas::~FontAtlas() {
    if (m_Loaded) {
        Unload();
    }
}

FontAtlas::FontAtlas(FontAtlas&& other) noexcept
    : m_Loaded(other.m_Loaded)
    , m_VulkanContext(other.m_VulkanContext)
    , m_Metrics(std::move(other.m_Metrics))
    , m_Glyphs(std::move(other.m_Glyphs))
    , m_Kerning(std::move(other.m_Kerning))
    , m_AtlasImage(other.m_AtlasImage)
    , m_AtlasAllocation(other.m_AtlasAllocation)
    , m_AtlasImageView(other.m_AtlasImageView)
    , m_AtlasSampler(other.m_AtlasSampler)
{
    other.m_Loaded = false;
    other.m_AtlasImage = VK_NULL_HANDLE;
    other.m_AtlasAllocation = VK_NULL_HANDLE;
    other.m_AtlasImageView = VK_NULL_HANDLE;
    other.m_AtlasSampler = VK_NULL_HANDLE;
}

FontAtlas& FontAtlas::operator=(FontAtlas&& other) noexcept {
    if (this != &other) {
        if (m_Loaded) {
            Unload();
        }
        m_Loaded = other.m_Loaded;
        m_VulkanContext = other.m_VulkanContext;
        m_Metrics = std::move(other.m_Metrics);
        m_Glyphs = std::move(other.m_Glyphs);
        m_Kerning = std::move(other.m_Kerning);
        m_AtlasImage = other.m_AtlasImage;
        m_AtlasAllocation = other.m_AtlasAllocation;
        m_AtlasImageView = other.m_AtlasImageView;
        m_AtlasSampler = other.m_AtlasSampler;

        other.m_Loaded = false;
        other.m_AtlasImage = VK_NULL_HANDLE;
        other.m_AtlasAllocation = VK_NULL_HANDLE;
        other.m_AtlasImageView = VK_NULL_HANDLE;
        other.m_AtlasSampler = VK_NULL_HANDLE;
    }
    return *this;
}

bool FontAtlas::Load(const std::string& atlasPath, const std::string& metricsPath,
                     RHI::VulkanContext* vulkanContext) {
    if (m_Loaded) {
        ENGINE_CORE_WARN("FontAtlas::Load - Atlas already loaded, unloading first");
        Unload();
    }

    m_VulkanContext = vulkanContext;

    // Parse JSON metrics first
    if (!ParseMetrics(metricsPath)) {
        ENGINE_CORE_ERROR("FontAtlas::Load - Failed to parse metrics: {}", metricsPath);
        return false;
    }

    // Create Vulkan texture from atlas image
    if (!CreateAtlasTexture(atlasPath, vulkanContext)) {
        ENGINE_CORE_ERROR("FontAtlas::Load - Failed to create atlas texture: {}", atlasPath);
        m_Glyphs.clear();
        m_Kerning.clear();
        return false;
    }

    m_Loaded = true;
    ENGINE_CORE_INFO("FontAtlas loaded: {} ({} glyphs, {}x{} atlas)", 
                     m_Metrics.name, m_Glyphs.size(), 
                     m_Metrics.atlasWidth, m_Metrics.atlasHeight);
    return true;
}

void FontAtlas::Unload() {
    DestroyVulkanResources();
    m_Glyphs.clear();
    m_Kerning.clear();
    m_Metrics = FontAtlasMetrics{};
    m_Loaded = false;
}

const GlyphMetrics* FontAtlas::GetGlyph(uint32_t unicode) const {
    auto it = m_Glyphs.find(unicode);
    return it != m_Glyphs.end() ? &it->second : nullptr;
}

float FontAtlas::GetKerning(uint32_t first, uint32_t second) const {
    auto it = m_Kerning.find(KerningKey(first, second));
    return it != m_Kerning.end() ? it->second : 0.0f;
}

float FontAtlas::CalculateTextWidth(std::string_view text) const {
    float width = 0.0f;
    uint32_t prevChar = 0;

    // Simple UTF-8 iteration (handles ASCII correctly)
    for (size_t i = 0; i < text.size();) {
        uint32_t codepoint = 0;
        unsigned char c = static_cast<unsigned char>(text[i]);

        if ((c & 0x80) == 0) {
            // ASCII
            codepoint = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8
            codepoint = (c & 0x1F) << 6;
            if (i + 1 < text.size()) {
                codepoint |= (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            }
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8
            codepoint = (c & 0x0F) << 12;
            if (i + 1 < text.size()) {
                codepoint |= (static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6;
            }
            if (i + 2 < text.size()) {
                codepoint |= (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            }
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte UTF-8
            codepoint = (c & 0x07) << 18;
            if (i + 1 < text.size()) {
                codepoint |= (static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12;
            }
            if (i + 2 < text.size()) {
                codepoint |= (static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6;
            }
            if (i + 3 < text.size()) {
                codepoint |= (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            }
            i += 4;
        } else {
            // Invalid UTF-8, skip byte
            i += 1;
            continue;
        }

        // Apply kerning from previous character
        if (prevChar != 0) {
            width += GetKerning(prevChar, codepoint);
        }

        // Add glyph advance
        if (auto* glyph = GetGlyph(codepoint)) {
            width += glyph->advance;
        }

        prevChar = codepoint;
    }

    return width;
}

bool FontAtlas::ParseMetrics(const std::string& metricsPath) {
    std::ifstream file(metricsPath);
    if (!file.is_open()) {
        ENGINE_CORE_ERROR("FontAtlas::ParseMetrics - Cannot open file: {}", metricsPath);
        return false;
    }

    try {
        nlohmann::json json;
        file >> json;

        // Parse atlas metadata
        if (json.contains("atlas")) {
            auto& atlas = json["atlas"];
            m_Metrics.atlasType = atlas.value("type", "msdf");
            m_Metrics.distanceRange = atlas.value("distanceRange", 4.0f);
            m_Metrics.atlasWidth = atlas.value("width", 0);
            m_Metrics.atlasHeight = atlas.value("height", 0);
        }

        // Parse font metrics
        if (json.contains("metrics")) {
            auto& metrics = json["metrics"];
            m_Metrics.emSize = metrics.value("emSize", 32.0f);
            m_Metrics.lineHeight = metrics.value("lineHeight", 1.0f);
            m_Metrics.ascender = metrics.value("ascender", 1.0f);
            m_Metrics.descender = metrics.value("descender", 0.0f);
            m_Metrics.underlineY = metrics.value("underlineY", 0.0f);
            m_Metrics.underlineThickness = metrics.value("underlineThickness", 0.0f);
        }

        // Parse font name
        if (json.contains("name")) {
            m_Metrics.name = json["name"].get<std::string>();
        } else {
            // Extract from path
            size_t lastSlash = metricsPath.find_last_of("/\\");
            size_t lastDot = metricsPath.find_last_of('.');
            m_Metrics.name = metricsPath.substr(
                lastSlash != std::string::npos ? lastSlash + 1 : 0,
                lastDot != std::string::npos ? lastDot - lastSlash - 1 : std::string::npos
            );
        }

        // Parse glyphs
        if (json.contains("glyphs")) {
            for (auto& glyph : json["glyphs"]) {
                GlyphMetrics metrics;
                metrics.unicode = glyph.value("unicode", 0u);
                metrics.advance = glyph.value("advance", 0.0f);

                // Plane bounds (positioning in em units)
                if (glyph.contains("planeBounds")) {
                    auto& plane = glyph["planeBounds"];
                    metrics.planeLeft = plane.value("left", 0.0f);
                    metrics.planeBottom = plane.value("bottom", 0.0f);
                    metrics.planeRight = plane.value("right", 0.0f);
                    metrics.planeTop = plane.value("top", 0.0f);
                }

                // Atlas bounds (UV coordinates)
                if (glyph.contains("atlasBounds")) {
                    auto& atlas = glyph["atlasBounds"];
                    // MSDF-atlas-gen outputs pixel coordinates, normalize to 0-1
                    float atlasW = static_cast<float>(m_Metrics.atlasWidth);
                    float atlasH = static_cast<float>(m_Metrics.atlasHeight);
                    
                    if (atlasW > 0 && atlasH > 0) {
                        metrics.atlasLeft = atlas.value("left", 0.0f) / atlasW;
                        metrics.atlasBottom = atlas.value("bottom", 0.0f) / atlasH;
                        metrics.atlasRight = atlas.value("right", 0.0f) / atlasW;
                        metrics.atlasTop = atlas.value("top", 0.0f) / atlasH;
                    }
                }

                m_Glyphs[metrics.unicode] = metrics;
            }
        }

        // Parse kerning pairs
        if (json.contains("kerning")) {
            for (auto& kern : json["kerning"]) {
                uint32_t first = kern.value("unicode1", 0u);
                uint32_t second = kern.value("unicode2", 0u);
                float advance = kern.value("advance", 0.0f);
                m_Kerning[KerningKey(first, second)] = advance;
            }
        }

        return true;
    } catch (const std::exception& e) {
        ENGINE_CORE_ERROR("FontAtlas::ParseMetrics - JSON parse error: {}", e.what());
        return false;
    }
}

bool FontAtlas::CreateAtlasTexture(const std::string& atlasPath, RHI::VulkanContext* vulkanContext) {
    // Load image data
    int width, height, channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* pixels = stbi_load(atlasPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    
    if (!pixels) {
        ENGINE_CORE_ERROR("FontAtlas::CreateAtlasTexture - Failed to load image: {}", atlasPath);
        return false;
    }

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;
    VkDevice device = vulkanContext->GetDevice();
    VmaAllocator allocator = vulkanContext->GetAllocator();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        stbi_image_free(pixels);
        ENGINE_CORE_ERROR("FontAtlas::CreateAtlasTexture - Failed to create staging buffer");
        return false;
    }

    // Copy pixel data to staging buffer
    void* data;
    vmaMapMemory(allocator, stagingAllocation, &data);
    memcpy(data, pixels, imageSize);
    vmaUnmapMemory(allocator, stagingAllocation);
    stbi_image_free(pixels);

    // Create image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(width);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo imageAllocInfo = {};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &imageAllocInfo, &m_AtlasImage, &m_AtlasAllocation, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        ENGINE_CORE_ERROR("FontAtlas::CreateAtlasTexture - Failed to create image");
        return false;
    }

    // Transition image layout and copy from staging buffer
    VkCommandBuffer cmdBuffer = vulkanContext->GetCommandBuffer();
    
    // This is a simplified approach - in production you'd use a proper upload command buffer
    // For now, we assume the command buffer is available for immediate use
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkResetCommandBuffer(cmdBuffer, 0);
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // Transition to transfer destination
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_AtlasImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer to image
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, m_AtlasImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(vulkanContext->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanContext->GetGraphicsQueue());

    // Cleanup staging buffer
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    // Create image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_AtlasImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_AtlasImageView) != VK_SUCCESS) {
        ENGINE_CORE_ERROR("FontAtlas::CreateAtlasTexture - Failed to create image view");
        return false;
    }

    // Create sampler with linear filtering (important for MSDF)
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_AtlasSampler) != VK_SUCCESS) {
        ENGINE_CORE_ERROR("FontAtlas::CreateAtlasTexture - Failed to create sampler");
        vkDestroyImageView(device, m_AtlasImageView, nullptr);
        m_AtlasImageView = VK_NULL_HANDLE;
        return false;
    }

    // Update metrics with actual dimensions
    m_Metrics.atlasWidth = static_cast<uint32_t>(width);
    m_Metrics.atlasHeight = static_cast<uint32_t>(height);

    return true;
}

void FontAtlas::DestroyVulkanResources() {
    if (!m_VulkanContext) {
        return;
    }

    VkDevice device = m_VulkanContext->GetDevice();
    vkDeviceWaitIdle(device);

    if (m_AtlasSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_AtlasSampler, nullptr);
        m_AtlasSampler = VK_NULL_HANDLE;
    }

    if (m_AtlasImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_AtlasImageView, nullptr);
        m_AtlasImageView = VK_NULL_HANDLE;
    }

    if (m_AtlasImage != VK_NULL_HANDLE) {
        VmaAllocator allocator = m_VulkanContext->GetAllocator();
        vmaDestroyImage(allocator, m_AtlasImage, m_AtlasAllocation);
        m_AtlasImage = VK_NULL_HANDLE;
        m_AtlasAllocation = VK_NULL_HANDLE;
    }
}

// ============================================================================
// FontManager Implementation
// ============================================================================

FontManager& FontManager::Get() {
    static FontManager instance;
    return instance;
}

void FontManager::Initialize(RHI::VulkanContext* vulkanContext) {
    m_VulkanContext = vulkanContext;
    ENGINE_CORE_INFO("FontManager initialized");
}

void FontManager::Shutdown() {
    m_Fonts.clear();
    m_VulkanContext = nullptr;
    ENGINE_CORE_INFO("FontManager shut down");
}

bool FontManager::LoadFont(const std::string& name, const std::string& atlasPath,
                           const std::string& metricsPath) {
    if (!m_VulkanContext) {
        ENGINE_CORE_ERROR("FontManager::LoadFont - Not initialized");
        return false;
    }

    if (m_Fonts.count(name) > 0) {
        ENGINE_CORE_WARN("FontManager::LoadFont - Font '{}' already loaded, replacing", name);
        m_Fonts.erase(name);
    }

    auto atlas = std::make_unique<FontAtlas>();
    if (!atlas->Load(atlasPath, metricsPath, m_VulkanContext)) {
        return false;
    }

    m_Fonts[name] = std::move(atlas);
    return true;
}

FontAtlas* FontManager::GetFont(const std::string& name) {
    auto it = m_Fonts.find(name);
    return it != m_Fonts.end() ? it->second.get() : nullptr;
}

FontAtlas* FontManager::GetDefaultFont() {
    return GetFont(m_DefaultFontName);
}

bool FontManager::HasFont(const std::string& name) const {
    return m_Fonts.count(name) > 0;
}

} // namespace UI
} // namespace Core
