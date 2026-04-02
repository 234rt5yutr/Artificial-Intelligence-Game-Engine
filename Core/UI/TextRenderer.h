#pragma once

#include "Anchoring.h"
#include "FontAtlas.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace Core {
    namespace RHI { class VulkanContext; }
namespace UI {

    /// @brief Text rendering style configuration
    /// 
    /// Defines visual properties for rendered text including font, size,
    /// colors, outline, shadow, and alignment options.
    struct TextStyle {
        std::string fontFamily = "default";     ///< Font name registered with FontManager
        float fontSize = 16.0f;                 ///< Font size in pixels
        glm::vec4 color = {1, 1, 1, 1};         ///< Base text color (RGBA)
        glm::vec4 outlineColor = {0, 0, 0, 1};  ///< Outline color (RGBA)
        float outlineWidth = 0.0f;              ///< Outline width in pixels (0 = no outline)
        float shadowOffset = 0.0f;              ///< Drop shadow offset in pixels (0 = no shadow)
        glm::vec4 shadowColor = {0, 0, 0, 0.5f};///< Shadow color (RGBA)
        HorizontalAlign hAlign = HorizontalAlign::Left;   ///< Horizontal text alignment
        VerticalAlign vAlign = VerticalAlign::Top;        ///< Vertical text alignment
    };

    /// @brief Vertex structure for text rendering
    /// 
    /// Each vertex contains position in screen space, texture coordinates
    /// into the font atlas, and an optional per-vertex color tint.
    struct TextVertex {
        glm::vec2 position;     ///< Screen-space position in pixels
        glm::vec2 texCoord;     ///< UV coordinates into font atlas (0-1)
        glm::vec4 color;        ///< Per-vertex color tint

        /// @brief Get Vulkan vertex input binding description
        static VkVertexInputBindingDescription GetBindingDescription();

        /// @brief Get Vulkan vertex input attribute descriptions
        static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions();
    };

    /// @brief Push constants for MSDF text shader
    /// 
    /// Matches the layout in msdf_text.vert/frag shaders.
    struct TextPushConstants {
        glm::mat4 projectionMatrix;     ///< Orthographic projection (screen-space to clip-space)
        glm::vec4 textColor;            ///< Base text color (RGBA)
        glm::vec4 outlineColor;         ///< Outline color (RGBA)
        glm::vec4 shadowColor;          ///< Drop shadow color (RGBA)
        float outlineWidth;             ///< Outline width in pixels
        float shadowOffset;             ///< Shadow offset in pixels
        float distanceRange;            ///< MSDF distance range (typically 4.0)
        float fontSize;                 ///< Font size in pixels
    };

    /// @brief MSDF-based text renderer with batching support
    /// 
    /// TextRenderer provides high-quality text rendering using Multi-channel Signed
    /// Distance Field (MSDF) fonts. Features include:
    /// - Anti-aliased text at any size
    /// - Outline rendering with configurable width and color
    /// - Drop shadow support
    /// - Efficient batching (single draw call per font atlas)
    /// - Screen-space, anchored, and world-space text positioning
    /// 
    /// Usage:
    /// @code
    /// TextRenderer textRenderer;
    /// textRenderer.Initialize(vulkanContext);
    /// 
    /// // During frame update
    /// TextStyle style;
    /// style.fontSize = 24.0f;
    /// style.color = {1, 1, 1, 1};
    /// textRenderer.DrawText("Hello World", {100, 100}, style);
    /// textRenderer.DrawTextAnchored("FPS: 60", Anchor::TopRight, {-10, 10}, style);
    /// 
    /// // During render pass
    /// textRenderer.Flush(commandBuffer);
    /// @endcode
    class TextRenderer {
    public:
        TextRenderer() = default;
        ~TextRenderer();

        // Disable copy, allow move
        TextRenderer(const TextRenderer&) = delete;
        TextRenderer& operator=(const TextRenderer&) = delete;
        TextRenderer(TextRenderer&& other) noexcept;
        TextRenderer& operator=(TextRenderer&& other) noexcept;

        /// @brief Initialize the text renderer
        /// @param vulkanContext Vulkan context for resource creation
        /// @param renderPass Render pass for pipeline creation
        void Initialize(RHI::VulkanContext* vulkanContext, VkRenderPass renderPass);

        /// @brief Shutdown and release all resources
        void Shutdown();

        /// @brief Check if renderer is initialized
        bool IsInitialized() const { return m_Initialized; }

        // =====================================================================
        // Text Drawing API
        // =====================================================================

        /// @brief Draw text at screen-space position (pixel coordinates)
        /// @param text UTF-8 encoded text string
        /// @param position Position in pixels (origin top-left)
        /// @param style Text style configuration
        void DrawText(std::string_view text, glm::vec2 position, const TextStyle& style);

        /// @brief Draw text anchored to viewport
        /// @param text UTF-8 encoded text string
        /// @param anchor Viewport anchor point
        /// @param offset Offset from anchor in pixels
        /// @param style Text style configuration
        void DrawTextAnchored(std::string_view text, Anchor anchor, glm::vec2 offset, 
                              const TextStyle& style);

        /// @brief Draw text at world-space position (3D projected to screen)
        /// @param text UTF-8 encoded text string
        /// @param worldPosition 3D position in world space
        /// @param viewProj Combined view-projection matrix
        /// @param style Text style configuration
        /// @param billboard If true, text always faces camera (default)
        void DrawText3D(std::string_view text, glm::vec3 worldPosition, 
                        const glm::mat4& viewProj, const TextStyle& style, 
                        bool billboard = true);

        // =====================================================================
        // Text Measurement
        // =====================================================================

        /// @brief Measure text bounds in pixels
        /// @param text UTF-8 encoded text string
        /// @param style Text style configuration
        /// @return Size of text bounds in pixels (width, height)
        glm::vec2 MeasureText(std::string_view text, const TextStyle& style);

        // =====================================================================
        // Viewport Management
        // =====================================================================

        /// @brief Set viewport size (call on window resize)
        /// @param width Viewport width in pixels
        /// @param height Viewport height in pixels
        void SetViewportSize(uint32_t width, uint32_t height);

        /// @brief Get current viewport size
        glm::vec2 GetViewportSize() const { return m_ViewportSize; }

        // =====================================================================
        // Rendering
        // =====================================================================

        /// @brief Batch and render all queued text
        /// @param cmdBuffer Active command buffer within render pass
        /// 
        /// This flushes all pending text draw calls, batching by font atlas.
        /// Call this during your render pass after drawing opaque geometry.
        void Flush(VkCommandBuffer cmdBuffer);

        /// @brief Clear all pending draw calls without rendering
        void Clear();

    private:
        /// @brief Internal draw command structure
        struct TextDrawCommand {
            std::string text;
            glm::vec2 position;         ///< Screen-space position after alignment
            TextStyle style;
            FontAtlas* fontAtlas;       ///< Cached font atlas pointer
        };

        /// @brief Batch of vertices for a single font atlas
        struct TextBatch {
            FontAtlas* fontAtlas = nullptr;
            std::vector<TextVertex> vertices;
            std::vector<uint32_t> indices;
        };

        // =====================================================================
        // Pipeline Creation
        // =====================================================================

        /// @brief Create graphics pipeline for text rendering
        bool CreatePipeline(VkRenderPass renderPass);

        /// @brief Create descriptor set layout and pool
        bool CreateDescriptorResources();

        /// @brief Get or create descriptor set for font atlas
        VkDescriptorSet GetOrCreateDescriptorSet(FontAtlas* atlas);

        // =====================================================================
        // Vertex Buffer Management
        // =====================================================================

        /// @brief Create/resize vertex and index buffers
        bool CreateBuffers(size_t vertexCount, size_t indexCount);

        /// @brief Upload vertices and indices to GPU buffers
        void UploadBatchData(const std::vector<TextVertex>& vertices, 
                             const std::vector<uint32_t>& indices);

        // =====================================================================
        // Text Layout
        // =====================================================================

        /// @brief Generate vertices for a single text string
        void GenerateTextVertices(const TextDrawCommand& cmd, 
                                  std::vector<TextVertex>& outVertices,
                                  std::vector<uint32_t>& outIndices);

        /// @brief Apply alignment offset to position
        glm::vec2 ApplyAlignment(glm::vec2 position, glm::vec2 textSize, 
                                 HorizontalAlign hAlign, VerticalAlign vAlign);

        /// @brief Parse UTF-8 codepoint from string
        /// @return Number of bytes consumed
        static size_t DecodeUTF8(std::string_view text, size_t offset, uint32_t& outCodepoint);

        /// @brief Create orthographic projection matrix
        glm::mat4 CreateOrthographicProjection() const;

    private:
        bool m_Initialized = false;
        RHI::VulkanContext* m_VulkanContext = nullptr;
        glm::vec2 m_ViewportSize = {1920.0f, 1080.0f};

        // Pipeline resources
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

        // Cached descriptor sets per font atlas
        std::unordered_map<FontAtlas*, VkDescriptorSet> m_DescriptorSets;

        // GPU buffers (dynamically resized)
        VkBuffer m_VertexBuffer = VK_NULL_HANDLE;
        VmaAllocation m_VertexAllocation = VK_NULL_HANDLE;
        size_t m_VertexBufferCapacity = 0;

        VkBuffer m_IndexBuffer = VK_NULL_HANDLE;
        VmaAllocation m_IndexAllocation = VK_NULL_HANDLE;
        size_t m_IndexBufferCapacity = 0;

        // Pending draw commands (batched on Flush)
        std::vector<TextDrawCommand> m_PendingDraws;

        // Batch statistics for debugging
        size_t m_LastFrameDrawCalls = 0;
        size_t m_LastFrameVertexCount = 0;
    };

} // namespace UI
} // namespace Core
