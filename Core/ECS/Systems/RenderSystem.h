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
        bool VirtualGeometryFallback;   // 1 byte
        bool VirtualPagesUnavailable;   // 1 byte
        uint8_t Padding;                // 1 byte padding
        // Range into RenderSystem::GetBoneMatrices for a skinned mesh; count 0
        // means static geometry. An index rather than a pointer, because the
        // frame packet is copied across the sim/render thread boundary and a
        // pointer into component storage would be a race.
        uint32_t BoneOffset;            // 4 bytes
        uint32_t BoneCount;             // 4 bytes
    };
    static_assert(sizeof(DrawCommand) == 88, "DrawCommand size check");

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

        // Skinning matrices for every skeletal draw command this frame, packed
        // back to back and indexed by DrawCommand::BoneOffset.
        const std::vector<Math::Mat4>& GetBoneMatrices() const { return m_BoneMatrices; }

        // Clear draw commands (call after rendering)
        void ClearDrawCommands() { m_DrawCommands.clear(); m_BoneMatrices.clear(); }

        // Statistics
        uint32_t GetVisibleEntityCount() const { return m_VisibleEntityCount; }
        uint32_t GetTotalEntityCount() const { return m_TotalEntityCount; }

        // Optional: Set custom visibility test (e.g., frustum culling)
        using VisibilityTest = std::function<bool(const Math::Mat4&, const Renderer::Mesh*)>;
        void SetVisibilityTest(VisibilityTest test) { m_VisibilityTest = test; }

    private:
        // Resolves local poses to skinning matrices and emits skeletal draw
        // commands. Sequential in both update modes: skeletal entities are far
        // rarer than static ones, and the bone array has to stay packed.
        void CollectSkeletalDrawCommands(Scene& scene);

        std::vector<DrawCommand> m_DrawCommands;
        std::vector<Math::Mat4> m_BoneMatrices;
        VisibilityTest m_VisibilityTest;
        std::atomic<uint32_t> m_VisibleEntityCount{0};
        std::atomic<uint32_t> m_TotalEntityCount{0};
        std::mutex m_DrawCommandsMutex;

        // Thread-local buffers for parallel collection
        ThreadLocalScratch<std::vector<DrawCommand>> m_ThreadLocalCommands;
    };

} // namespace ECS
} // namespace Core
