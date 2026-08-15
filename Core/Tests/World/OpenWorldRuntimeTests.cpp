// OpenWorldRuntime unit tests: validates world partition streaming, LRU memory budgeting,
// HLOD clustering, and large-world coordinate origin rebasing.

#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/World/OpenWorldRuntime.h"
#include "Core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n",                 \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

int main() {
    using namespace Core;
    using namespace Core::World;

    Engine::Log::Init();

    OpenWorldRuntime& runtime = OpenWorldRuntime::Get();

    // 1. Streaming Budget Configuration
    {
        StreamingBudgetConfig budget;
        budget.MaxResidentCells = 4;
        budget.MaxResidentMemoryBytes = 1000;
        runtime.SetStreamingBudget(budget);

        StreamingBudgetConfig retrieved = runtime.GetStreamingBudget();
        CHECK(retrieved.MaxResidentCells == 4);
        CHECK(retrieved.MaxResidentMemoryBytes == 1000);
    }

    // 2. Cell Stream In with Dependencies
    {
        PartitionCellStreamInRequest req;
        req.Cell.CellId = "Cell_0_0";
        req.Cell.Coordinate = {0, 0};
        req.Cell.EstimatedMemoryBytes = 200;
        req.Cell.Dependencies = {"Cell_Deps_A", "Cell_Deps_B"};

        auto res = runtime.StreamWorldPartitionCellIn(req);
        CHECK(res.Ok);
        CHECK(res.Value.CellId == "Cell_0_0");
        CHECK(res.Value.HydratedDependencyCount == 2);
        CHECK(runtime.IsCellResident("Cell_0_0"));
        CHECK(runtime.IsCellResident("Cell_Deps_A"));
        CHECK(runtime.IsCellResident("Cell_Deps_B"));
        CHECK(runtime.GetResidentCellCount() == 3);
    }

    // 3. Stream Out
    {
        PartitionCellStreamOutRequest outReq;
        outReq.CellId = "Cell_0_0";
        auto outRes = runtime.StreamWorldPartitionCellOut(outReq);
        CHECK(outRes.Ok);
        CHECK(!runtime.IsCellResident("Cell_0_0"));
    }

    // 4. HLOD Clustering
    {
        HLODBuildRequest hlodReq;
        hlodReq.LayerId = "HLOD_Terrain_LOD1";
        hlodReq.SourceCellIds = {"Cell_0_0", "Cell_0_1", "Cell_1_0", "Cell_1_1", "Cell_2_0"};
        hlodReq.ClusterSize = 2;
        hlodReq.OutputAssetPath = "Assets/HLOD/lod1.json";

        auto hlodRes = runtime.BuildHierarchicalLOD(hlodReq);
        CHECK(hlodRes.Ok);
        CHECK(hlodRes.Value.Clusters.size() == 3);
        CHECK(hlodRes.Value.Clusters[0].SourceCells.size() == 2);
        CHECK(hlodRes.Value.Clusters[1].SourceCells.size() == 2);
        CHECK(hlodRes.Value.Clusters[2].SourceCells.size() == 1);
    }

    // 5. Large-World Coordinate Origin Rebasing
    {
        ECS::Scene testScene("RebaseTestScene");
        auto entityA = testScene.CreateEntity("EntityA");
        auto& transA = entityA.AddComponent<ECS::TransformComponent>();
        transA.Position = glm::vec3(1000.0f, 50.0f, 2000.0f);

        auto entityB = testScene.CreateEntity("EntityB");
        auto& transB = entityB.AddComponent<ECS::TransformComponent>();
        transB.Position = glm::vec3(-500.0f, 10.0f, 300.0f);

        WorldOriginRebaseRequest rebaseReq;
        rebaseReq.Scene = &testScene;
        rebaseReq.NewOrigin = Math::Vec3(1000.0f, 0.0f, 2000.0f);

        auto rebaseRes = runtime.RebaseWorldOrigin(rebaseReq);
        CHECK(rebaseRes.Ok);
        CHECK(rebaseRes.Value.ShiftedEntityCount == 2);
        CHECK(runtime.GetCurrentWorldOrigin().x == 1000.0f);
        CHECK(runtime.GetCurrentWorldOrigin().z == 2000.0f);

        // After shifting by -rebaseDelta (-1000, 0, -2000):
        // EntityA position should now be (0, 50, 0) relative to local camera precision center
        CHECK(std::abs(transA.Position.x - 0.0f) < 0.001f);
        CHECK(std::abs(transA.Position.y - 50.0f) < 0.001f);
        CHECK(std::abs(transA.Position.z - 0.0f) < 0.001f);

        // EntityB position should now be (-1500, 10, -1700)
        CHECK(std::abs(transB.Position.x - (-1500.0f)) < 0.001f);
        CHECK(std::abs(transB.Position.y - 10.0f) < 0.001f);
        CHECK(std::abs(transB.Position.z - (-1700.0f)) < 0.001f);
    }

    return 0;
}
