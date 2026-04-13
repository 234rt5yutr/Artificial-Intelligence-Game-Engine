#include "Core/UI/Widgets/WidgetSystem.h"

#include "Core/Log.h"

#include <algorithm>

namespace Core {
namespace UI {
namespace Widgets {

    WidgetSystem& WidgetSystem::Get() {
        static WidgetSystem instance;
        return instance;
    }

    void WidgetSystem::Initialize() {
        if (m_Initialized) {
            return;
        }
        m_Initialized = true;
    }

    void WidgetSystem::Shutdown() {
        if (!m_Initialized) {
            return;
        }

        ClearWidgets();
        m_WidgetPool.clear();
        m_Batches.clear();
        m_Initialized = false;
    }

    void WidgetSystem::RegisterWidgetRecursive(const std::shared_ptr<Widget>& widget) {
        if (widget == nullptr) {
            return;
        }
        if (!widget->GetId().empty()) {
            m_WidgetLookup[widget->GetId()] = widget.get();
        }
        for (const std::shared_ptr<Widget>& child : widget->GetChildren()) {
            RegisterWidgetRecursive(child);
        }
    }

    void WidgetSystem::UnregisterWidgetRecursive(Widget* widget) {
        if (widget == nullptr) {
            return;
        }
        if (!widget->GetId().empty()) {
            m_WidgetLookup.erase(widget->GetId());
        }
        for (const std::shared_ptr<Widget>& child : widget->GetChildren()) {
            UnregisterWidgetRecursive(child.get());
        }
    }

    void WidgetSystem::AddWidget(std::shared_ptr<Widget> widget) {
        if (widget == nullptr) {
            return;
        }

        RegisterWidgetRecursive(widget);
        m_Widgets.push_back(std::move(widget));
        m_LayoutDirty = true;
        m_BatchesDirty = true;
    }

    void WidgetSystem::RemoveWidget(const std::string& id) {
        auto widgetIt = std::find_if(
            m_Widgets.begin(),
            m_Widgets.end(),
            [&id](const std::shared_ptr<Widget>& widget) {
                return widget != nullptr && widget->GetId() == id;
            });
        if (widgetIt == m_Widgets.end()) {
            return;
        }

        UnregisterWidgetRecursive(widgetIt->get());
        m_Widgets.erase(widgetIt);
        m_BatchesDirty = true;
        m_LayoutDirty = true;
    }

    Widget* WidgetSystem::FindWidget(const std::string& id) {
        auto widgetIt = m_WidgetLookup.find(id);
        return widgetIt != m_WidgetLookup.end() ? widgetIt->second : nullptr;
    }

    void WidgetSystem::ClearWidgets() {
        m_Widgets.clear();
        m_WidgetLookup.clear();
        m_HoveredWidget = nullptr;
        m_FocusedWidget = nullptr;
        m_BatchesDirty = true;
        m_LayoutDirty = true;
    }

    void WidgetSystem::Update(float deltaTime) {
        if (m_LayoutDirty) {
            UpdateLayout();
            m_LayoutDirty = false;
        }

        for (const std::shared_ptr<Widget>& widget : m_Widgets) {
            if (widget != nullptr) {
                widget->Update(deltaTime);
            }
        }

        m_BatchesDirty = true;
    }

    void WidgetSystem::CollectRenderCommands(Widget* widget, std::vector<WidgetRenderCommand>& commands) {
        if (widget == nullptr || !widget->IsVisible()) {
            return;
        }

        WidgetRenderCommand command;
        command.widget = widget;
        command.screenPosition = CalculateScreenPosition(widget);
        command.screenSize = widget->GetSize();
        command.alpha = widget->GetAlpha();
        command.textureId = 0;
        command.shaderId = 0;
        commands.push_back(command);

        for (const std::shared_ptr<Widget>& child : widget->GetChildren()) {
            CollectRenderCommands(child.get(), commands);
        }
    }

    void WidgetSystem::GenerateBatchGeometry(const WidgetRenderCommand& cmd, WidgetBatch& batch) {
        const glm::vec2 pos = cmd.screenPosition;
        const glm::vec2 size = cmd.screenSize;
        const glm::vec4 color{1.0f, 1.0f, 1.0f, cmd.alpha};

        const uint32_t baseIndex = static_cast<uint32_t>(batch.vertices.size());
        batch.vertices.push_back({pos, {0.0f, 0.0f}, color});
        batch.vertices.push_back({{pos.x + size.x, pos.y}, {1.0f, 0.0f}, color});
        batch.vertices.push_back({{pos.x + size.x, pos.y + size.y}, {1.0f, 1.0f}, color});
        batch.vertices.push_back({{pos.x, pos.y + size.y}, {0.0f, 1.0f}, color});

        batch.indices.push_back(baseIndex + 0);
        batch.indices.push_back(baseIndex + 1);
        batch.indices.push_back(baseIndex + 2);
        batch.indices.push_back(baseIndex + 0);
        batch.indices.push_back(baseIndex + 2);
        batch.indices.push_back(baseIndex + 3);
    }

    void WidgetSystem::BatchRenderCommands(const std::vector<WidgetRenderCommand>& commands) {
        std::vector<WidgetRenderCommand> sorted = commands;
        std::sort(
            sorted.begin(),
            sorted.end(),
            [](const WidgetRenderCommand& lhs, const WidgetRenderCommand& rhs) {
                if (lhs.widget->GetZOrder() != rhs.widget->GetZOrder()) {
                    return lhs.widget->GetZOrder() < rhs.widget->GetZOrder();
                }
                if (lhs.textureId != rhs.textureId) {
                    return lhs.textureId < rhs.textureId;
                }
                return lhs.shaderId < rhs.shaderId;
            });

        std::unordered_map<WidgetBatchKey, WidgetBatch, WidgetBatchKeyHash> batchMap;
        for (const WidgetRenderCommand& command : sorted) {
            WidgetBatchKey key{command.textureId, command.shaderId, command.widget->GetZOrder()};
            WidgetBatch& batch = batchMap[key];
            batch.key = key;
            batch.commands.push_back(command);
            GenerateBatchGeometry(command, batch);
        }

        m_Batches.clear();
        m_Batches.reserve(batchMap.size());
        for (auto& [key, batch] : batchMap) {
            (void)key;
            m_Batches.push_back(std::move(batch));
        }

        std::sort(
            m_Batches.begin(),
            m_Batches.end(),
            [](const WidgetBatch& lhs, const WidgetBatch& rhs) {
                return lhs.key.zOrder < rhs.key.zOrder;
            });
    }

    void WidgetSystem::BuildBatches() {
        if (!m_BatchesDirty) {
            return;
        }

        std::vector<WidgetRenderCommand> commands;
        for (const std::shared_ptr<Widget>& widget : m_Widgets) {
            CollectRenderCommands(widget.get(), commands);
        }

        BatchRenderCommands(commands);
        m_BatchesDirty = false;
    }

    void WidgetSystem::Render() {
        BuildBatches();
    }

    void WidgetSystem::CollectHitTestWidgets(Widget* widget, std::vector<Widget*>& outWidgets) {
        if (widget == nullptr || !widget->IsVisible()) {
            return;
        }
        outWidgets.push_back(widget);
        for (const std::shared_ptr<Widget>& child : widget->GetChildren()) {
            CollectHitTestWidgets(child.get(), outWidgets);
        }
    }

    Widget* WidgetSystem::HitTest(const glm::vec2& position) {
        std::vector<Widget*> candidates;
        for (const std::shared_ptr<Widget>& rootWidget : m_Widgets) {
            CollectHitTestWidgets(rootWidget.get(), candidates);
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const Widget* lhs, const Widget* rhs) {
                return lhs->GetZOrder() > rhs->GetZOrder();
            });

        for (Widget* candidate : candidates) {
            if (candidate->ContainsPoint(position, m_ScreenSize)) {
                return candidate;
            }
        }
        return nullptr;
    }

    void WidgetSystem::OnMouseMove(const glm::vec2& position) {
        m_MousePosition = position;

        Widget* hoveredWidget = HitTest(position);
        if (hoveredWidget == m_HoveredWidget) {
            return;
        }

        if (m_HoveredWidget != nullptr) {
            m_HoveredWidget->SetHovered(false);
        }
        m_HoveredWidget = hoveredWidget;
        if (m_HoveredWidget != nullptr) {
            m_HoveredWidget->SetHovered(true);
        }
    }

    void WidgetSystem::OnMouseButton(int button, bool pressed) {
        if (button != 0 || !pressed) {
            return;
        }

        if (m_HoveredWidget != nullptr && m_HoveredWidget->IsInteractive()) {
            if (m_FocusedWidget != nullptr && m_FocusedWidget != m_HoveredWidget) {
                m_FocusedWidget->SetFocused(false);
            }
            m_FocusedWidget = m_HoveredWidget;
            m_FocusedWidget->SetFocused(true);
            return;
        }

        if (m_FocusedWidget != nullptr) {
            m_FocusedWidget->SetFocused(false);
            m_FocusedWidget = nullptr;
        }
    }

    void WidgetSystem::OnKeyEvent(int key, bool pressed) {
        (void)key;
        (void)pressed;
    }

    bool WidgetSystem::SetWidgetProperty(
        const std::string& widgetId,
        std::string_view propertyPath,
        const WidgetPropertyValue& value) {
        Widget* widget = FindWidget(widgetId);
        if (widget == nullptr) {
            return false;
        }
        return widget->SetPropertyValue(propertyPath, value);
    }

    std::optional<WidgetSystem::WidgetPropertyValue> WidgetSystem::GetWidgetProperty(
        const std::string& widgetId,
        std::string_view propertyPath) const {
        auto widgetIt = m_WidgetLookup.find(widgetId);
        if (widgetIt == m_WidgetLookup.end() || widgetIt->second == nullptr) {
            return std::nullopt;
        }
        return widgetIt->second->GetPropertyValue(propertyPath);
    }

    void WidgetSystem::PrewarmWidgetPool(const std::string& widgetType, uint32_t count) {
        std::vector<std::shared_ptr<Widget>>& pool = m_WidgetPool[widgetType];
        for (uint32_t index = 0; index < count; ++index) {
            std::unique_ptr<Widget> created = WidgetRegistry::Get().CreateWidget(widgetType);
            if (!created) {
                ENGINE_CORE_WARN("WidgetSystem: Failed to prewarm widget type '{}'", widgetType);
                return;
            }
            pool.push_back(std::shared_ptr<Widget>(created.release()));
        }
    }

    std::shared_ptr<Widget> WidgetSystem::CheckoutWidget(const std::string& widgetType) {
        std::vector<std::shared_ptr<Widget>>& pool = m_WidgetPool[widgetType];
        if (!pool.empty()) {
            std::shared_ptr<Widget> widget = pool.back();
            pool.pop_back();
            return widget;
        }

        std::unique_ptr<Widget> created = WidgetRegistry::Get().CreateWidget(widgetType);
        if (!created) {
            return nullptr;
        }
        return std::shared_ptr<Widget>(created.release());
    }

    void WidgetSystem::CheckinWidget(const std::string& widgetType, std::shared_ptr<Widget> widget) {
        if (!widget) {
            return;
        }
        widget->SetVisible(true);
        widget->SetAlpha(1.0f);
        widget->SetHovered(false);
        widget->SetFocused(false);
        widget->ClearChildren();
        m_WidgetPool[widgetType].push_back(std::move(widget));
    }

    void WidgetSystem::UpdateLayout() {
        for (const std::shared_ptr<Widget>& widget : m_Widgets) {
            if (widget != nullptr) {
                widget->MarkDirty();
            }
        }
    }

    glm::vec2 WidgetSystem::CalculateScreenPosition(Widget* widget) const {
        if (widget == nullptr) {
            return {0.0f, 0.0f};
        }
        return widget->CalculateScreenPosition(m_ScreenSize);
    }

    bool WidgetSystem::RunContractSelfTest(std::vector<std::string>* failures) const {
        bool success = true;
        auto reportFailure = [&](const std::string& message) {
            success = false;
            if (failures != nullptr) {
                failures->push_back(message);
            }
        };

        std::shared_ptr<Widget> root = std::make_shared<Widget>("test-root");
        root->SetAnchor(Anchor::TopLeft);
        root->SetPosition({100.0f, 100.0f});
        root->SetSize({200.0f, 200.0f});

        std::shared_ptr<Widget> child = std::make_shared<Widget>("test-child");
        child->SetPosition({10.0f, 10.0f});
        child->SetSize({20.0f, 20.0f});
        root->AddChild(child);

        if (root->GetChildren().size() != 1) {
            reportFailure("Widget hierarchy traversal failed.");
        }

        if (!child->ContainsPoint({111.0f, 111.0f}, {1920.0f, 1080.0f})) {
            reportFailure("Widget hit testing failed for child bounds.");
        }

        return success;
    }

} // namespace Widgets
} // namespace UI
} // namespace Core

