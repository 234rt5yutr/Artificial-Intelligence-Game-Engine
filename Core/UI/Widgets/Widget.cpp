#include "Widget.h"
#include "Core/Log.h"

#include <algorithm>

namespace Core {
namespace UI {

// ============================================================================
// Widget Implementation
// ============================================================================

void Widget::Update(float deltaTime) {
    // Update fade animation
    if (m_FadeState != FadeState::None) {
        UpdateFadeAnimation(deltaTime);
    }
}

glm::vec2 Widget::CalculateScreenPosition(const glm::vec2& viewportSize) const {
    // Get anchor position
    glm::vec2 anchorPos = AnchorUtils::GetAnchorPosition(m_Anchor, viewportSize);
    
    // Apply offset
    glm::vec2 position = anchorPos + m_Offset;
    
    // Apply pivot offset
    position -= m_Pivot * m_Size;
    
    return position;
}

UIRect Widget::GetScreenRect(const glm::vec2& viewportSize) const {
    UIRect rect;
    rect.position = CalculateScreenPosition(viewportSize);
    rect.size = m_Size;
    return rect;
}

bool Widget::ContainsPoint(const glm::vec2& point, const glm::vec2& viewportSize) const {
    if (!IsVisible()) {
        return false;
    }
    return GetScreenRect(viewportSize).Contains(point);
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

void Widget::UpdateFadeAnimation(float deltaTime) {
    m_FadeTimer += deltaTime;
    
    float t = m_FadeDuration > 0.0f ? m_FadeTimer / m_FadeDuration : 1.0f;
    t = glm::clamp(t, 0.0f, 1.0f);
    
    // Smooth interpolation (ease in-out)
    t = t * t * (3.0f - 2.0f * t);
    
    m_Alpha = glm::mix(m_FadeStartAlpha, m_FadeEndAlpha, t);
    
    if (m_FadeTimer >= m_FadeDuration) {
        m_FadeState = FadeState::None;
        m_Alpha = m_FadeEndAlpha;
        
        // Hide widget when fade out completes
        if (m_FadeEndAlpha <= 0.0f) {
            m_Visible = false;
        }
    }
}

// ============================================================================
// WidgetRegistry Implementation
// ============================================================================

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
