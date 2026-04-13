#pragma once

#include "Widget.h"
#include <string>
#include <vector>
#include <functional>

namespace Core {
namespace UI {
namespace Widgets {

    // =========================================================================
    // Label Widget - Static or dynamic text display
    // =========================================================================
    class Label : public Widget {
    public:
        Label(const std::string& id = "");
        
        void SetText(const std::string& text);
        const std::string& GetText() const { return m_Text; }
        
        void SetTextStyle(const TextStyle& style) { m_TextStyle = style; }
        const TextStyle& GetTextStyle() const { return m_TextStyle; }
        
        void SetWrapWidth(float width) { m_WrapWidth = width; }
        float GetWrapWidth() const { return m_WrapWidth; }

        bool SetPropertyValue(std::string_view propertyPath, const PropertyValue& value) override;
        std::optional<PropertyValue> GetPropertyValue(std::string_view propertyPath) const override;

    protected:
        void OnRender(float deltaTime) override;
        glm::vec2 CalculatePreferredSize() const override;

    private:
        std::string m_Text;
        TextStyle m_TextStyle;
        float m_WrapWidth = 0.0f;
    };

    // =========================================================================
    // Panel Widget - Container with background
    // =========================================================================
    class Panel : public Widget {
    public:
        Panel(const std::string& id = "");

        void SetBackgroundColor(const glm::vec4& color) { m_BackgroundColor = color; }
        const glm::vec4& GetBackgroundColor() const { return m_BackgroundColor; }

        void SetBorderColor(const glm::vec4& color) { m_BorderColor = color; }
        void SetBorderWidth(float width) { m_BorderWidth = width; }
        
        void SetCornerRadius(float radius) { m_CornerRadius = radius; }
        float GetCornerRadius() const { return m_CornerRadius; }

        void SetTexture(const std::string& texturePath);

    protected:
        void OnRender(float deltaTime) override;

    private:
        glm::vec4 m_BackgroundColor{0.1f, 0.1f, 0.1f, 0.8f};
        glm::vec4 m_BorderColor{0.5f, 0.5f, 0.5f, 1.0f};
        float m_BorderWidth = 0.0f;
        float m_CornerRadius = 0.0f;
        std::string m_TexturePath;
    };

    // =========================================================================
    // ProgressBar Widget - Horizontal progress indicator
    // =========================================================================
    class ProgressBar : public Widget {
    public:
        ProgressBar(const std::string& id = "");

        void SetValue(float value);
        float GetValue() const { return m_Value; }

        void SetMinValue(float min) { m_MinValue = min; }
        void SetMaxValue(float max) { m_MaxValue = max; }
        float GetMinValue() const { return m_MinValue; }
        float GetMaxValue() const { return m_MaxValue; }

        void SetFillColor(const glm::vec4& color) { m_FillColor = color; }
        void SetBackgroundColor(const glm::vec4& color) { m_BackgroundColor = color; }
        void SetBorderColor(const glm::vec4& color) { m_BorderColor = color; }

        void SetShowPercentage(bool show) { m_ShowPercentage = show; }
        bool GetShowPercentage() const { return m_ShowPercentage; }

        void SetAnimated(bool animated) { m_Animated = animated; }
        void SetAnimationSpeed(float speed) { m_AnimationSpeed = speed; }

        bool SetPropertyValue(std::string_view propertyPath, const PropertyValue& value) override;
        std::optional<PropertyValue> GetPropertyValue(std::string_view propertyPath) const override;

    protected:
        void OnRender(float deltaTime) override;
        void OnUpdate(float deltaTime) override;

    private:
        float m_Value = 0.0f;
        float m_DisplayValue = 0.0f;
        float m_MinValue = 0.0f;
        float m_MaxValue = 1.0f;
        
        glm::vec4 m_FillColor{0.2f, 0.6f, 0.2f, 1.0f};
        glm::vec4 m_BackgroundColor{0.1f, 0.1f, 0.1f, 0.8f};
        glm::vec4 m_BorderColor{0.5f, 0.5f, 0.5f, 1.0f};
        
        bool m_ShowPercentage = true;
        bool m_Animated = true;
        float m_AnimationSpeed = 5.0f;
    };

    // =========================================================================
    // HealthBar Widget - Segmented health display with color gradients
    // =========================================================================
    class HealthBar : public Widget {
    public:
        HealthBar(const std::string& id = "");

        void SetHealth(float current, float max);
        float GetCurrentHealth() const { return m_CurrentHealth; }
        float GetMaxHealth() const { return m_MaxHealth; }

        void SetSegments(int segments) { m_Segments = segments; }
        int GetSegments() const { return m_Segments; }

        void SetHighColor(const glm::vec4& color) { m_HighColor = color; }
        void SetMediumColor(const glm::vec4& color) { m_MediumColor = color; }
        void SetLowColor(const glm::vec4& color) { m_LowColor = color; }
        void SetCriticalColor(const glm::vec4& color) { m_CriticalColor = color; }

        void SetShowNumbers(bool show) { m_ShowNumbers = show; }
        bool GetShowNumbers() const { return m_ShowNumbers; }

        void SetDamageFlashDuration(float duration) { m_DamageFlashDuration = duration; }
        void TriggerDamageFlash();

        bool SetPropertyValue(std::string_view propertyPath, const PropertyValue& value) override;
        std::optional<PropertyValue> GetPropertyValue(std::string_view propertyPath) const override;

    protected:
        void OnRender(float deltaTime) override;
        void OnUpdate(float deltaTime) override;

    private:
        glm::vec4 GetHealthColor(float percentage) const;

        float m_CurrentHealth = 100.0f;
        float m_MaxHealth = 100.0f;
        float m_DisplayHealth = 100.0f;
        int m_Segments = 0;
        
        glm::vec4 m_HighColor{0.2f, 0.8f, 0.2f, 1.0f};
        glm::vec4 m_MediumColor{0.8f, 0.8f, 0.2f, 1.0f};
        glm::vec4 m_LowColor{0.8f, 0.4f, 0.2f, 1.0f};
        glm::vec4 m_CriticalColor{0.8f, 0.1f, 0.1f, 1.0f};
        
        bool m_ShowNumbers = true;
        float m_DamageFlashDuration = 0.2f;
        float m_DamageFlashTimer = 0.0f;
    };

    // =========================================================================
    // Crosshair Widget - Centered crosshair with customizable style
    // =========================================================================
    class Crosshair : public Widget {
    public:
        enum class Style {
            Cross,      // Simple + shape
            Dot,        // Center dot only
            Circle,     // Circle outline
            Dynamic     // Expands with spread
        };

        Crosshair(const std::string& id = "");

        void SetStyle(Style style) { m_Style = style; }
        Style GetStyle() const { return m_Style; }

        void SetColor(const glm::vec4& color) { m_Color = color; }
        void SetOutlineColor(const glm::vec4& color) { m_OutlineColor = color; }

        void SetSize(float size) { m_BaseSize = size; }
        float GetSize() const { return m_BaseSize; }

        void SetSpread(float spread);
        float GetSpread() const { return m_Spread; }

        void SetGap(float gap) { m_Gap = gap; }
        void SetThickness(float thickness) { m_Thickness = thickness; }

        void SetHitMarkerEnabled(bool enabled) { m_HitMarkerEnabled = enabled; }
        void TriggerHitMarker();

        bool SetPropertyValue(std::string_view propertyPath, const PropertyValue& value) override;
        std::optional<PropertyValue> GetPropertyValue(std::string_view propertyPath) const override;

    protected:
        void OnRender(float deltaTime) override;
        void OnUpdate(float deltaTime) override;

    private:
        Style m_Style = Style::Cross;
        glm::vec4 m_Color{1.0f, 1.0f, 1.0f, 0.8f};
        glm::vec4 m_OutlineColor{0.0f, 0.0f, 0.0f, 0.5f};
        
        float m_BaseSize = 20.0f;
        float m_Spread = 0.0f;
        float m_DisplaySpread = 0.0f;
        float m_Gap = 4.0f;
        float m_Thickness = 2.0f;
        
        bool m_HitMarkerEnabled = true;
        float m_HitMarkerTimer = 0.0f;
        float m_HitMarkerDuration = 0.15f;
    };

    // =========================================================================
    // ObjectiveMarker Widget - World-space objective indicator
    // =========================================================================
    class ObjectiveMarker : public Widget {
    public:
        enum class MarkerType {
            Waypoint,       // Navigation target
            Quest,          // Quest objective
            Enemy,          // Enemy indicator
            Friendly,       // Friendly unit
            Item,           // Collectible item
            Custom          // Custom icon
        };

        ObjectiveMarker(const std::string& id = "");

        void SetMarkerType(MarkerType type) { m_MarkerType = type; }
        MarkerType GetMarkerType() const { return m_MarkerType; }

        void SetWorldPosition(const glm::vec3& position) { m_WorldPosition = position; }
        const glm::vec3& GetWorldPosition() const { return m_WorldPosition; }

        void SetLabel(const std::string& label) { m_Label = label; }
        const std::string& GetLabel() const { return m_Label; }

        void SetDistance(float distance) { m_Distance = distance; }
        float GetDistance() const { return m_Distance; }

        void SetShowDistance(bool show) { m_ShowDistance = show; }
        void SetShowLabel(bool show) { m_ShowLabel = show; }

        void SetColor(const glm::vec4& color) { m_Color = color; }
        void SetPulse(bool pulse) { m_Pulse = pulse; }

        void SetClampToScreen(bool clamp) { m_ClampToScreen = clamp; }
        void SetScreenPosition(const glm::vec2& pos) { m_ScreenPosition = pos; }
        void SetOnScreen(bool onScreen) { m_IsOnScreen = onScreen; }

    protected:
        void OnRender(float deltaTime) override;
        void OnUpdate(float deltaTime) override;

    private:
        MarkerType m_MarkerType = MarkerType::Waypoint;
        glm::vec3 m_WorldPosition{0, 0, 0};
        glm::vec2 m_ScreenPosition{0, 0};
        
        std::string m_Label;
        float m_Distance = 0.0f;
        
        bool m_ShowDistance = true;
        bool m_ShowLabel = true;
        bool m_ClampToScreen = true;
        bool m_IsOnScreen = true;
        
        glm::vec4 m_Color{1.0f, 0.8f, 0.2f, 1.0f};
        bool m_Pulse = false;
        float m_PulseTimer = 0.0f;
    };

    // =========================================================================
    // Minimap Widget - Radar-style minimap display
    // =========================================================================
    class Minimap : public Widget {
    public:
        struct BlipInfo {
            glm::vec2 position;
            glm::vec4 color;
            float size = 4.0f;
            bool rotating = false;
            float rotation = 0.0f;
        };

        Minimap(const std::string& id = "");

        void SetPlayerPosition(const glm::vec2& position);
        void SetPlayerRotation(float rotation);
        void SetWorldScale(float scale) { m_WorldScale = scale; }

        void AddBlip(const std::string& id, const BlipInfo& blip);
        void UpdateBlip(const std::string& id, const BlipInfo& blip);
        void RemoveBlip(const std::string& id);
        void ClearBlips();

        void SetBackgroundColor(const glm::vec4& color) { m_BackgroundColor = color; }
        void SetBorderColor(const glm::vec4& color) { m_BorderColor = color; }

        void SetRotateWithPlayer(bool rotate) { m_RotateWithPlayer = rotate; }
        void SetCircular(bool circular) { m_Circular = circular; }

    protected:
        void OnRender(float deltaTime) override;

    private:
        glm::vec2 m_PlayerPosition{0, 0};
        float m_PlayerRotation = 0.0f;
        float m_WorldScale = 100.0f;
        
        std::unordered_map<std::string, BlipInfo> m_Blips;
        
        glm::vec4 m_BackgroundColor{0.1f, 0.1f, 0.1f, 0.7f};
        glm::vec4 m_BorderColor{0.5f, 0.5f, 0.5f, 1.0f};
        
        bool m_RotateWithPlayer = true;
        bool m_Circular = true;
    };

    // =========================================================================
    // Notification Widget - Pop-up notification message
    // =========================================================================
    class Notification : public Widget {
    public:
        enum class NotificationType {
            Info,
            Success,
            Warning,
            Error
        };

        Notification(const std::string& id = "");

        void Show(const std::string& message, NotificationType type = NotificationType::Info, float duration = 3.0f);
        void Hide();

        void SetIcon(NotificationType type, const std::string& iconPath);

        bool SetPropertyValue(std::string_view propertyPath, const PropertyValue& value) override;
        std::optional<PropertyValue> GetPropertyValue(std::string_view propertyPath) const override;

    protected:
        void OnRender(float deltaTime) override;
        void OnUpdate(float deltaTime) override;

    private:
        glm::vec4 GetTypeColor(NotificationType type) const;

        std::string m_Message;
        NotificationType m_Type = NotificationType::Info;
        float m_DisplayDuration = 3.0f;
        float m_Timer = 0.0f;
        bool m_Showing = false;
        
        std::unordered_map<NotificationType, std::string> m_Icons;
    };

} // namespace Widgets
} // namespace UI
} // namespace Core
