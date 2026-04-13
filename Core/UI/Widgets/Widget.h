#pragma once

#include "Core/UI/Anchoring.h"
#include "Core/UI/TextRenderer.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Core {
namespace UI {

    class WidgetSystem;

    /// @brief Base class for Stage 27 widget runtime instances.
    class Widget {
    public:
        using PropertyValue = std::variant<std::monostate, bool, int32_t, float, std::string, glm::vec2, glm::vec4>;

        explicit Widget(std::string id = {});
        virtual ~Widget() = default;

        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;
        Widget(Widget&&) = default;
        Widget& operator=(Widget&&) = default;

        // =====================================================================
        // Lifecycle
        // =====================================================================

        virtual void Update(float deltaTime);
        virtual void Render(TextRenderer& textRenderer, const glm::vec2& viewportSize);

        virtual void OnUpdate(float deltaTime);
        virtual void OnRender(float deltaTime);
        virtual glm::vec2 CalculatePreferredSize() const;

        // =====================================================================
        // Positioning
        // =====================================================================

        void SetAnchor(Anchor anchor);
        Anchor GetAnchor() const { return m_Anchor; }

        void SetOffset(const glm::vec2& offset);
        glm::vec2 GetOffset() const { return m_Offset; }

        // Compatibility alias for older widget runtime call sites.
        void SetPosition(const glm::vec2& position) { SetOffset(position); }
        glm::vec2 GetPosition() const { return GetOffset(); }

        void SetSize(const glm::vec2& size);
        glm::vec2 GetSize() const { return m_Size; }

        void SetPivot(const glm::vec2& pivotNormalized);
        void SetPivot(Anchor pivotAnchor);
        glm::vec2 GetPivot() const { return m_Pivot; }

        glm::vec2 CalculateScreenPosition(const glm::vec2& viewportSize) const;
        UIRect GetScreenRect(const glm::vec2& viewportSize) const;
        bool ContainsPoint(const glm::vec2& point, const glm::vec2& viewportSize) const;

        // =====================================================================
        // Hierarchy
        // =====================================================================

        void AddChild(std::shared_ptr<Widget> child);
        bool RemoveChild(const std::string& childId);
        void ClearChildren();

        const std::vector<std::shared_ptr<Widget>>& GetChildren() const { return m_Children; }
        std::vector<std::shared_ptr<Widget>>& GetChildren() { return m_Children; }
        Widget* GetParent() const { return m_Parent; }

        // =====================================================================
        // Visibility & State
        // =====================================================================

        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible && m_Alpha > 0.0f; }

        void SetAlpha(float alpha) { m_Alpha = glm::clamp(alpha, 0.0f, 1.0f); }
        float GetAlpha() const { return m_Alpha; }

        void SetColor(const glm::vec4& color) { m_Color = color; m_IsDirty = true; }
        glm::vec4 GetColor() const { return m_Color; }

        bool IsDirty() const { return m_IsDirty; }
        void MarkDirty() { m_IsDirty = true; }
        void ClearDirty() { m_IsDirty = false; }

        // =====================================================================
        // Input Focus / Hover
        // =====================================================================

        void SetInteractive(bool interactive) { m_Interactive = interactive; }
        bool IsInteractive() const { return m_Interactive; }

        void SetFocused(bool focused);
        bool IsFocused() const { return m_Focused; }

        void SetHovered(bool hovered);
        bool IsHovered() const { return m_Hovered; }

        // =====================================================================
        // Z-Order
        // =====================================================================

        void SetZOrder(int32_t zOrder) { m_ZOrder = zOrder; }
        int32_t GetZOrder() const { return m_ZOrder; }

        // =====================================================================
        // Animation
        // =====================================================================

        void FadeIn(float duration = 0.3f);
        void FadeOut(float duration = 0.3f);
        bool IsAnimating() const { return m_FadeState != FadeState::None; }

        // =====================================================================
        // Binding / Property Reflection
        // =====================================================================

        virtual bool SetPropertyValue(std::string_view propertyPath, const PropertyValue& value);
        virtual std::optional<PropertyValue> GetPropertyValue(std::string_view propertyPath) const;

        // =====================================================================
        // Identification
        // =====================================================================

        void SetId(const std::string& id) { m_Id = id; }
        const std::string& GetId() const { return m_Id; }

        void SetName(const std::string& name) { m_Name = name; }
        const std::string& GetName() const { return m_Name; }

    protected:
        virtual void OnHoverBegin();
        virtual void OnHoverEnd();
        virtual void OnFocusGained();
        virtual void OnFocusLost();

        glm::vec4 GetEffectiveColor() const {
            return glm::vec4(m_Color.r, m_Color.g, m_Color.b, m_Color.a * m_Alpha);
        }

        void UpdateFadeAnimation(float deltaTime);

    protected:
        std::string m_Id;
        std::string m_Name;

        Anchor m_Anchor = Anchor::TopLeft;
        glm::vec2 m_Offset = {0.0f, 0.0f};
        glm::vec2 m_Size = {100.0f, 100.0f};
        glm::vec2 m_Pivot = {0.0f, 0.0f};

        bool m_Visible = true;
        float m_Alpha = 1.0f;
        glm::vec4 m_Color = {1.0f, 1.0f, 1.0f, 1.0f};
        bool m_IsDirty = true;

        bool m_Interactive = false;
        bool m_Focused = false;
        bool m_Hovered = false;

        int32_t m_ZOrder = 0;

        enum class FadeState { None, FadingIn, FadingOut };
        FadeState m_FadeState = FadeState::None;
        float m_FadeDuration = 0.3f;
        float m_FadeTimer = 0.0f;
        float m_FadeStartAlpha = 0.0f;
        float m_FadeEndAlpha = 1.0f;

        std::vector<std::shared_ptr<Widget>> m_Children;
        Widget* m_Parent = nullptr;
    };

    using WidgetFactory = std::function<std::unique_ptr<Widget>()>;

    class WidgetRegistry {
    public:
        static WidgetRegistry& Get();

        template<typename T>
        void RegisterWidget(const std::string& typeName) {
            m_Factories[typeName] = []() { return std::make_unique<T>(); };
        }

        std::unique_ptr<Widget> CreateWidget(const std::string& typeName);
        bool HasWidgetType(const std::string& typeName) const;

    private:
        WidgetRegistry() = default;
        std::unordered_map<std::string, WidgetFactory> m_Factories;
    };

} // namespace UI
} // namespace Core

