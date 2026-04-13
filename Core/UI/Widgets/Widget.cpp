#include "Core/UI/Widgets/Widget.h"

#include "Core/Log.h"

#include <algorithm>

namespace Core {
namespace UI {
namespace {

    template<typename T>
    const T* TryGetPropertyValue(const Widget::PropertyValue& value) {
        return std::get_if<T>(&value);
    }

} // namespace

    Widget::Widget(std::string id) : m_Id(std::move(id)) {}

    void Widget::Update(float deltaTime) {
        if (m_FadeState != FadeState::None) {
            UpdateFadeAnimation(deltaTime);
        }

        OnUpdate(deltaTime);
        for (const std::shared_ptr<Widget>& child : m_Children) {
            if (child != nullptr) {
                child->Update(deltaTime);
            }
        }
    }

    void Widget::Render(TextRenderer& textRenderer, const glm::vec2& viewportSize) {
        (void)textRenderer;
        (void)viewportSize;

        OnRender(0.0f);
        for (const std::shared_ptr<Widget>& child : m_Children) {
            if (child != nullptr && child->IsVisible()) {
                child->Render(textRenderer, viewportSize);
            }
        }
    }

    void Widget::OnUpdate(float /*deltaTime*/) {}

    void Widget::OnRender(float /*deltaTime*/) {}

    glm::vec2 Widget::CalculatePreferredSize() const {
        return m_Size;
    }

    void Widget::SetAnchor(Anchor anchor) {
        m_Anchor = anchor;
        m_IsDirty = true;
    }

    void Widget::SetOffset(const glm::vec2& offset) {
        m_Offset = offset;
        m_IsDirty = true;
    }

    void Widget::SetSize(const glm::vec2& size) {
        m_Size = size;
        m_IsDirty = true;
    }

    void Widget::SetScale(const glm::vec2& scale) {
        m_Scale = glm::max(scale, glm::vec2(0.0f, 0.0f));
        m_IsDirty = true;
    }

    void Widget::SetPivot(const glm::vec2& pivotNormalized) {
        m_Pivot = glm::vec2(
            glm::clamp(pivotNormalized.x, 0.0f, 1.0f),
            glm::clamp(pivotNormalized.y, 0.0f, 1.0f));
        m_IsDirty = true;
    }

    void Widget::SetPivot(Anchor pivotAnchor) {
        SetPivot(AnchorUtils::GetNormalizedAnchor(pivotAnchor));
    }

    glm::vec2 Widget::CalculateScreenPosition(const glm::vec2& viewportSize) const {
        const glm::vec2 scaledSize = GetScaledSize();
        glm::vec2 anchorPosition;
        if (m_Parent != nullptr) {
            const glm::vec2 parentPosition = m_Parent->CalculateScreenPosition(viewportSize);
            const glm::vec2 parentSize = m_Parent->GetScaledSize();
            anchorPosition = parentPosition + AnchorUtils::GetAnchorPosition(m_Anchor, parentSize);
        } else {
            anchorPosition = AnchorUtils::GetAnchorPosition(m_Anchor, viewportSize);
        }

        return anchorPosition + m_Offset - (m_Pivot * scaledSize);
    }

    UIRect Widget::GetScreenRect(const glm::vec2& viewportSize) const {
        UIRect rect;
        rect.position = CalculateScreenPosition(viewportSize);
        rect.size = GetScaledSize();
        return rect;
    }

    bool Widget::ContainsPoint(const glm::vec2& point, const glm::vec2& viewportSize) const {
        if (!IsVisible()) {
            return false;
        }
        return GetScreenRect(viewportSize).Contains(point);
    }

    void Widget::AddChild(std::shared_ptr<Widget> child) {
        if (child == nullptr || child.get() == this) {
            return;
        }

        child->m_Parent = this;
        m_Children.push_back(std::move(child));
        m_IsDirty = true;
    }

    bool Widget::RemoveChild(const std::string& childId) {
        const auto childIt = std::find_if(
            m_Children.begin(),
            m_Children.end(),
            [&childId](const std::shared_ptr<Widget>& child) {
                return child != nullptr && child->GetId() == childId;
            });
        if (childIt == m_Children.end()) {
            return false;
        }

        if (*childIt != nullptr) {
            (*childIt)->m_Parent = nullptr;
        }
        m_Children.erase(childIt);
        m_IsDirty = true;
        return true;
    }

    void Widget::ClearChildren() {
        for (const std::shared_ptr<Widget>& child : m_Children) {
            if (child != nullptr) {
                child->m_Parent = nullptr;
            }
        }
        m_Children.clear();
        m_IsDirty = true;
    }

    void Widget::SetFocused(bool focused) {
        if (m_Focused == focused) {
            return;
        }
        m_Focused = focused;
        if (m_Focused) {
            OnFocusGained();
        } else {
            OnFocusLost();
        }
    }

    void Widget::SetHovered(bool hovered) {
        if (m_Hovered == hovered) {
            return;
        }
        m_Hovered = hovered;
        if (m_Hovered) {
            OnHoverBegin();
        } else {
            OnHoverEnd();
        }
    }

    void Widget::FadeIn(float duration) {
        m_FadeState = FadeState::FadingIn;
        m_FadeDuration = duration;
        m_FadeTimer = 0.0f;
        m_FadeStartAlpha = m_Alpha;
        m_FadeEndAlpha = 1.0f;
        m_Visible = true;
    }

    void Widget::FadeOut(float duration) {
        m_FadeState = FadeState::FadingOut;
        m_FadeDuration = duration;
        m_FadeTimer = 0.0f;
        m_FadeStartAlpha = m_Alpha;
        m_FadeEndAlpha = 0.0f;
    }

    bool Widget::SetPropertyValue(std::string_view propertyPath, const PropertyValue& value) {
        if (propertyPath == "visible") {
            if (const bool* boolValue = TryGetPropertyValue<bool>(value)) {
                SetVisible(*boolValue);
                return true;
            }
            return false;
        }
        if (propertyPath == "interactive") {
            if (const bool* boolValue = TryGetPropertyValue<bool>(value)) {
                SetInteractive(*boolValue);
                return true;
            }
            return false;
        }
        if (propertyPath == "alpha") {
            if (const float* floatValue = TryGetPropertyValue<float>(value)) {
                SetAlpha(*floatValue);
                return true;
            }
            return false;
        }
        if (propertyPath == "zOrder") {
            if (const int32_t* intValue = TryGetPropertyValue<int32_t>(value)) {
                SetZOrder(*intValue);
                return true;
            }
            return false;
        }
        if (propertyPath == "position" || propertyPath == "offset") {
            if (const glm::vec2* vec2Value = TryGetPropertyValue<glm::vec2>(value)) {
                SetOffset(*vec2Value);
                return true;
            }
            return false;
        }
        if (propertyPath == "size") {
            if (const glm::vec2* vec2Value = TryGetPropertyValue<glm::vec2>(value)) {
                SetSize(*vec2Value);
                return true;
            }
            return false;
        }
        if (propertyPath == "scale") {
            if (const glm::vec2* vec2Value = TryGetPropertyValue<glm::vec2>(value)) {
                SetScale(*vec2Value);
                return true;
            }
            return false;
        }
        if (propertyPath == "color") {
            if (const glm::vec4* vec4Value = TryGetPropertyValue<glm::vec4>(value)) {
                SetColor(*vec4Value);
                return true;
            }
            return false;
        }
        if (propertyPath == "id") {
            if (const std::string* stringValue = TryGetPropertyValue<std::string>(value)) {
                SetId(*stringValue);
                return true;
            }
            return false;
        }
        if (propertyPath == "name") {
            if (const std::string* stringValue = TryGetPropertyValue<std::string>(value)) {
                SetName(*stringValue);
                return true;
            }
            return false;
        }

        return false;
    }

    std::optional<Widget::PropertyValue> Widget::GetPropertyValue(std::string_view propertyPath) const {
        if (propertyPath == "visible") {
            return PropertyValue(m_Visible);
        }
        if (propertyPath == "interactive") {
            return PropertyValue(m_Interactive);
        }
        if (propertyPath == "alpha") {
            return PropertyValue(m_Alpha);
        }
        if (propertyPath == "zOrder") {
            return PropertyValue(m_ZOrder);
        }
        if (propertyPath == "position" || propertyPath == "offset") {
            return PropertyValue(m_Offset);
        }
        if (propertyPath == "size") {
            return PropertyValue(m_Size);
        }
        if (propertyPath == "scale") {
            return PropertyValue(m_Scale);
        }
        if (propertyPath == "color") {
            return PropertyValue(m_Color);
        }
        if (propertyPath == "id") {
            return PropertyValue(m_Id);
        }
        if (propertyPath == "name") {
            return PropertyValue(m_Name);
        }
        return std::nullopt;
    }

    void Widget::OnHoverBegin() {}

    void Widget::OnHoverEnd() {}

    void Widget::OnFocusGained() {}

    void Widget::OnFocusLost() {}

    void Widget::UpdateFadeAnimation(float deltaTime) {
        m_FadeTimer += deltaTime;

        float t = m_FadeDuration > 0.0f ? m_FadeTimer / m_FadeDuration : 1.0f;
        t = glm::clamp(t, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t);
        m_Alpha = glm::mix(m_FadeStartAlpha, m_FadeEndAlpha, t);

        if (m_FadeTimer >= m_FadeDuration) {
            m_FadeState = FadeState::None;
            m_Alpha = m_FadeEndAlpha;
            if (m_FadeEndAlpha <= 0.0f) {
                m_Visible = false;
            }
        }
    }

    WidgetRegistry& WidgetRegistry::Get() {
        static WidgetRegistry instance;
        return instance;
    }

    std::unique_ptr<Widget> WidgetRegistry::CreateWidget(const std::string& typeName) {
        auto it = m_Factories.find(typeName);
        if (it != m_Factories.end()) {
            return it->second();
        }

        ENGINE_CORE_WARN("WidgetRegistry::CreateWidget - Unknown widget type: {}", typeName);
        return nullptr;
    }

    bool WidgetRegistry::HasWidgetType(const std::string& typeName) const {
        return m_Factories.find(typeName) != m_Factories.end();
    }

} // namespace UI
} // namespace Core

