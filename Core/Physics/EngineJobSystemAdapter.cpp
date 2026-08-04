#include "Core/Physics/EngineJobSystemAdapter.h"
#include "Core/Log.h"
#include <thread>
#include <algorithm>

namespace Core {
namespace Physics {

    EngineJobSystemAdapter::EngineJobSystemAdapter()
        : JPH::JobSystemWithBarrier(MAX_BARRIERS)
    {
    }

    EngineJobSystemAdapter::~EngineJobSystemAdapter()
    {
        // Queued jobs hold a raw `this` via QueueJob's lambda, so they must all be
        // done before the base class tears the barriers down.
        Core::JobSystem::Wait(m_JobContext);
    }

    void EngineJobSystemAdapter::Initialize(uint32_t inMaxConcurrency)
    {
        if (inMaxConcurrency == 0) {
            m_MaxConcurrency = std::max(1u, std::thread::hardware_concurrency());
        } else {
            m_MaxConcurrency = inMaxConcurrency;
        }

        ENGINE_CORE_INFO("EngineJobSystemAdapter initialized with {} max concurrency", m_MaxConcurrency);
    }

    int EngineJobSystemAdapter::GetMaxConcurrency() const
    {
        return static_cast<int>(m_MaxConcurrency == 0 ? 1u : m_MaxConcurrency);
    }

    JPH::JobSystem::JobHandle EngineJobSystemAdapter::CreateJob(
        const char* inName,
        JPH::ColorArg inColor,
        const JobFunction& inJobFunction,
        uint32_t inNumDependencies)
    {
        Job* job = new Job(inName, inColor, this, inJobFunction, inNumDependencies);

        // Take the handle's reference before queueing. Queueing first would let a
        // worker run and release the job while its reference count is still zero,
        // which underflows the count and leaks the job instead of freeing it.
        JobHandle handle(job);

        if (inNumDependencies == 0) {
            QueueJob(job);
        }

        return handle;
    }

    void EngineJobSystemAdapter::QueueJob(Job* inJob)
    {
        PROFILE_FUNCTION();

        // The queue owns a reference while the job is pending; the matching Release()
        // below is what eventually routes into FreeJob.
        inJob->AddRef();

        Core::JobSystem::Execute(m_JobContext, [inJob]() {
            inJob->Execute();
            inJob->Release();
        });
    }

    void EngineJobSystemAdapter::QueueJobs(Job** inJobs, uint32_t inNumJobs)
    {
        PROFILE_FUNCTION();

        for (uint32_t i = 0; i < inNumJobs; ++i) {
            QueueJob(inJobs[i]);
        }
    }

    void EngineJobSystemAdapter::FreeJob(Job* inJob)
    {
        delete inJob;
    }

} // namespace Physics
} // namespace Core
