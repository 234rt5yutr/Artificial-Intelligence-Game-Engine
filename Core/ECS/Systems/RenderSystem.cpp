#include "Core/ECS/Systems/RenderSystem.h"
#include "Core/ECS/Components/SkeletalMeshComponent.h"
#include "Core/JobSystem/JobSystem.h"

namespace Core {
namespace ECS {

    void RenderSystem::Update(Scene& scene)
    {
        PROFILE_FUNCTION();

        m_DrawCommands.clear();
        m_VisibleEntityCount = 0;
        m_TotalEntityCount = 0;

        auto view = scene.View<TransformComponent, MeshComponent>();

        // Reserve space based on view size hint
        m_DrawCommands.reserve(view.size_hint());

        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& mesh = view.get<MeshComponent>(entity);

            m_TotalEntityCount++;

            // Skip if mesh is invalid or not visible
            if (!mesh.IsValid() || !mesh.Visible) {
                continue;
            }

            if (mesh.AssetGeneration != mesh.LastBoundGeneration) {
                mesh.LastBoundGeneration = mesh.AssetGeneration;
            }

            // Apply custom visibility test if set (e.g., frustum culling)
            if (m_VisibilityTest && !m_VisibilityTest(transform.WorldMatrix, mesh.MeshData.get())) {
                continue;
            }

            // Create draw command
            DrawCommand cmd{};
            cmd.Mesh = mesh.MeshData.get();
            cmd.Transform = transform.WorldMatrix;
            cmd.PreviousTransform = transform.PreviousWorldMatrix;
            cmd.MaterialIndex = mesh.MaterialIndex;
            cmd.CastShadows = mesh.CastShadows;
            cmd.VirtualPagesUnavailable = false;
            cmd.VirtualGeometryFallback = false;

            if (cmd.Mesh->HasVirtualGeometry()) {
                cmd.VirtualPagesUnavailable = !cmd.Mesh->AreVirtualGeometryPagesResident();
                cmd.VirtualGeometryFallback =
                    cmd.Mesh->UsesVirtualGeometryFallback() || cmd.VirtualPagesUnavailable;
            }

            m_DrawCommands.push_back(cmd);
            m_VisibleEntityCount++;
        }

        CollectSkeletalDrawCommands(scene);

        ENGINE_CORE_TRACE("RenderSystem: {} visible / {} total entities", 
                          m_VisibleEntityCount.load(), m_TotalEntityCount.load());
    }

    void RenderSystem::UpdateParallel(Scene& scene)
    {
        PROFILE_FUNCTION();

        m_DrawCommands.clear();
        m_VisibleEntityCount = 0;
        m_TotalEntityCount = 0;
        m_ThreadLocalCommands.Reset();

        auto& registry = scene.GetRegistry();
        auto view = registry.view<TransformComponent, MeshComponent>();

        // Collect entities for parallel processing
        std::vector<entt::entity> entities;
        entities.reserve(view.size_hint());
        for (auto entity : view) {
            entities.push_back(entity);
        }

        if (entities.empty()) {
            return;
        }

        uint32_t entityCount = static_cast<uint32_t>(entities.size());
        m_TotalEntityCount = entityCount;

        // Check if parallel processing is worthwhile
        if (!ShouldRunParallel(entityCount)) {
            // Fall back to sequential
            Update(scene);
            return;
        }

        // Parallel collection into thread-local buffers
        JobSystem::Context ctx;
        JobSystem::Dispatch(ctx, entityCount, GetBatchSize(), [&](uint32_t index) {
            entt::entity entity = entities[index];
            auto& transform = registry.get<TransformComponent>(entity);
            auto& mesh = registry.get<MeshComponent>(entity);

            // Skip invalid/invisible
            if (!mesh.IsValid() || !mesh.Visible) {
                return;
            }

            if (mesh.AssetGeneration != mesh.LastBoundGeneration) {
                mesh.LastBoundGeneration = mesh.AssetGeneration;
            }

            // Visibility test (thread-safe if the test function is)
            if (m_VisibilityTest && !m_VisibilityTest(transform.WorldMatrix, mesh.MeshData.get())) {
                return;
            }

            // Create draw command in thread-local buffer
            DrawCommand cmd{};
            cmd.Mesh = mesh.MeshData.get();
            cmd.Transform = transform.WorldMatrix;
            cmd.PreviousTransform = transform.PreviousWorldMatrix;
            cmd.MaterialIndex = mesh.MaterialIndex;
            cmd.CastShadows = mesh.CastShadows;
            cmd.VirtualPagesUnavailable = false;
            cmd.VirtualGeometryFallback = false;

            if (cmd.Mesh->HasVirtualGeometry()) {
                cmd.VirtualPagesUnavailable = !cmd.Mesh->AreVirtualGeometryPagesResident();
                cmd.VirtualGeometryFallback =
                    cmd.Mesh->UsesVirtualGeometryFallback() || cmd.VirtualPagesUnavailable;
            }

            auto& localCommands = m_ThreadLocalCommands.Get();
            localCommands.push_back(cmd);

            m_VisibleEntityCount.fetch_add(1, std::memory_order_relaxed);
        });
        JobSystem::Wait(ctx);

        // Merge thread-local buffers into main draw commands
        // This is done sequentially but is typically fast
        m_DrawCommands = m_ThreadLocalCommands.Aggregate(
            [](std::vector<DrawCommand> acc, const std::vector<DrawCommand>& threadLocal) {
                acc.insert(acc.end(), threadLocal.begin(), threadLocal.end());
                return acc;
            });

        CollectSkeletalDrawCommands(scene);

        ENGINE_CORE_TRACE("RenderSystem (parallel): {} visible / {} total entities", 
                          m_VisibleEntityCount.load(), m_TotalEntityCount.load());
    }

    void RenderSystem::CollectSkeletalDrawCommands(Scene& scene)
    {
        PROFILE_FUNCTION();

        // Cleared here rather than in each caller: this is the only producer.
        m_BoneMatrices.clear();

        auto view = scene.View<TransformComponent, SkeletalMeshComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& skeletal = view.get<SkeletalMeshComponent>(entity);

            m_TotalEntityCount++;
            if (!skeletal.MeshData || !skeletal.Visible) {
                continue;
            }
            if (m_VisibilityTest && !m_VisibilityTest(transform.WorldMatrix, skeletal.MeshData.get())) {
                continue;
            }

            const Renderer::Skeleton& skeleton = skeletal.MeshData->GetSkeleton();
            const uint32_t boneCount = skeleton.GetBoneCount();

            DrawCommand cmd{};
            cmd.Mesh = skeletal.MeshData.get();
            cmd.Transform = transform.WorldMatrix;
            cmd.PreviousTransform = transform.PreviousWorldMatrix;
            cmd.MaterialIndex = skeletal.MaterialIndex;
            cmd.CastShadows = skeletal.CastShadows;
            cmd.BoneOffset = static_cast<uint32_t>(m_BoneMatrices.size());
            cmd.BoneCount = boneCount;

            if (boneCount > 0) {
                SkeletonPose& pose = skeletal.CurrentPose;
                // A component can exist with no pose evaluated yet. The bind
                // pose is the right answer then, and skinning by
                // globalPose * inverseBind reproduces it exactly.
                const bool hasPose = pose.LocalPoses.size() == boneCount;
                if (pose.SkinningMatrices.size() != boneCount) {
                    pose.Resize(boneCount);
                }

                // Local poses to global, then global times inverse bind. Bones
                // are stored parents first, so one forward sweep resolves the
                // whole hierarchy.
                for (uint32_t i = 0; i < boneCount; ++i) {
                    const Renderer::Bone& bone = skeleton.Bones[i];
                    const Math::Mat4 local = hasPose ? pose.LocalPoses[i].ToMatrix()
                                                     : bone.LocalTransform;
                    pose.GlobalPoses[i] = bone.ParentIndex >= 0
                        ? pose.GlobalPoses[bone.ParentIndex] * local
                        : local;
                    pose.SkinningMatrices[i] = pose.GlobalPoses[i] * bone.InverseBindMatrix;
                }
                m_BoneMatrices.insert(m_BoneMatrices.end(), pose.SkinningMatrices.begin(),
                                      pose.SkinningMatrices.end());
            }

            m_DrawCommands.push_back(cmd);
            m_VisibleEntityCount++;
        }
    }

} // namespace ECS
} // namespace Core
