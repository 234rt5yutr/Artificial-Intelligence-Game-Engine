#pragma once

#include "Core/Math/Math.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Core {
namespace ECS {
    class Scene;
}

namespace Physics {
    class PhysicsWorld;
}

namespace Network {
    class ServerReplicationSystem;
}

namespace World {

    template <typename T>
    struct Result {
        bool Ok = false;
        T Value{};
        std::string Error;

        static Result Success(T value) {
            Result result;
            result.Ok = true;
            result.Value = std::move(value);
            return result;
        }

        static Result Failure(std::string error) {
            Result result;
            result.Ok = false;
            result.Error = std::move(error);
            return result;
        }
    };

    template <>
    struct Result<void> {
        bool Ok = false;
        std::string Error;

        static Result Success() {
            Result result;
            result.Ok = true;
            return result;
        }

        static Result Failure(std::string error) {
            Result result;
            result.Ok = false;
            result.Error = std::move(error);
            return result;
        }
    };

    struct PartitionCellCoordinate {
        int32_t X = 0;
        int32_t Z = 0;
    };

    struct PartitionCellDescriptor {
        std::string CellId;
        PartitionCellCoordinate Coordinate{};
        std::filesystem::path CellAssetPath;
        std::vector<std::string> Dependencies;
        uint64_t EstimatedMemoryBytes = 0;
    };

    struct StreamingBudgetConfig {
        uint32_t MaxResidentCells = 128;
        uint64_t MaxResidentMemoryBytes = 1024ull * 1024ull * 1024ull;
    };

    struct PartitionCellStreamInRequest {
        PartitionCellDescriptor Cell;
        ECS::Scene* TargetScene = nullptr;
        bool ActivateImmediately = true;
    };

    struct PartitionCellStreamOutRequest {
        std::string CellId;
        bool ForceEvict = false;
    };

    struct PartitionCellStreamResult {
        std::string CellId;
        uint32_t HydratedDependencyCount = 0;
        uint32_t ResidentCellCount = 0;
        uint64_t ResidentMemoryBytes = 0;
    };

    struct HLODBuildRequest {
        std::string LayerId;
        std::vector<std::string> SourceCellIds;
        std::filesystem::path OutputAssetPath;
        uint32_t ClusterSize = 4;
    };

    struct HLODClusterInfo {
        std::string ClusterId;
        std::vector<std::string> SourceCells;
    };

    struct HLODBuildResult {
        std::filesystem::path OutputAssetPath;
        std::vector<HLODClusterInfo> Clusters;
    };

    struct WorldOriginRebaseRequest {
        ECS::Scene* Scene = nullptr;
        Math::Vec3 NewOrigin{0.0f, 0.0f, 0.0f};
        Physics::PhysicsWorld* PhysicsWorld = nullptr;
        Network::ServerReplicationSystem* ReplicationSystem = nullptr;
    };

    struct WorldOriginRebaseResult {
        Math::Vec3 PreviousOrigin{0.0f, 0.0f, 0.0f};
        Math::Vec3 NewOrigin{0.0f, 0.0f, 0.0f};
        Math::Vec3 AppliedOffset{0.0f, 0.0f, 0.0f};
        uint32_t ShiftedEntityCount = 0;
        uint32_t ShiftedPhysicsBodyCount = 0;
    };

    class OpenWorldRuntime {
    public:
        static OpenWorldRuntime& Get();

        void SetStreamingBudget(const StreamingBudgetConfig& config);
        StreamingBudgetConfig GetStreamingBudget() const;

        Result<PartitionCellStreamResult> StreamWorldPartitionCellIn(const PartitionCellStreamInRequest& request);
        Result<void> StreamWorldPartitionCellOut(const PartitionCellStreamOutRequest& request);
        Result<HLODBuildResult> BuildHierarchicalLOD(const HLODBuildRequest& request);
        Result<WorldOriginRebaseResult> RebaseWorldOrigin(const WorldOriginRebaseRequest& request);

        bool IsCellResident(const std::string& cellId) const;
        uint32_t GetResidentCellCount() const;
        uint64_t GetResidentMemoryBytes() const;
        Math::Vec3 GetCurrentWorldOrigin() const;

    private:
        OpenWorldRuntime() = default;

        struct ResidentCellState {
            PartitionCellDescriptor Descriptor;
            uint64_t LastTouchedFrame = 0;
            bool IsPlaceholder = false;
        };

        bool EnsureBudgetForIncoming(uint64_t incomingBytes, const std::string& incomingCellId);
        void EvictCellInternal(const std::string& cellId);

    private:
        mutable std::mutex m_Mutex;
        StreamingBudgetConfig m_Budget{};
        std::unordered_map<std::string, ResidentCellState> m_ResidentCells;
        std::unordered_map<std::string, std::unordered_set<std::string>> m_CellDependents;
        uint64_t m_CurrentResidentMemoryBytes = 0;
        uint64_t m_FrameCounter = 0;
        Math::Vec3 m_CurrentWorldOrigin{0.0f, 0.0f, 0.0f};
    };

    Result<PartitionCellStreamResult> StreamWorldPartitionCellIn(const PartitionCellStreamInRequest& request);
    Result<void> StreamWorldPartitionCellOut(const PartitionCellStreamOutRequest& request);
    Result<HLODBuildResult> BuildHierarchicalLOD(const HLODBuildRequest& request);
    Result<WorldOriginRebaseResult> RebaseWorldOrigin(const WorldOriginRebaseRequest& request);

} // namespace World
} // namespace Core
