#include "HUDWidgets.h"
#include <algorithm>
#include <cmath>

namespace Core {
namespace UI {
namespace Widgets {
namespace {

    template<typename T>
    const T* TryGetHudValue(const Widget::PropertyValue& value) {
        return std::get_if<T>(&value);
    }

}

// =============================================================================
// Label Widget
// =============================================================================

Label::Label(const std::string& id) : Widget(id) {
    SetSize({100, 20});
}

void Label::SetText(const std::string& text) {
    m_Text = text;
}

bool Label::SetPropertyValue(std::string_view propertyPath, const PropertyValue& value) {
    if (propertyPath == "text") {
        if (const std::string* stringValue = TryGetHudValue<std::string>(value)) {
            SetText(*stringValue);
            return true;
        }
        return false;
    }
    if (propertyPath == "wrapWidth") {
        if (const float* wrapValue = TryGetHudValue<float>(value)) {
            SetWrapWidth(*wrapValue);
            return true;
        }
        return false;
    }
    return Widget::SetPropertyValue(propertyPath, value);
}

std::optional<Widget::PropertyValue> Label::GetPropertyValue(std::string_view propertyPath) const {
    if (propertyPath == "text") {
        return PropertyValue(m_Text);
    }
    if (propertyPath == "wrapWidth") {
        return PropertyValue(m_WrapWidth);
    }
    return Widget::GetPropertyValue(propertyPath);
}

void Label::OnRender(float deltaTime) {
    (void)deltaTime;
    // Text rendering handled by UIManager using TextRenderer
}

glm::vec2 Label::CalculatePreferredSize() const {
    // Estimate based on text length and font size
    float charWidth = m_TextStyle.fontSize * 0.5f;
    float width = m_Text.length() * charWidth;
    float height = m_TextStyle.fontSize * 1.2f;
    return {width, height};
}

// =============================================================================
// Panel Widget
// =============================================================================

Panel::Panel(const std::string& id) : Widget(id) {
    SetSize({200, 150});
}

void Panel::SetTexture(const std::string& texturePath) {
    m_TexturePath = texturePath;
}

void Panel::OnRender(float deltaTime) {
    (void)deltaTime;
    // Panel rendering handled by UIManager
}

// =============================================================================
// ProgressBar Widget
// =============================================================================

ProgressBar::ProgressBar(const std::string& id) : Widget(id) {
    SetSize({200, 20});
}

void ProgressBar::SetValue(float value) {
    m_Value = std::clamp(value, m_MinValue, m_MaxValue);
}

bool ProgressBar::SetPropertyValue(std::string_view propertyPath, const PropertyValue& value) {
    if (propertyPath == "value") {
        if (const float* floatValue = TryGetHudValue<float>(value)) {
            SetValue(*floatValue);
            return true;
        }
        return false;
    }
    if (propertyPath == "minValue") {
        if (const float* floatValue = TryGetHudValue<float>(value)) {
            SetMinValue(*floatValue);
            return true;
        }
        return false;
    }
    if (propertyPath == "maxValue") {
        if (const float* floatValue = TryGetHudValue<float>(value)) {
            SetMaxValue(*floatValue);
            return true;
        }
        return false;
    }
    if (propertyPath == "showPercentage") {
        if (const bool* boolValue = TryGetHudValue<bool>(value)) {
            SetShowPercentage(*boolValue);
            return true;
        }
        return false;
    }
    return Widget::SetPropertyValue(propertyPath, value);
}

std::optional<Widget::PropertyValue> ProgressBar::GetPropertyValue(std::string_view propertyPath) const {
    if (propertyPath == "value") {
        return PropertyValue(m_Value);
    }
    if (propertyPath == "minValue") {
        return PropertyValue(m_MinValue);
    }
    if (propertyPath == "maxValue") {
        return PropertyValue(m_MaxValue);
    }
    if (propertyPath == "showPercentage") {
        return PropertyValue(m_ShowPercentage);
    }
    return Widget::GetPropertyValue(propertyPath);
}

void ProgressBar::OnUpdate(float deltaTime) {
    Widget::OnUpdate(deltaTime);
    
    if (m_Animated) {
        float target = m_Value;
        float diff = target - m_DisplayValue;
        m_DisplayValue += diff * m_AnimationSpeed * deltaTime;
    } else {
        m_DisplayValue = m_Value;
    }
}

void ProgressBar::OnRender(float deltaTime) {
    (void)deltaTime;
    // ProgressBar rendering handled by UIManager
}

// =============================================================================
// HealthBar Widget
// =============================================================================

HealthBar::HealthBar(const std::string& id) : Widget(id) {
    SetSize({200, 24});
}

void HealthBar::SetHealth(float current, float max) {
    float oldHealth = m_CurrentHealth;
    m_CurrentHealth = std::max(0.0f, std::min(current, max));
    m_MaxHealth = std::max(1.0f, max);
    
    if (current < oldHealth) {
        TriggerDamageFlash();
    }
}

bool HealthBar::SetPropertyValue(std::string_view propertyPath, const PropertyValue& value) {
    if (propertyPath == "health.current") {
        if (const float* currentHealth = TryGetHudValue<float>(value)) {
            SetHealth(*currentHealth, m_MaxHealth);
            return true;
        }
        return false;
    }
    if (propertyPath == "health.max") {
        if (const float* maxHealth = TryGetHudValue<float>(value)) {
            SetHealth(m_CurrentHealth, *maxHealth);
            return true;
        }
        return false;
    }
    if (propertyPath == "showNumbers") {
        if (const bool* boolValue = TryGetHudValue<bool>(value)) {
            SetShowNumbers(*boolValue);
            return true;
        }
        return false;
    }
    return Widget::SetPropertyValue(propertyPath, value);
}

std::optional<Widget::PropertyValue> HealthBar::GetPropertyValue(std::string_view propertyPath) const {
    if (propertyPath == "health.current") {
        return PropertyValue(m_CurrentHealth);
    }
    if (propertyPath == "health.max") {
        return PropertyValue(m_MaxHealth);
    }
    if (propertyPath == "showNumbers") {
        return PropertyValue(m_ShowNumbers);
    }
    return Widget::GetPropertyValue(propertyPath);
}

void HealthBar::TriggerDamageFlash() {
    m_DamageFlashTimer = m_DamageFlashDuration;
}

void HealthBar::OnUpdate(float deltaTime) {
    Widget::OnUpdate(deltaTime);
    
    // Animate display health
    float diff = m_CurrentHealth - m_DisplayHealth;
    m_DisplayHealth += diff * 5.0f * deltaTime;
    
    // Update damage flash
    if (m_DamageFlashTimer > 0.0f) {
        m_DamageFlashTimer -= deltaTime;
    }
}

void HealthBar::OnRender(float deltaTime) {
    (void)deltaTime;
    // HealthBar rendering handled by UIManager
}

glm::vec4 HealthBar::GetHealthColor(float percentage) const {
    if (percentage > 0.6f) {
        return m_HighColor;
    } else if (percentage > 0.3f) {
        float t = (percentage - 0.3f) / 0.3f;
        return glm::mix(m_MediumColor, m_HighColor, t);
    } else if (percentage > 0.15f) {
        float t = (percentage - 0.15f) / 0.15f;
        return glm::mix(m_LowColor, m_MediumColor, t);
    } else {
        float t = percentage / 0.15f;
        return glm::mix(m_CriticalColor, m_LowColor, t);
    }
}

// =============================================================================
// Crosshair Widget
// =============================================================================

Crosshair::Crosshair(const std::string& id) : Widget(id) {
    Widget::SetSize({64, 64});
    SetAnchor(Anchor::Center);
    SetPivot(Anchor::Center);
}

void Crosshair::SetSpread(float spread) {
    m_Spread = std::max(0.0f, spread);
}

bool Crosshair::SetPropertyValue(std::string_view propertyPath, const PropertyValue& value) {
    if (propertyPath == "spread") {
        if (const float* spreadValue = TryGetHudValue<float>(value)) {
            SetSpread(*spreadValue);
            return true;
        }
        return false;
    }
    if (propertyPath == "hitMarkerEnabled") {
        if (const bool* boolValue = TryGetHudValue<bool>(value)) {
            SetHitMarkerEnabled(*boolValue);
            return true;
        }
        return false;
    }
    return Widget::SetPropertyValue(propertyPath, value);
}

std::optional<Widget::PropertyValue> Crosshair::GetPropertyValue(std::string_view propertyPath) const {
    if (propertyPath == "spread") {
        return PropertyValue(m_Spread);
    }
    if (propertyPath == "hitMarkerEnabled") {
        return PropertyValue(m_HitMarkerEnabled);
    }
    return Widget::GetPropertyValue(propertyPath);
}

void Crosshair::TriggerHitMarker() {
    if (m_HitMarkerEnabled) {
        m_HitMarkerTimer = m_HitMarkerDuration;
    }
}

void Crosshair::OnUpdate(float deltaTime) {
    Widget::OnUpdate(deltaTime);
    
    // Smoothly interpolate spread
    float diff = m_Spread - m_DisplaySpread;
    m_DisplaySpread += diff * 15.0f * deltaTime;
    
    // Update hit marker
    if (m_HitMarkerTimer > 0.0f) {
        m_HitMarkerTimer -= deltaTime;
    }
}

void Crosshair::OnRender(float deltaTime) {
    (void)deltaTime;
    // Crosshair rendering handled by UIManager
}

// =============================================================================
// ObjectiveMarker Widget
// =============================================================================

ObjectiveMarker::ObjectiveMarker(const std::string& id) : Widget(id) {
    SetSize({32, 32});
}

void ObjectiveMarker::OnUpdate(float deltaTime) {
    Widget::OnUpdate(deltaTime);
    
    if (m_Pulse) {
        m_PulseTimer += deltaTime * 2.0f;
        if (m_PulseTimer > 2.0f * 3.14159f) {
            m_PulseTimer -= 2.0f * 3.14159f;
        }
    }
}

void ObjectiveMarker::OnRender(float deltaTime) {
    (void)deltaTime;
    // ObjectiveMarker rendering handled by UIManager/WorldWidgetRenderer
}

// =============================================================================
// Minimap Widget
// =============================================================================

Minimap::Minimap(const std::string& id) : Widget(id) {
    SetSize({150, 150});
    SetAnchor(Anchor::TopRight);
    SetPivot(Anchor::TopRight);
}

void Minimap::SetPlayerPosition(const glm::vec2& position) {
    m_PlayerPosition = position;
}

void Minimap::SetPlayerRotation(float rotation) {
    m_PlayerRotation = rotation;
}

void Minimap::AddBlip(const std::string& id, const BlipInfo& blip) {
    m_Blips[id] = blip;
}

void Minimap::UpdateBlip(const std::string& id, const BlipInfo& blip) {
    auto it = m_Blips.find(id);
    if (it != m_Blips.end()) {
        it->second = blip;
    }
}

void Minimap::RemoveBlip(const std::string& id) {
    m_Blips.erase(id);
}

void Minimap::ClearBlips() {
    m_Blips.clear();
}

void Minimap::OnRender(float deltaTime) {
    (void)deltaTime;
    // Minimap rendering handled by UIManager
}

// =============================================================================
// Notification Widget
// =============================================================================

Notification::Notification(const std::string& id) : Widget(id) {
    SetSize({300, 60});
    SetAnchor(Anchor::TopCenter);
    SetPivot(Anchor::TopCenter);
    SetVisible(false);
}

void Notification::Show(const std::string& message, NotificationType type, float duration) {
    m_Message = message;
    m_Type = type;
    m_DisplayDuration = duration;
    m_Timer = 0.0f;
    m_Showing = true;
    SetVisible(true);
    FadeIn(0.2f);
}

void Notification::Hide() {
    FadeOut(0.2f);
    m_Showing = false;
}

void Notification::SetIcon(NotificationType type, const std::string& iconPath) {
    m_Icons[type] = iconPath;
}

bool Notification::SetPropertyValue(std::string_view propertyPath, const PropertyValue& value) {
    if (propertyPath == "message") {
        if (const std::string* message = TryGetHudValue<std::string>(value)) {
            Show(*message, m_Type, m_DisplayDuration);
            return true;
        }
        return false;
    }
    if (propertyPath == "displayDuration") {
        if (const float* duration = TryGetHudValue<float>(value)) {
            m_DisplayDuration = std::max(0.0f, *duration);
            return true;
        }
        return false;
    }
    return Widget::SetPropertyValue(propertyPath, value);
}

std::optional<Widget::PropertyValue> Notification::GetPropertyValue(std::string_view propertyPath) const {
    if (propertyPath == "message") {
        return PropertyValue(m_Message);
    }
    if (propertyPath == "displayDuration") {
        return PropertyValue(m_DisplayDuration);
    }
    return Widget::GetPropertyValue(propertyPath);
}

void Notification::OnUpdate(float deltaTime) {
    Widget::OnUpdate(deltaTime);
    
    if (m_Showing) {
        m_Timer += deltaTime;
        if (m_Timer >= m_DisplayDuration) {
            Hide();
        }
    }
    
    if (!m_Showing && GetAlpha() <= 0.01f) {
        SetVisible(false);
    }
}

void Notification::OnRender(float deltaTime) {
    (void)deltaTime;
    // Notification rendering handled by UIManager
}

glm::vec4 Notification::GetTypeColor(NotificationType type) const {
    switch (type) {
        case NotificationType::Info:
            return {0.2f, 0.4f, 0.8f, 1.0f};
        case NotificationType::Success:
            return {0.2f, 0.7f, 0.3f, 1.0f};
        case NotificationType::Warning:
            return {0.9f, 0.7f, 0.2f, 1.0f};
        case NotificationType::Error:
            return {0.8f, 0.2f, 0.2f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

} // namespace Widgets
} // namespace UI
} // namespace Core
