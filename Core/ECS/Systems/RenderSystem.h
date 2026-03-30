#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/MeshComponent.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <vector>
#include <functional>

namespace Core {
namespace ECS {

    // Draw command structure for batching
    struct DrawCommand {
        const Renderer::Mesh* Mesh;
        Math::Mat4 Transform;
        uint32_t MaterialIndex;
        bool CastShadows;
    };

    class RenderSystem {
    public:
        RenderSystem() = default;
        ~RenderSystem() = default;

        // Collect draw commands from scene
        void Update(Scene& scene);

        // Get collected draw commands for renderer consumption
        const std::vector<DrawCommand>& GetDrawCommands() const { return m_DrawCommands; }

        // Clear draw commands (call after rendering)
        void ClearDrawCommands() { m_DrawCommands.clear(); }

        // Statistics
        uint32_t GetVisibleEntityCount() const { return m_VisibleEntityCount; }
        uint32_t GetTotalEntityCount() const { return m_TotalEntityCount; }

        // Optional: Set custom visibility test (e.g., frustum culling)
        using VisibilityTest = std::function<bool(const Math::Mat4&, const Renderer::Mesh*)>;
        void SetVisibilityTest(VisibilityTest test) { m_VisibilityTest = test; }

    private:
        std::vector<DrawCommand> m_DrawCommands;
        VisibilityTest m_VisibilityTest;
        uint32_t m_VisibleEntityCount = 0;
        uint32_t m_TotalEntityCount = 0;
    };

} // namespace ECS
} // namespace Core
