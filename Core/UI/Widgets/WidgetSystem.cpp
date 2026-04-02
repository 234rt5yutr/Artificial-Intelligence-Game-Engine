#include "WidgetSystem.h"
#include <algorithm>

namespace Core {
namespace UI {
namespace Widgets {

// =============================================================================
// Singleton Access
// =============================================================================

WidgetSystem& WidgetSystem::Get() {
    static WidgetSystem instance;
    return instance;
}

// =============================================================================
// Initialization
// =============================================================================

void WidgetSystem::Initialize() {
    if (m_Initialized) return;
    m_Initialized = true;
}

void WidgetSystem::Shutdown() {
    if (!m_Initialized) return;
    
    ClearWidgets();
    m_Batches.clear();
    m_Initialized = false;
}

// =============================================================================
// Widget Management
// =============================================================================

void WidgetSystem::AddWidget(std::shared_ptr<Widget> widget) {
    if (!widget) return;
    
    m_Widgets.push_back(widget);
    m_WidgetLookup[widget->GetId()] = widget.get();
    m_LayoutDirty = true;
    m_BatchesDirty = true;
}

void WidgetSystem::RemoveWidget(const std::string& id) {
    auto it = std::find_if(m_Widgets.begin(), m_Widgets.end(),
        [&id](const std::shared_ptr<Widget>& w) { return w->GetId() == id; });
    
    if (it != m_Widgets.end()) {
        m_WidgetLookup.erase(id);
        m_Widgets.erase(it);
        m_BatchesDirty = true;
    }
}

Widget* WidgetSystem::FindWidget(const std::string& id) {
    auto it = m_WidgetLookup.find(id);
    return (it != m_WidgetLookup.end()) ? it->second : nullptr;
}

void WidgetSystem::ClearWidgets() {
    m_Widgets.clear();
    m_WidgetLookup.clear();
    m_HoveredWidget = nullptr;
    m_FocusedWidget = nullptr;
    m_BatchesDirty = true;
}

// =============================================================================
// Update & Render
// =============================================================================

void WidgetSystem::Update(float deltaTime) {
    if (m_LayoutDirty) {
        UpdateLayout();
        m_LayoutDirty = false;
    }
    
    for (auto& widget : m_Widgets) {
        if (widget) {
            widget->Update(deltaTime);
        }
    }
    
    m_BatchesDirty = true;
}

void WidgetSystem::BuildBatches() {
    if (!m_BatchesDirty) return;
    
    m_Batches.clear();
    
    // Collect all render commands
    std::vector<WidgetRenderCommand> commands;
    for (auto& widget : m_Widgets) {
        if (widget && widget->IsVisible()) {
            CollectRenderCommands(widget.get(), commands);
        }
    }
    
    // Sort and batch
    BatchRenderCommands(commands);
    
    m_BatchesDirty = false;
}

void WidgetSystem::CollectRenderCommands(Widget* widget, std::vector<WidgetRenderCommand>& commands) {
    if (!widget || !widget->IsVisible()) return;
    
    WidgetRenderCommand cmd;
    cmd.widget = widget;
    cmd.screenPosition = CalculateScreenPosition(widget);
    cmd.screenSize = widget->GetSize();
    cmd.alpha = widget->GetAlpha();
    cmd.textureId = 0;  // Default texture
    cmd.shaderId = 0;   // Default shader
    
    commands.push_back(cmd);
    
    // Collect children
    for (auto& child : widget->GetChildren()) {
        CollectRenderCommands(child.get(), commands);
    }
}

void WidgetSystem::BatchRenderCommands(const std::vector<WidgetRenderCommand>& commands) {
    // Sort by z-order, then by texture/shader
    std::vector<WidgetRenderCommand> sorted = commands;
    std::sort(sorted.begin(), sorted.end(), [](const WidgetRenderCommand& a, const WidgetRenderCommand& b) {
        if (a.widget->GetZOrder() != b.widget->GetZOrder()) {
            return a.widget->GetZOrder() < b.widget->GetZOrder();
        }
        if (a.textureId != b.textureId) {
            return a.textureId < b.textureId;
        }
        return a.shaderId < b.shaderId;
    });
    
    // Group into batches
    std::unordered_map<WidgetBatchKey, WidgetBatch, WidgetBatchKeyHash> batchMap;
    
    for (const auto& cmd : sorted) {
        WidgetBatchKey key{cmd.textureId, cmd.shaderId, cmd.widget->GetZOrder()};
        
        auto& batch = batchMap[key];
        batch.key = key;
        batch.commands.push_back(cmd);
        
        // Generate geometry
        GenerateBatchGeometry(cmd, batch);
    }
    
    // Convert to vector
    m_Batches.reserve(batchMap.size());
    for (auto& [key, batch] : batchMap) {
        m_Batches.push_back(std::move(batch));
    }
    
    // Sort batches by z-order
    std::sort(m_Batches.begin(), m_Batches.end(), [](const WidgetBatch& a, const WidgetBatch& b) {
        return a.key.zOrder < b.key.zOrder;
    });
}

void WidgetSystem::GenerateBatchGeometry(const WidgetRenderCommand& cmd, WidgetBatch& batch) {
    // Generate quad vertices
    glm::vec2 pos = cmd.screenPosition;
    glm::vec2 size = cmd.screenSize;
    glm::vec4 color{1.0f, 1.0f, 1.0f, cmd.alpha};
    
    uint32_t baseIndex = static_cast<uint32_t>(batch.vertices.size());
    
    // Top-left
    batch.vertices.push_back({pos, {0, 0}, color});
    // Top-right
    batch.vertices.push_back({{pos.x + size.x, pos.y}, {1, 0}, color});
    // Bottom-right
    batch.vertices.push_back({{pos.x + size.x, pos.y + size.y}, {1, 1}, color});
    // Bottom-left
    batch.vertices.push_back({{pos.x, pos.y + size.y}, {0, 1}, color});
    
    // Indices for two triangles
    batch.indices.push_back(baseIndex + 0);
    batch.indices.push_back(baseIndex + 1);
    batch.indices.push_back(baseIndex + 2);
    batch.indices.push_back(baseIndex + 0);
    batch.indices.push_back(baseIndex + 2);
    batch.indices.push_back(baseIndex + 3);
}

void WidgetSystem::Render() {
    BuildBatches();
    // Actual Vulkan rendering handled by UIManager
}

// =============================================================================
// Input Handling
// =============================================================================

void WidgetSystem::OnMouseMove(const glm::vec2& position) {
    m_MousePosition = position;
    
    Widget* newHovered = HitTest(position);
    if (newHovered != m_HoveredWidget) {
        if (m_HoveredWidget) {
            m_HoveredWidget->SetHovered(false);
        }
        m_HoveredWidget = newHovered;
        if (m_HoveredWidget) {
            m_HoveredWidget->SetHovered(true);
        }
    }
}

void WidgetSystem::OnMouseButton(int button, bool pressed) {
    if (button == 0 && pressed) { // Left click
        if (m_HoveredWidget && m_HoveredWidget->IsInteractive()) {
            if (m_FocusedWidget) {
                m_FocusedWidget->SetFocused(false);
            }
            m_FocusedWidget = m_HoveredWidget;
            m_FocusedWidget->SetFocused(true);
        } else {
            if (m_FocusedWidget) {
                m_FocusedWidget->SetFocused(false);
                m_FocusedWidget = nullptr;
            }
        }
    }
}

void WidgetSystem::OnKeyEvent(int key, bool pressed) {
    (void)key;
    (void)pressed;
    // Forward to focused widget if needed
}

Widget* WidgetSystem::HitTest(const glm::vec2& position) {
    // Test in reverse order (top to bottom)
    for (auto it = m_Widgets.rbegin(); it != m_Widgets.rend(); ++it) {
        if ((*it) && (*it)->IsVisible()) {
            glm::vec2 widgetPos = CalculateScreenPosition(it->get());
            glm::vec2 widgetSize = (*it)->GetSize();
            
            if (position.x >= widgetPos.x && position.x <= widgetPos.x + widgetSize.x &&
                position.y >= widgetPos.y && position.y <= widgetPos.y + widgetSize.y) {
                return it->get();
            }
        }
    }
    return nullptr;
}

// =============================================================================
// Layout
// =============================================================================

void WidgetSystem::UpdateLayout() {
    for (auto& widget : m_Widgets) {
        if (widget) {
            // Calculate anchored position
            glm::vec2 pos = widget->GetPosition();
            Anchor anchor = widget->GetAnchor();
            
            switch (anchor) {
                case Anchor::TopLeft:
                    // Position is already from top-left
                    break;
                case Anchor::TopCenter:
                    pos.x += m_ScreenSize.x / 2.0f;
                    break;
                case Anchor::TopRight:
                    pos.x += m_ScreenSize.x;
                    break;
                case Anchor::CenterLeft:
                    pos.y += m_ScreenSize.y / 2.0f;
                    break;
                case Anchor::Center:
                    pos.x += m_ScreenSize.x / 2.0f;
                    pos.y += m_ScreenSize.y / 2.0f;
                    break;
                case Anchor::CenterRight:
                    pos.x += m_ScreenSize.x;
                    pos.y += m_ScreenSize.y / 2.0f;
                    break;
                case Anchor::BottomLeft:
                    pos.y += m_ScreenSize.y;
                    break;
                case Anchor::BottomCenter:
                    pos.x += m_ScreenSize.x / 2.0f;
                    pos.y += m_ScreenSize.y;
                    break;
                case Anchor::BottomRight:
                    pos.x += m_ScreenSize.x;
                    pos.y += m_ScreenSize.y;
                    break;
            }
            
            // Store calculated screen position (would be stored in widget)
        }
    }
}

glm::vec2 WidgetSystem::CalculateScreenPosition(Widget* widget) const {
    if (!widget) return {0, 0};
    
    glm::vec2 pos = widget->GetPosition();
    Anchor anchor = widget->GetAnchor();
    Anchor pivot = widget->GetPivot();
    glm::vec2 size = widget->GetSize();
    
    // Apply anchor
    switch (anchor) {
        case Anchor::TopLeft:
            break;
        case Anchor::TopCenter:
            pos.x += m_ScreenSize.x / 2.0f;
            break;
        case Anchor::TopRight:
            pos.x += m_ScreenSize.x;
            break;
        case Anchor::CenterLeft:
            pos.y += m_ScreenSize.y / 2.0f;
            break;
        case Anchor::Center:
            pos.x += m_ScreenSize.x / 2.0f;
            pos.y += m_ScreenSize.y / 2.0f;
            break;
        case Anchor::CenterRight:
            pos.x += m_ScreenSize.x;
            pos.y += m_ScreenSize.y / 2.0f;
            break;
        case Anchor::BottomLeft:
            pos.y += m_ScreenSize.y;
            break;
        case Anchor::BottomCenter:
            pos.x += m_ScreenSize.x / 2.0f;
            pos.y += m_ScreenSize.y;
            break;
        case Anchor::BottomRight:
            pos.x += m_ScreenSize.x;
            pos.y += m_ScreenSize.y;
            break;
    }
    
    // Apply pivot
    switch (pivot) {
        case Anchor::TopLeft:
            break;
        case Anchor::TopCenter:
            pos.x -= size.x / 2.0f;
            break;
        case Anchor::TopRight:
            pos.x -= size.x;
            break;
        case Anchor::CenterLeft:
            pos.y -= size.y / 2.0f;
            break;
        case Anchor::Center:
            pos.x -= size.x / 2.0f;
            pos.y -= size.y / 2.0f;
            break;
        case Anchor::CenterRight:
            pos.x -= size.x;
            pos.y -= size.y / 2.0f;
            break;
        case Anchor::BottomLeft:
            pos.y -= size.y;
            break;
        case Anchor::BottomCenter:
            pos.x -= size.x / 2.0f;
            pos.y -= size.y;
            break;
        case Anchor::BottomRight:
            pos.x -= size.x;
            pos.y -= size.y;
            break;
    }
    
    return pos;
}

} // namespace Widgets
} // namespace UI
} // namespace Core
