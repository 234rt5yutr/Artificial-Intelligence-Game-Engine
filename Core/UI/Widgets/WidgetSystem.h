#pragma once

#include "Widget.h"
#include <vector>
#include <memory>
#include <unordered_map>
#include <queue>
#include <functional>

namespace Core {
namespace UI {
namespace Widgets {

    /// @brief Batch key for grouping widgets with same render state
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

    /// @brief Hash function for WidgetBatchKey
    struct WidgetBatchKeyHash {
        size_t operator()(const WidgetBatchKey& key) const {
            return std::hash<uint32_t>{}(key.textureId) ^
                   (std::hash<uint32_t>{}(key.shaderId) << 1) ^
                   (std::hash<int32_t>{}(key.zOrder) << 2);
        }
    };

    /// @brief Vertex for batched widget rendering
    struct BatchVertex {
        glm::vec2 position;
        glm::vec2 texCoord;
        glm::vec4 color;
    };

    /// @brief Render command for a single widget
    struct WidgetRenderCommand {
        Widget* widget = nullptr;
        glm::vec2 screenPosition;
        glm::vec2 screenSize;
        float alpha = 1.0f;
        uint32_t textureId = 0;
        uint32_t shaderId = 0;
    };

    /// @brief Batch of widgets sharing render state
    struct WidgetBatch {
        WidgetBatchKey key;
        std::vector<WidgetRenderCommand> commands;
        std::vector<BatchVertex> vertices;
        std::vector<uint32_t> indices;
    };

    /// @brief Widget system for batched rendering
    /// 
    /// WidgetSystem handles:
    /// - Widget tree management (parent/child relationships)
    /// - Batched rendering by texture/shader/z-order
    /// - Event dispatch and hit testing
    /// - Widget layout calculations
    class WidgetSystem {
    public:
        static WidgetSystem& Get();

        // =====================================================================
        // Initialization
        // =====================================================================

        void Initialize();
        void Shutdown();

        // =====================================================================
        // Widget Management
        // =====================================================================

        /// @brief Add a root-level widget
        void AddWidget(std::shared_ptr<Widget> widget);

        /// @brief Remove a widget
        void RemoveWidget(const std::string& id);

        /// @brief Find widget by ID
        Widget* FindWidget(const std::string& id);

        /// @brief Get all widgets
        const std::vector<std::shared_ptr<Widget>>& GetWidgets() const { return m_Widgets; }

        /// @brief Clear all widgets
        void ClearWidgets();

        // =====================================================================
        // Update & Render
        // =====================================================================

        /// @brief Update all widgets
        void Update(float deltaTime);

        /// @brief Build render batches
        void BuildBatches();

        /// @brief Get render batches
        const std::vector<WidgetBatch>& GetBatches() const { return m_Batches; }

        /// @brief Render all batches (call after BuildBatches)
        void Render();

        // =====================================================================
        // Input Handling
        // =====================================================================

        /// @brief Process mouse move event
        void OnMouseMove(const glm::vec2& position);

        /// @brief Process mouse button event
        void OnMouseButton(int button, bool pressed);

        /// @brief Process keyboard event
        void OnKeyEvent(int key, bool pressed);

        /// @brief Get widget under cursor
        Widget* GetWidgetUnderCursor() const { return m_HoveredWidget; }

        // =====================================================================
        // Layout
        // =====================================================================

        /// @brief Set screen size for layout calculations
        void SetScreenSize(const glm::vec2& size) { m_ScreenSize = size; m_LayoutDirty = true; }

        /// @brief Get screen size
        glm::vec2 GetScreenSize() const { return m_ScreenSize; }

        /// @brief Mark layout as dirty
        void InvalidateLayout() { m_LayoutDirty = true; }

        /// @brief Force layout recalculation
        void UpdateLayout();

    private:
        WidgetSystem() = default;
        ~WidgetSystem() = default;

        // Delete copy/move
        WidgetSystem(const WidgetSystem&) = delete;
        WidgetSystem& operator=(const WidgetSystem&) = delete;

        /// @brief Collect render commands from widget tree
        void CollectRenderCommands(Widget* widget, std::vector<WidgetRenderCommand>& commands);

        /// @brief Sort and batch render commands
        void BatchRenderCommands(const std::vector<WidgetRenderCommand>& commands);

        /// @brief Generate vertices for a command
        void GenerateBatchGeometry(const WidgetRenderCommand& cmd, WidgetBatch& batch);

        /// @brief Hit test widgets
        Widget* HitTest(const glm::vec2& position);

        /// @brief Calculate widget screen position
        glm::vec2 CalculateScreenPosition(Widget* widget) const;

    private:
        bool m_Initialized = false;
        
        std::vector<std::shared_ptr<Widget>> m_Widgets;
        std::unordered_map<std::string, Widget*> m_WidgetLookup;
        
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
