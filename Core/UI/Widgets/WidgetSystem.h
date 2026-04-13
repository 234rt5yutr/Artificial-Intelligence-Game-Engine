#pragma once

#include "Core/UI/Widgets/Widget.h"

#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Core {
namespace UI {
namespace Widgets {

    struct WidgetBatchKey {
        uint32_t textureId = 0;
        uint32_t shaderId = 0;
        int32_t zOrder = 0;
        bool operator==(const WidgetBatchKey& other) const {
            return textureId == other.textureId &&
                   shaderId == other.shaderId &&
                   zOrder == other.zOrder;
        }
    };

    struct WidgetBatchKeyHash {
        size_t operator()(const WidgetBatchKey& key) const {
            return std::hash<uint32_t>{}(key.textureId) ^
                   (std::hash<uint32_t>{}(key.shaderId) << 1) ^
                   (std::hash<int32_t>{}(key.zOrder) << 2);
        }
    };

    struct BatchVertex {
        glm::vec2 position;
        glm::vec2 texCoord;
        glm::vec4 color;
    };

    struct WidgetRenderCommand {
        Widget* widget = nullptr;
        glm::vec2 screenPosition;
        glm::vec2 screenSize;
        float alpha = 1.0f;
        uint32_t textureId = 0;
        uint32_t shaderId = 0;
    };

    struct WidgetBatch {
        WidgetBatchKey key;
        std::vector<WidgetRenderCommand> commands;
        std::vector<BatchVertex> vertices;
        std::vector<uint32_t> indices;
    };

    class WidgetSystem {
    public:
        using WidgetPropertyValue = Widget::PropertyValue;

        static WidgetSystem& Get();

        void Initialize();
        void Shutdown();

        void AddWidget(std::shared_ptr<Widget> widget);
        void RemoveWidget(const std::string& id);
        Widget* FindWidget(const std::string& id);
        const std::vector<std::shared_ptr<Widget>>& GetWidgets() const { return m_Widgets; }
        void ClearWidgets();

        void Update(float deltaTime);
        void BuildBatches();
        const std::vector<WidgetBatch>& GetBatches() const { return m_Batches; }
        void Render();

        void OnMouseMove(const glm::vec2& position);
        void OnMouseButton(int button, bool pressed);
        void OnKeyEvent(int key, bool pressed);
        Widget* GetWidgetUnderCursor() const { return m_HoveredWidget; }
        Widget* GetFocusedWidget() const { return m_FocusedWidget; }

        // Reflection hooks consumed by Stage 27 binding/transition services.
        bool SetWidgetProperty(const std::string& widgetId, std::string_view propertyPath, const WidgetPropertyValue& value);
        std::optional<WidgetPropertyValue> GetWidgetProperty(const std::string& widgetId, std::string_view propertyPath) const;

        // Lightweight runtime pooling helpers for blueprint/layout instance reuse.
        void PrewarmWidgetPool(const std::string& widgetType, uint32_t count);
        std::shared_ptr<Widget> CheckoutWidget(const std::string& widgetType);
        void CheckinWidget(const std::string& widgetType, std::shared_ptr<Widget> widget);

        void SetScreenSize(const glm::vec2& size) { m_ScreenSize = size; m_LayoutDirty = true; }
        glm::vec2 GetScreenSize() const { return m_ScreenSize; }
        void InvalidateLayout() { m_LayoutDirty = true; }
        void UpdateLayout();

        // Contract-focused runtime self-test used by Stage 27 bring-up.
        bool RunContractSelfTest(std::vector<std::string>* failures) const;

    private:
        WidgetSystem() = default;
        ~WidgetSystem() = default;
        WidgetSystem(const WidgetSystem&) = delete;
        WidgetSystem& operator=(const WidgetSystem&) = delete;

        void RegisterWidgetRecursive(const std::shared_ptr<Widget>& widget);
        void UnregisterWidgetRecursive(Widget* widget);
        void CollectRenderCommands(Widget* widget, std::vector<WidgetRenderCommand>& commands);
        void BatchRenderCommands(const std::vector<WidgetRenderCommand>& commands);
        void GenerateBatchGeometry(const WidgetRenderCommand& cmd, WidgetBatch& batch);
        Widget* HitTest(const glm::vec2& position);
        glm::vec2 CalculateScreenPosition(Widget* widget) const;
        void CollectHitTestWidgets(Widget* widget, std::vector<Widget*>& outWidgets);

    private:
        bool m_Initialized = false;
        std::vector<std::shared_ptr<Widget>> m_Widgets;
        std::unordered_map<std::string, Widget*> m_WidgetLookup;
        std::unordered_map<std::string, std::vector<std::shared_ptr<Widget>>> m_WidgetPool;

        std::vector<WidgetBatch> m_Batches;
        bool m_BatchesDirty = true;

        glm::vec2 m_ScreenSize{1920, 1080};
        bool m_LayoutDirty = true;

        glm::vec2 m_MousePosition{0, 0};
        Widget* m_HoveredWidget = nullptr;
        Widget* m_FocusedWidget = nullptr;
    };

} // namespace Widgets
} // namespace UI
} // namespace Core

