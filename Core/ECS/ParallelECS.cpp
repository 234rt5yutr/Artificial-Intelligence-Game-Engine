#include "Core/ECS/ParallelECS.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/HierarchyComponent.h"
#include "Core/Log.h"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

// Use engine core log macros
#define LOG_INFO    ENGINE_CORE_INFO
#define LOG_WARN    ENGINE_CORE_WARN
#define LOG_DEBUG   ENGINE_CORE_TRACE
#define LOG_ERROR   ENGINE_CORE_ERROR

namespace Core {
namespace ECS {

    //=========================================================================
    // ParallelTransformHelper Implementation
    //=========================================================================

    void ParallelTransformHelper::SortByDepth(entt::registry& registry, std::vector<entt::entity>& entities)
    {
        PROFILE_FUNCTION();

        std::sort(entities.begin(), entities.end(),
            [&registry](entt::entity a, entt::entity b) {
                auto* hierA = registry.try_get<HierarchyComponent>(a);
                auto* hierB = registry.try_get<HierarchyComponent>(b);
                
                uint32_t depthA = hierA ? hierA->Depth : 0;
                uint32_t depthB = hierB ? hierB->Depth : 0;
                
                return depthA < depthB;
            });
    }

    std::vector<std::vector<entt::entity>> ParallelTransformHelper::GetParallelBatches(
        entt::registry& registry,
        const std::vector<entt::entity>& sortedEntities)
    {
        PROFILE_FUNCTION();

        std::vector<std::vector<entt::entity>> batches;
        
        if (sortedEntities.empty()) {
            return batches;
        }

        // Group entities by depth level - entities at same depth can be processed in parallel
        // as long as they don't share the same parent
        
        uint32_t currentDepth = 0;
        std::vector<entt::entity> currentBatch;
        currentBatch.reserve(DEFAULT_BATCH_SIZE);

        for (entt::entity entity : sortedEntities) {
            auto* hier = registry.try_get<HierarchyComponent>(entity);
            uint32_t depth = hier ? hier->Depth : 0;

            if (depth != currentDepth && !currentBatch.empty()) {
                batches.push_back(std::move(currentBatch));
                currentBatch = std::vector<entt::entity>();
                currentBatch.reserve(DEFAULT_BATCH_SIZE);
                currentDepth = depth;
            }

            currentBatch.push_back(entity);
        }

        if (!currentBatch.empty()) {
            batches.push_back(std::move(currentBatch));
        }

        return batches;
    }

    void ParallelTransformHelper::UpdateTransformsParallel(Scene& scene)
    {
        PROFILE_FUNCTION();

        auto& registry = scene.GetRegistry();

        // First: Update all root transforms (no hierarchy component or no parent)
        // These can be fully parallelized
        {
            auto rootView = registry.view<TransformComponent>(entt::exclude<HierarchyComponent>);
            std::vector<entt::entity> rootEntities;
            rootEntities.reserve(rootView.size_hint());

            for (auto entity : rootView) {
                auto& transform = rootView.get<TransformComponent>(entity);
                if (transform.IsDirty) {
                    rootEntities.push_back(entity);
                }
            }

            if (!rootEntities.empty()) {
                uint32_t count = static_cast<uint32_t>(rootEntities.size());
                
                if (count > DEFAULT_BATCH_SIZE) {
                    JobSystem::Context ctx;
                    JobSystem::Dispatch(ctx, count, DEFAULT_BATCH_SIZE, [&](uint32_t index) {
                        entt::entity entity = rootEntities[index];
                        auto& transform = registry.get<TransformComponent>(entity);
                        transform.WorldMatrix = transform.GetLocalMatrix();
                        transform.IsDirty = false;
                    });
                    JobSystem::Wait(ctx);
                } else {
                    for (auto entity : rootEntities) {
                        auto& transform = registry.get<TransformComponent>(entity);
                        transform.WorldMatrix = transform.GetLocalMatrix();
                        transform.IsDirty = false;
                    }
                }
            }
        }

        // Second: Process hierarchical entities by depth level
        // Entities at the same depth level can be processed in parallel
        {
            auto hierarchyView = registry.view<TransformComponent, HierarchyComponent>();
            std::vector<entt::entity> hierarchicalEntities;
            hierarchicalEntities.reserve(hierarchyView.size_hint());

            for (auto entity : hierarchyView) {
                hierarchicalEntities.push_back(entity);
            }

            if (hierarchicalEntities.empty()) {
                return;
            }

            // Sort by depth
            SortByDepth(registry, hierarchicalEntities);

            // Get batches by depth level
            auto batches = GetParallelBatches(registry, hierarchicalEntities);

            // Process each depth level
            for (auto& batch : batches) {
                uint32_t count = static_cast<uint32_t>(batch.size());

                if (count > DEFAULT_BATCH_SIZE) {
                    // Parallel processing for this depth level
                    JobSystem::Context ctx;
                    JobSystem::Dispatch(ctx, count, DEFAULT_BATCH_SIZE, [&](uint32_t index) {
                        entt::entity entity = batch[index];
                        auto& transform = registry.get<TransformComponent>(entity);
                        auto& hierarchy = registry.get<HierarchyComponent>(entity);

                        if (hierarchy.HasParent()) {
                            auto* parentTransform = registry.try_get<TransformComponent>(hierarchy.Parent);
                            if (parentTransform) {
                                transform.WorldMatrix = parentTransform->WorldMatrix * transform.GetLocalMatrix();
                            } else {
                                transform.WorldMatrix = transform.GetLocalMatrix();
                            }
                        } else {
                            transform.WorldMatrix = transform.GetLocalMatrix();
                        }
                        transform.IsDirty = false;
                    });
                    JobSystem::Wait(ctx);
                } else {
                    // Sequential processing for small batches
                    for (auto entity : batch) {
                        auto& transform = registry.get<TransformComponent>(entity);
                        auto& hierarchy = registry.get<HierarchyComponent>(entity);

                        if (hierarchy.HasParent()) {
                            auto* parentTransform = registry.try_get<TransformComponent>(hierarchy.Parent);
                            if (parentTransform) {
                                transform.WorldMatrix = parentTransform->WorldMatrix * transform.GetLocalMatrix();
                            } else {
                                transform.WorldMatrix = transform.GetLocalMatrix();
                            }
                        } else {
                            transform.WorldMatrix = transform.GetLocalMatrix();
                        }
                        transform.IsDirty = false;
                    }
                }
            }
        }

        LOG_DEBUG("ParallelTransformHelper: Completed parallel transform update");
    }

} // namespace ECS
} // namespace Core
