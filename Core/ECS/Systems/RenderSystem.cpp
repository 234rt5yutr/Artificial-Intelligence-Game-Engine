#include "Core/ECS/Systems/RenderSystem.h"

namespace Core {
namespace ECS {

    void RenderSystem::Update(Scene& scene)
    {
        PROFILE_FUNCTION();

        m_DrawCommands.clear();
        m_VisibleEntityCount = 0;
        m_TotalEntityCount = 0;

        auto view = scene.View<TransformComponent, MeshComponent>();

        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& mesh = view.get<MeshComponent>(entity);

            m_TotalEntityCount++;

            // Skip if mesh is invalid or not visible
            if (!mesh.IsValid() || !mesh.Visible) {
                continue;
            }

            // Apply custom visibility test if set (e.g., frustum culling)
            if (m_VisibilityTest && !m_VisibilityTest(transform.WorldMatrix, mesh.MeshData.get())) {
                continue;
            }

            // Create draw command
            DrawCommand cmd;
            cmd.Mesh = mesh.MeshData.get();
            cmd.Transform = transform.WorldMatrix;
            cmd.MaterialIndex = mesh.MaterialIndex;
            cmd.CastShadows = mesh.CastShadows;

            m_DrawCommands.push_back(cmd);
            m_VisibleEntityCount++;
        }

        ENGINE_CORE_TRACE("RenderSystem: {} visible / {} total entities", 
                          m_VisibleEntityCount, m_TotalEntityCount);
    }

} // namespace ECS
} // namespace Core
