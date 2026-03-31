#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/MeshComponent.h"
#include "Core/ECS/ParallelECS.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>

namespace Core {
namespace ECS {

    // Draw command structure for batching (cache-optimized)
    struct DrawCommand {
        Math::Mat4 Transform;           // 64 bytes - most accessed, put first
        const Renderer::Mesh* Mesh;     // 8 bytes
        uint32_t MaterialIndex;         // 4 bytes
        bool CastShadows;               // 1 byte
        uint8_t Padding[3];             // 3 bytes padding
    };
    static_assert(sizeof(DrawCommand) == 80, "DrawCommand size check");

    class RenderSystem : public ParallelSystemBase {
    public:
        RenderSystem() = default;
        ~RenderSystem() = default;

        // Collect draw commands from scene
        void Update(Scene& scene);

        // Parallel update mode
        void UpdateParallel(Scene& scene);

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
        std::atomic<uint32_t> m_VisibleEntityCount{0};
        std::atomic<uint32_t> m_TotalEntityCount{0};
        std::mutex m_DrawCommandsMutex;

        // Thread-local buffers for parallel collection
        ThreadLocalScratch<std::vector<DrawCommand>> m_ThreadLocalCommands;
    };

} // namespace ECS
} // namespace Core
