#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace Core {
namespace UI {

    /// @brief 9-point anchor system for UI element positioning
    /// 
    /// Anchors define the reference point on the viewport from which
    /// UI elements are positioned. Combined with offsets, this allows
    /// for responsive UI layouts that adapt to different resolutions.
    /// 
    /// Example: TopRight anchor with offset (-10, 10) places element
    /// 10 pixels from the top-right corner.
    enum class Anchor : uint8_t {
        TopLeft = 0,
        TopCenter,
        TopRight,
        CenterLeft,
        Center,
        CenterRight,
        BottomLeft,
        BottomCenter,
        BottomRight
    };

    /// @brief Horizontal alignment for text and widgets
    enum class HorizontalAlign : uint8_t {
        Left = 0,
        Center,
        Right
    };

    /// @brief Vertical alignment for text and widgets
    enum class VerticalAlign : uint8_t {
        Top = 0,
        Center,
        Bottom
    };

    /// @brief Sizing mode for UI elements
    enum class SizeMode : uint8_t {
        Fixed,          ///< Fixed pixel size
        Relative,       ///< Percentage of parent/viewport
        Auto            ///< Size to content
    };

    /// @brief UI layout constraints
    struct LayoutConstraints {
        glm::vec2 minSize = {0.0f, 0.0f};         ///< Minimum size in pixels
        glm::vec2 maxSize = {FLT_MAX, FLT_MAX};   ///< Maximum size in pixels
        glm::vec2 preferredSize = {0.0f, 0.0f};   ///< Preferred size in pixels
        SizeMode widthMode = SizeMode::Fixed;
        SizeMode heightMode = SizeMode::Fixed;
    };

    /// @brief UI element rect with position and size
    struct UIRect {
        glm::vec2 position = {0.0f, 0.0f};  ///< Top-left corner in screen pixels
        glm::vec2 size = {0.0f, 0.0f};      ///< Width and height in pixels

        /// @brief Get the center of the rect
        glm::vec2 GetCenter() const {
            return position + size * 0.5f;
        }

        /// @brief Get the bottom-right corner
        glm::vec2 GetBottomRight() const {
            return position + size;
        }

        /// @brief Check if a point is inside the rect
        bool Contains(const glm::vec2& point) const {
            return point.x >= position.x && point.x <= position.x + size.x &&
                   point.y >= position.y && point.y <= position.y + size.y;
        }

        /// @brief Check if two rects overlap
        bool Overlaps(const UIRect& other) const {
            return position.x < other.position.x + other.size.x &&
                   position.x + size.x > other.position.x &&
                   position.y < other.position.y + other.size.y &&
                   position.y + size.y > other.position.y;
        }
    };

    /// @brief Utility class for anchor-based positioning calculations
    class AnchorUtils {
    public:
        /// @brief Calculate the anchor point position on the viewport
        /// @param anchor The anchor type
        /// @param viewportSize The viewport dimensions in pixels
        /// @return Position of the anchor point in screen pixels (origin top-left)
        static glm::vec2 GetAnchorPosition(Anchor anchor, const glm::vec2& viewportSize);

        /// @brief Calculate screen position from anchor and offset
        /// @param anchor The anchor type
        /// @param offset Offset from the anchor point (positive X = right, positive Y = down)
        /// @param viewportSize The viewport dimensions in pixels
        /// @return Position in screen pixels (origin top-left)
        static glm::vec2 CalculatePosition(Anchor anchor, const glm::vec2& offset,
                                          const glm::vec2& viewportSize);

        /// @brief Calculate screen position with element size consideration
        /// @param anchor The anchor type
        /// @param offset Offset from the anchor point
        /// @param elementSize Size of the UI element
        /// @param viewportSize The viewport dimensions
        /// @param hAlign How to align the element horizontally relative to anchor
        /// @param vAlign How to align the element vertically relative to anchor
        /// @return Top-left position of the element in screen pixels
        static glm::vec2 CalculatePositionWithSize(Anchor anchor, const glm::vec2& offset,
                                                   const glm::vec2& elementSize,
                                                   const glm::vec2& viewportSize,
                                                   HorizontalAlign hAlign = HorizontalAlign::Left,
                                                   VerticalAlign vAlign = VerticalAlign::Top);

        /// @brief Convert anchor to normalized coordinates (0-1)
        /// @param anchor The anchor type
        /// @return Normalized anchor position (0,0 = top-left, 1,1 = bottom-right)
        static glm::vec2 GetNormalizedAnchor(Anchor anchor);

        /// @brief Get the alignment offset multiplier for an anchor
        /// @param anchor The anchor type
        /// @return Multiplier for element size to align correctly
        static glm::vec2 GetAlignmentMultiplier(Anchor anchor);

        /// @brief Calculate rect for anchored element
        /// @param anchor The anchor type
        /// @param offset Offset from anchor
        /// @param size Element size
        /// @param viewportSize Viewport dimensions
        /// @param pivotAlign Where the pivot point is on the element
        /// @return UIRect with calculated position
        static UIRect CalculateRect(Anchor anchor, const glm::vec2& offset,
                                    const glm::vec2& size, const glm::vec2& viewportSize,
                                    Anchor pivotAlign = Anchor::TopLeft);

        /// @brief Clamp a position to keep an element within viewport bounds
        /// @param position Current position (top-left)
        /// @param elementSize Element dimensions
        /// @param viewportSize Viewport dimensions
        /// @param margin Margin from viewport edges
        /// @return Clamped position
        static glm::vec2 ClampToViewport(const glm::vec2& position, const glm::vec2& elementSize,
                                         const glm::vec2& viewportSize, float margin = 0.0f);

        /// @brief Convert absolute position to anchor-relative offset
        /// @param absolutePos Position in screen pixels
        /// @param anchor Reference anchor
        /// @param viewportSize Viewport dimensions
        /// @return Offset relative to the anchor
        static glm::vec2 PositionToOffset(const glm::vec2& absolutePos, Anchor anchor,
                                          const glm::vec2& viewportSize);

        /// @brief Get human-readable name for anchor
        static const char* GetAnchorName(Anchor anchor);
    };

    // ========================================================================
    // Inline implementations
    // ========================================================================

    inline glm::vec2 AnchorUtils::GetAnchorPosition(Anchor anchor, const glm::vec2& viewportSize) {
        return GetNormalizedAnchor(anchor) * viewportSize;
    }

    inline glm::vec2 AnchorUtils::GetNormalizedAnchor(Anchor anchor) {
        static constexpr glm::vec2 anchors[9] = {
            {0.0f, 0.0f},   // TopLeft
            {0.5f, 0.0f},   // TopCenter
            {1.0f, 0.0f},   // TopRight
            {0.0f, 0.5f},   // CenterLeft
            {0.5f, 0.5f},   // Center
            {1.0f, 0.5f},   // CenterRight
            {0.0f, 1.0f},   // BottomLeft
            {0.5f, 1.0f},   // BottomCenter
            {1.0f, 1.0f}    // BottomRight
        };
        return anchors[static_cast<size_t>(anchor)];
    }

    inline glm::vec2 AnchorUtils::GetAlignmentMultiplier(Anchor anchor) {
        // Returns how much to offset based on element size to align pivot to anchor
        return GetNormalizedAnchor(anchor);
    }

    inline glm::vec2 AnchorUtils::CalculatePosition(Anchor anchor, const glm::vec2& offset,
                                                    const glm::vec2& viewportSize) {
        return GetAnchorPosition(anchor, viewportSize) + offset;
    }

    inline glm::vec2 AnchorUtils::CalculatePositionWithSize(Anchor anchor, const glm::vec2& offset,
                                                            const glm::vec2& elementSize,
                                                            const glm::vec2& viewportSize,
                                                            HorizontalAlign hAlign,
                                                            VerticalAlign vAlign) {
        glm::vec2 anchorPos = GetAnchorPosition(anchor, viewportSize);
        
        // Apply horizontal alignment
        float hOffset = 0.0f;
        switch (hAlign) {
            case HorizontalAlign::Left:   hOffset = 0.0f; break;
            case HorizontalAlign::Center: hOffset = -elementSize.x * 0.5f; break;
            case HorizontalAlign::Right:  hOffset = -elementSize.x; break;
        }

        // Apply vertical alignment
        float vOffset = 0.0f;
        switch (vAlign) {
            case VerticalAlign::Top:    vOffset = 0.0f; break;
            case VerticalAlign::Center: vOffset = -elementSize.y * 0.5f; break;
            case VerticalAlign::Bottom: vOffset = -elementSize.y; break;
        }

        return anchorPos + offset + glm::vec2(hOffset, vOffset);
    }

    inline UIRect AnchorUtils::CalculateRect(Anchor anchor, const glm::vec2& offset,
                                             const glm::vec2& size, const glm::vec2& viewportSize,
                                             Anchor pivotAlign) {
        glm::vec2 anchorPos = GetAnchorPosition(anchor, viewportSize);
        glm::vec2 pivotOffset = GetAlignmentMultiplier(pivotAlign) * size;
        
        UIRect rect;
        rect.position = anchorPos + offset - pivotOffset;
        rect.size = size;
        return rect;
    }

    inline glm::vec2 AnchorUtils::ClampToViewport(const glm::vec2& position, const glm::vec2& elementSize,
                                                  const glm::vec2& viewportSize, float margin) {
        glm::vec2 clamped;
        clamped.x = glm::clamp(position.x, margin, viewportSize.x - elementSize.x - margin);
        clamped.y = glm::clamp(position.y, margin, viewportSize.y - elementSize.y - margin);
        return clamped;
    }

    inline glm::vec2 AnchorUtils::PositionToOffset(const glm::vec2& absolutePos, Anchor anchor,
                                                   const glm::vec2& viewportSize) {
        return absolutePos - GetAnchorPosition(anchor, viewportSize);
    }

    inline const char* AnchorUtils::GetAnchorName(Anchor anchor) {
        static const char* names[9] = {
            "TopLeft", "TopCenter", "TopRight",
            "CenterLeft", "Center", "CenterRight",
            "BottomLeft", "BottomCenter", "BottomRight"
        };
        return names[static_cast<size_t>(anchor)];
    }

} // namespace UI
} // namespace Core
