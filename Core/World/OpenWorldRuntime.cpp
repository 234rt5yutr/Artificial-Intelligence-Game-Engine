#include "Core/World/OpenWorldRuntime.h"

#include "Core/Asset/AssetLoader.h"
#include "Core/ECS/Components/RigidBodyComponent.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Scene.h"
#include "Core/Log.h"
#include "Core/Network/ServerReplicationSystem.h"
#include "Core/Physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>

namespace Core {
namespace World {

OpenWorldRuntime& OpenWorldRuntime::Get() {
    static OpenWorldRuntime runtime;
    return runtime;
}

void OpenWorldRuntime::SetStreamingBudget(const StreamingBudgetConfig& config) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Budget = config;
}

StreamingBudgetConfig OpenWorldRuntime::GetStreamingBudget() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Budget;
}

Result<PartitionCellStreamResult> OpenWorldRuntime::StreamWorldPartitionCellIn(
    const PartitionCellStreamInRequest& request) {
    if (request.Cell.CellId.empty()) {
        return Result<PartitionCellStreamResult>::Failure("World.CellIdMissing");
    }

    uint64_t residentBytes = request.Cell.EstimatedMemoryBytes;
    if (residentBytes == 0 && !request.Cell.CellAssetPath.empty()) {
        std::error_code ec;
        residentBytes = std::filesystem::file_size(request.Cell.CellAssetPath, ec);
        if (ec) {
            residentBytes = 0;
        }
    }

    if (!request.Cell.CellAssetPath.empty()) {
        const Asset::LoadedStructuredAsset cellAsset =
            Asset::AssetLoader::LoadWorldPartitionCellAsset(request.Cell.CellAssetPath);
        if (!cellAsset.IsValid) {
            return Result<PartitionCellStreamResult>::Failure("World.CellAssetLoadFailed");
        }
    }

    PartitionCellStreamResult result;
    result.CellId = request.Cell.CellId;

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        ++m_FrameCounter;

        if (!EnsureBudgetForIncoming(residentBytes, request.Cell.CellId)) {
            return Result<PartitionCellStreamResult>::Failure("World.StreamingBudgetExceeded");
        }

        auto existing = m_ResidentCells.find(request.Cell.CellId);
        if (existing != m_ResidentCells.end()) {
            existing->second.LastTouchedFrame = m_FrameCounter;
            result.ResidentCellCount = static_cast<uint32_t>(m_ResidentCells.size());
            result.ResidentMemoryBytes = m_CurrentResidentMemoryBytes;
            return Result<PartitionCellStreamResult>::Success(result);
        }

        for (const std::string& dependency : request.Cell.Dependencies) {
            if (dependency.empty()) {
                continue;
            }

            m_CellDependents[dependency].insert(request.Cell.CellId);
            if (m_ResidentCells.find(dependency) == m_ResidentCells.end()) {
                ResidentCellState hydratedDependency;
                hydratedDependency.Descriptor.CellId = dependency;
                hydratedDependency.IsPlaceholder = true;
                hydratedDependency.LastTouchedFrame = m_FrameCounter;
                m_ResidentCells.emplace(dependency, std::move(hydratedDependency));
                ++result.HydratedDependencyCount;
            }
        }

        ResidentCellState state;
        state.Descriptor = request.Cell;
        state.Descriptor.EstimatedMemoryBytes = residentBytes;
        state.IsPlaceholder = false;
        state.LastTouchedFrame = m_FrameCounter;
        m_ResidentCells[request.Cell.CellId] = std::move(state);
        m_CurrentResidentMemoryBytes += residentBytes;

        result.ResidentCellCount = static_cast<uint32_t>(m_ResidentCells.size());
        result.ResidentMemoryBytes = m_CurrentResidentMemoryBytes;
    }

    ENGINE_CORE_INFO("World: streamed partition cell '{}' in (resident cells: {}, memory: {} bytes)",
                     result.CellId, result.ResidentCellCount, result.ResidentMemoryBytes);
    return Result<PartitionCellStreamResult>::Success(result);
}

Result<void> OpenWorldRuntime::StreamWorldPartitionCellOut(const PartitionCellStreamOutRequest& request) {
    if (request.CellId.empty()) {
        return Result<void>::Failure("World.CellIdMissing");
    }

    std::lock_guard<std::mutex> lock(m_Mutex);
    auto it = m_ResidentCells.find(request.CellId);
    if (it == m_ResidentCells.end()) {
        return Result<void>::Failure("World.CellNotResident");
    }

    const auto dependentsIt = m_CellDependents.find(request.CellId);
    if (!request.ForceEvict && dependentsIt != m_CellDependents.end() && !dependentsIt->second.empty()) {
        return Result<void>::Failure("World.CellHasDependents");
    }

    EvictCellInternal(request.CellId);
    ENGINE_CORE_INFO("World: streamed partition cell '{}' out", request.CellId);
    return Result<void>::Success();
}

Result<HLODBuildResult> OpenWorldRuntime::BuildHierarchicalLOD(const HLODBuildRequest& request) {
    if (request.SourceCellIds.empty()) {
        return Result<HLODBuildResult>::Failure("World.HLODSourceCellsMissing");
    }

    const uint32_t clusterSize = std::max(1u, request.ClusterSize);
    std::vector<std::string> sourceCells = request.SourceCellIds;
    std::sort(sourceCells.begin(), sourceCells.end());
    sourceCells.erase(std::unique(sourceCells.begin(), sourceCells.end()), sourceCells.end());

    HLODBuildResult result;
    result.OutputAssetPath = request.OutputAssetPath;
    if (result.OutputAssetPath.empty()) {
        result.OutputAssetPath = std::filesystem::path("build") / "generated" /
                                 (request.LayerId.empty() ? "world" : request.LayerId) / "runtime.hlod";
    }

    nlohmann::json root = {
        {"schemaVersion", 1},
        {"assetType", "hlod"},
        {"layerId", request.LayerId},
        {"clusters", nlohmann::json::array()}
    };

    for (size_t begin = 0; begin < sourceCells.size(); begin += clusterSize) {
        const size_t end = std::min(begin + clusterSize, sourceCells.size());
        HLODClusterInfo cluster;
        cluster.ClusterId = "hlod-cluster-" + std::to_string(result.Clusters.size());
        cluster.SourceCells.assign(sourceCells.begin() + static_cast<std::ptrdiff_t>(begin),
                                   sourceCells.begin() + static_cast<std::ptrdiff_t>(end));
        result.Clusters.push_back(cluster);

        root["clusters"].push_back({
            {"clusterId", cluster.ClusterId},
            {"sourceCells", cluster.SourceCells}
        });
    }

    std::error_code ec;
    if (!result.OutputAssetPath.parent_path().empty()) {
        std::filesystem::create_directories(result.OutputAssetPath.parent_path(), ec);
        if (ec) {
            return Result<HLODBuildResult>::Failure("World.HLODCreateDirectoryFailed");
        }
    }

    std::ofstream output(result.OutputAssetPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return Result<HLODBuildResult>::Failure("World.HLODOutputOpenFailed");
    }

    output << root.dump(2);
    if (!output.good()) {
        return Result<HLODBuildResult>::Failure("World.HLODOutputWriteFailed");
    }

    ENGINE_CORE_INFO("World: built HLOD asset '{}' with {} clusters",
                     result.OutputAssetPath.string(), result.Clusters.size());
    return Result<HLODBuildResult>::Success(result);
}

Result<WorldOriginRebaseResult> OpenWorldRuntime::RebaseWorldOrigin(const WorldOriginRebaseRequest& request) {
    if (request.Scene == nullptr) {
        return Result<WorldOriginRebaseResult>::Failure("World.SceneMissing");
    }

    WorldOriginRebaseResult result;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        result.PreviousOrigin = m_CurrentWorldOrigin;
        result.NewOrigin = request.NewOrigin;
    }

    const Math::Vec3 rebaseDelta = result.NewOrigin - result.PreviousOrigin;
    if (glm::length2(rebaseDelta) <= 0.0000001f) {
        result.AppliedOffset = Math::Vec3(0.0f, 0.0f, 0.0f);
        return Result<WorldOriginRebaseResult>::Success(result);
    }

    const Math::Vec3 entityOffset = -rebaseDelta;
    auto& registry = request.Scene->GetRegistry();
    auto transforms = registry.view<ECS::TransformComponent>();
    for (auto entity : transforms) {
        auto& transform = transforms.get<ECS::TransformComponent>(entity);
        transform.Position += entityOffset;
        transform.IsDirty = true;
        ++result.ShiftedEntityCount;
    }

    if (request.PhysicsWorld != nullptr) {
        auto rigidBodies = registry.view<ECS::RigidBodyComponent>();
        for (auto entity : rigidBodies) {
            auto& rigidBody = rigidBodies.get<ECS::RigidBodyComponent>(entity);
            if (!rigidBody.IsBodyCreated) {
                continue;
            }

            if (request.PhysicsWorld->ShiftBody(rigidBody.BodyID, entityOffset)) {
                ++result.ShiftedPhysicsBodyCount;
            }
        }
    }

    if (request.ReplicationSystem != nullptr) {
        request.ReplicationSystem->SetWorldOriginOffset(result.NewOrigin);
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_CurrentWorldOrigin = result.NewOrigin;
    }
    result.AppliedOffset = entityOffset;

    ENGINE_CORE_INFO("World: rebased origin to ({}, {}, {}), shifted {} entities and {} bodies",
                     result.NewOrigin.x, result.NewOrigin.y, result.NewOrigin.z,
                     result.ShiftedEntityCount, result.ShiftedPhysicsBodyCount);
    return Result<WorldOriginRebaseResult>::Success(result);
}

bool OpenWorldRuntime::IsCellResident(const std::string& cellId) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_ResidentCells.find(cellId) != m_ResidentCells.end();
}

uint32_t OpenWorldRuntime::GetResidentCellCount() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return static_cast<uint32_t>(m_ResidentCells.size());
}

uint64_t OpenWorldRuntime::GetResidentMemoryBytes() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_CurrentResidentMemoryBytes;
}

Math::Vec3 OpenWorldRuntime::GetCurrentWorldOrigin() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_CurrentWorldOrigin;
}

bool OpenWorldRuntime::EnsureBudgetForIncoming(uint64_t incomingBytes, const std::string& incomingCellId) {
    while ((!m_Budget.MaxResidentCells || m_ResidentCells.size() >= m_Budget.MaxResidentCells) ||
           (m_Budget.MaxResidentMemoryBytes > 0 &&
            m_CurrentResidentMemoryBytes + incomingBytes > m_Budget.MaxResidentMemoryBytes)) {
        std::string evictionCandidate;
        uint64_t oldestFrame = std::numeric_limits<uint64_t>::max();

        for (const auto& [cellId, state] : m_ResidentCells) {
            if (cellId == incomingCellId) {
                continue;
            }

            const auto dependentsIt = m_CellDependents.find(cellId);
            if (dependentsIt != m_CellDependents.end() && !dependentsIt->second.empty()) {
                continue;
            }

            if (state.LastTouchedFrame < oldestFrame ||
                (state.LastTouchedFrame == oldestFrame && cellId < evictionCandidate)) {
                oldestFrame = state.LastTouchedFrame;
                evictionCandidate = cellId;
            }
        }

        if (evictionCandidate.empty()) {
            return false;
        }

        EvictCellInternal(evictionCandidate);
    }

    return true;
}

void OpenWorldRuntime::EvictCellInternal(const std::string& cellId) {
    auto residentIt = m_ResidentCells.find(cellId);
    if (residentIt == m_ResidentCells.end()) {
        return;
    }

    const uint64_t residentBytes = residentIt->second.Descriptor.EstimatedMemoryBytes;
    m_CurrentResidentMemoryBytes = (residentBytes > m_CurrentResidentMemoryBytes)
        ? 0
        : (m_CurrentResidentMemoryBytes - residentBytes);

    for (const std::string& dependency : residentIt->second.Descriptor.Dependencies) {
        auto dependentsIt = m_CellDependents.find(dependency);
        if (dependentsIt != m_CellDependents.end()) {
            dependentsIt->second.erase(cellId);
            if (dependentsIt->second.empty()) {
                m_CellDependents.erase(dependentsIt);
            }
        }
    }

    m_ResidentCells.erase(residentIt);
    m_CellDependents.erase(cellId);
}

Result<PartitionCellStreamResult> StreamWorldPartitionCellIn(const PartitionCellStreamInRequest& request) {
    return OpenWorldRuntime::Get().StreamWorldPartitionCellIn(request);
}

Result<void> StreamWorldPartitionCellOut(const PartitionCellStreamOutRequest& request) {
    return OpenWorldRuntime::Get().StreamWorldPartitionCellOut(request);
}

Result<HLODBuildResult> BuildHierarchicalLOD(const HLODBuildRequest& request) {
    return OpenWorldRuntime::Get().BuildHierarchicalLOD(request);
}

Result<WorldOriginRebaseResult> RebaseWorldOrigin(const WorldOriginRebaseRequest& request) {
    return OpenWorldRuntime::Get().RebaseWorldOrigin(request);
}

} // namespace World
} // namespace Core
