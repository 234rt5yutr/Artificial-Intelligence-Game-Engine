#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include "Core/JobSystem/JobSystem.h"
#include "Core/Profile.h"
#include <atomic>

namespace Core {
namespace Physics {

    // Custom Jolt JobSystem that bridges to the engine's multi-threaded JobSystem.
    //
    // Barriers come from JPH::JobSystemWithBarrier rather than being hand-rolled:
    // Jolt's version handles the job-already-finished case in Barrier::AddJob and
    // runs pending barrier jobs on the waiting thread, which a naive counter cannot.
    class EngineJobSystemAdapter : public JPH::JobSystemWithBarrier {
    public:
        EngineJobSystemAdapter();
        virtual ~EngineJobSystemAdapter() override;

        // Initialize with max concurrency (0 = auto-detect)
        void Initialize(uint32_t inMaxConcurrency = 0);

        // JPH::JobSystem interface
        int GetMaxConcurrency() const override;
        JobHandle CreateJob(const char* inName, JPH::ColorArg inColor,
                           const JobFunction& inJobFunction, uint32_t inNumDependencies = 0) override;

    protected:
        void QueueJob(Job* inJob) override;
        void QueueJobs(Job** inJobs, uint32_t inNumJobs) override;
        void FreeJob(Job* inJob) override;

    private:
        // Jolt allocates barriers up front; one per concurrent PhysicsSystem::Update
        // plus headroom is plenty for a single physics world.
        static constexpr uint32_t MAX_BARRIERS = 8;

        uint32_t m_MaxConcurrency = 0;

        // Job execution using engine's job system
        Core::JobSystem::Context m_JobContext;
    };

} // namespace Physics
} // namespace Core
