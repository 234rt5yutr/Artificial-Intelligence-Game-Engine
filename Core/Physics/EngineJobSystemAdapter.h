#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystem.h>
#include "Core/JobSystem/JobSystem.h"
#include "Core/Profile.h"
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace Core {
namespace Physics {

    // Custom Jolt JobSystem that bridges to the engine's multi-threaded JobSystem
    class EngineJobSystemAdapter : public JPH::JobSystem {
    public:
        // Barrier implementation for synchronizing job completion
        class BarrierImpl : public JPH::JobSystem::Barrier {
        public:
            BarrierImpl();
            virtual ~BarrierImpl() override;

            void AddJob(const JobHandle& inJob) override;
            void AddJobs(const JobHandle* inHandles, uint32_t inNumHandles) override;

            void Wait();
            bool IsEmpty() const { return m_NumJobs.load(std::memory_order_acquire) == 0; }

        protected:
            void OnJobFinished(Job* inJob) override;

        private:
            std::atomic<uint32_t> m_NumJobs{0};
            std::mutex m_Mutex;
            std::condition_variable m_ConditionVariable;
        };

        EngineJobSystemAdapter();
        virtual ~EngineJobSystemAdapter() override;

        // Initialize with max concurrency (0 = auto-detect)
        void Initialize(uint32_t inMaxConcurrency = 0);

        // JPH::JobSystem interface
        int GetMaxConcurrency() const override;
        JobHandle CreateJob(const char* inName, JPH::ColorArg inColor, 
                           const JobFunction& inJobFunction, uint32_t inNumDependencies = 0) override;
        Barrier* CreateBarrier() override;
        void DestroyBarrier(Barrier* inBarrier) override;
        void WaitForJobs(Barrier* inBarrier) override;

    protected:
        void QueueJob(Job* inJob) override;
        void QueueJobs(Job** inJobs, uint32_t inNumJobs) override;
        void FreeJob(Job* inJob) override;

    private:
        uint32_t m_MaxConcurrency = 0;
        
        // Pool of barriers for reuse
        std::mutex m_BarrierMutex;
        std::vector<BarrierImpl*> m_BarrierPool;
        
        // Job execution using engine's job system
        Core::JobSystem::Context m_JobContext;
    };

} // namespace Physics
} // namespace Core
