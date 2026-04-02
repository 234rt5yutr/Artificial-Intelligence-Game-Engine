#pragma once

#include "Core/UI/Anchoring.h"
#include "Core/UI/TextRenderer.h"

#include <glm/glm.hpp>
#include <string>
#include <functional>
#include <memory>

namespace Core {
namespace UI {

    // Forward declarations
    class WidgetSystem;
    class TextRenderer;

    /// @brief Base class for all UI widgets
    /// 
    /// Widget provides a common interface for all UI elements, including:
    /// - Positioning (anchor-based or absolute)
    /// - Visibility and input focus handling
    /// - Animation support (fade in/out)
    /// - Z-order for rendering priority
    /// 
    /// Derived classes should implement Render() to draw their content.
    class Widget {
    public:
        Widget() = default;
        virtual ~Widget() = default;

        // Disable copy, allow move
        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;
        Widget(Widget&&) = default;
        Widget& operator=(Widget&&) = default;

        // =====================================================================
        // Lifecycle
        // =====================================================================

        /// @brief Update widget state
        /// @param deltaTime Time since last update in seconds
        virtual void Update(float deltaTime);

        /// @brief Render widget content
        /// @param textRenderer Text renderer for drawing text
        /// @param viewportSize Current viewport dimensions
        virtual void Render(TextRenderer& textRenderer, const glm::vec2& viewportSize) = 0;

        // =====================================================================
        // Positioning
        // =====================================================================

        /// @brief Set anchor-based positioning
        void SetAnchor(Anchor anchor) { m_Anchor = anchor; m_IsDirty = true; }
        Anchor GetAnchor() const { return m_Anchor; }

        /// @brief Set offset from anchor point
        void SetOffset(const glm::vec2& offset) { m_Offset = offset; m_IsDirty = true; }
        glm::vec2 GetOffset() const { return m_Offset; }

        /// @brief Set widget size
        void SetSize(const glm::vec2& size) { m_Size = size; m_IsDirty = true; }
        glm::vec2 GetSize() const { return m_Size; }

        /// @brief Set pivot point for positioning (0-1, where 0.5,0.5 is center)
        void SetPivot(const glm::vec2& pivot) { m_Pivot = pivot; m_IsDirty = true; }
        glm::vec2 GetPivot() const { return m_Pivot; }

        /// @brief Calculate screen position based on anchor, offset, and size
        glm::vec2 CalculateScreenPosition(const glm::vec2& viewportSize) const;

        /// @brief Get bounding rect in screen coordinates
        UIRect GetScreenRect(const glm::vec2& viewportSize) const;

        // =====================================================================
        // Visibility & State
        // =====================================================================

        /// @brief Set widget visibility
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible && m_Alpha > 0.0f; }

        /// @brief Set widget alpha (opacity)
        void SetAlpha(float alpha) { m_Alpha = glm::clamp(alpha, 0.0f, 1.0f); }
        float GetAlpha() const { return m_Alpha; }

        /// @brief Set widget tint color
        void SetColor(const glm::vec4& color) { m_Color = color; m_IsDirty = true; }
        glm::vec4 GetColor() const { return m_Color; }

        /// @brief Check if widget is dirty (needs re-render)
        bool IsDirty() const { return m_IsDirty; }
        void MarkDirty() { m_IsDirty = true; }
        void ClearDirty() { m_IsDirty = false; }

        // =====================================================================
        // Input Focus
        // =====================================================================

        /// @brief Set whether widget can receive input focus
        void SetInteractive(bool interactive) { m_Interactive = interactive; }
        bool IsInteractive() const { return m_Interactive; }

        /// @brief Set whether widget currently has input focus
        void SetFocused(bool focused) { m_Focused = focused; }
        bool IsFocused() const { return m_Focused; }

        /// @brief Check if point is inside widget bounds
        bool ContainsPoint(const glm::vec2& point, const glm::vec2& viewportSize) const;

        // =====================================================================
        // Z-Order
        // =====================================================================

        /// @brief Set render order (higher = rendered on top)
        void SetZOrder(int32_t zOrder) { m_ZOrder = zOrder; }
        int32_t GetZOrder() const { return m_ZOrder; }

        // =====================================================================
        // Animation
        // =====================================================================

        /// @brief Start fade-in animation
        void FadeIn(float duration = 0.3f);

        /// @brief Start fade-out animation
        void FadeOut(float duration = 0.3f);

        /// @brief Check if currently animating
        bool IsAnimating() const { return m_FadeState != FadeState::None; }

        // =====================================================================
        // Identification
        // =====================================================================

        /// @brief Set unique widget identifier
        void SetId(const std::string& id) { m_Id = id; }
        const std::string& GetId() const { return m_Id; }

        /// @brief Set display name
        void SetName(const std::string& name) { m_Name = name; }
        const std::string& GetName() const { return m_Name; }

    protected:
        /// @brief Get effective color with alpha applied
        glm::vec4 GetEffectiveColor() const {
            return glm::vec4(m_Color.r, m_Color.g, m_Color.b, m_Color.a * m_Alpha);
        }

        /// @brief Update fade animation
        void UpdateFadeAnimation(float deltaTime);

    protected:
        // Identification
        std::string m_Id;
        std::string m_Name;

        // Positioning
        Anchor m_Anchor = Anchor::TopLeft;
        glm::vec2 m_Offset = {0.0f, 0.0f};
        glm::vec2 m_Size = {100.0f, 100.0f};
        glm::vec2 m_Pivot = {0.0f, 0.0f};  // Top-left pivot by default

        // Visual state
        bool m_Visible = true;
        float m_Alpha = 1.0f;
        glm::vec4 m_Color = {1.0f, 1.0f, 1.0f, 1.0f};
        bool m_IsDirty = true;

        // Input
        bool m_Interactive = false;
        bool m_Focused = false;

        // Render order
        int32_t m_ZOrder = 0;

        // Animation
        enum class FadeState { None, FadingIn, FadingOut };
        FadeState m_FadeState = FadeState::None;
        float m_FadeDuration = 0.3f;
        float m_FadeTimer = 0.0f;
        float m_FadeStartAlpha = 0.0f;
        float m_FadeEndAlpha = 1.0f;
    };

    // =========================================================================
    // Widget Factory
    // =========================================================================

    /// @brief Widget creation callback type
    using WidgetFactory = std::function<std::unique_ptr<Widget>()>;

    /// @brief Registry for widget types and factories
    class WidgetRegistry {
    public:
        static WidgetRegistry& Get();

        /// @brief Register a widget type with its factory
        template<typename T>
        void RegisterWidget(const std::string& typeName) {
            m_Factories[typeName] = []() { return std::make_unique<T>(); };
        }

        /// @brief Create widget by type name
        std::unique_ptr<Widget> CreateWidget(const std::string& typeName);

        /// @brief Check if widget type is registered
        bool HasWidgetType(const std::string& typeName) const;

    private:
        WidgetRegistry() = default;
        std::unordered_map<std::string, WidgetFactory> m_Factories;
    };

} // namespace UI
} // namespace Core
