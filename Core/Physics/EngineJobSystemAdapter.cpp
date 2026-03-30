#include "Core/Physics/EngineJobSystemAdapter.h"
#include "Core/Log.h"
#include <thread>
#include <algorithm>

namespace Core {
namespace Physics {

    // BarrierImpl
    EngineJobSystemAdapter::BarrierImpl::BarrierImpl()
    {
    }

    EngineJobSystemAdapter::BarrierImpl::~BarrierImpl()
    {
        JPH_ASSERT(IsEmpty());
    }

    void EngineJobSystemAdapter::BarrierImpl::AddJob(const JobHandle& inJob)
    {
        // Increment job count and set barrier on job
        m_NumJobs.fetch_add(1, std::memory_order_relaxed);
        
        Job* job = inJob.GetPtr();
        if (job) {
            job->SetBarrier(this);
        }
    }

    void EngineJobSystemAdapter::BarrierImpl::AddJobs(const JobHandle* inHandles, uint32_t inNumHandles)
    {
        m_NumJobs.fetch_add(inNumHandles, std::memory_order_relaxed);
        
        for (uint32_t i = 0; i < inNumHandles; ++i) {
            Job* job = inHandles[i].GetPtr();
            if (job) {
                job->SetBarrier(this);
            }
        }
    }

    void EngineJobSystemAdapter::BarrierImpl::OnJobFinished(Job* /*inJob*/)
    {
        uint32_t prevCount = m_NumJobs.fetch_sub(1, std::memory_order_release);
        if (prevCount == 1) {
            // Last job finished, wake up waiting threads
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_ConditionVariable.notify_all();
        }
    }

    void EngineJobSystemAdapter::BarrierImpl::Wait()
    {
        PROFILE_FUNCTION();
        
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_ConditionVariable.wait(lock, [this]() {
            return m_NumJobs.load(std::memory_order_acquire) == 0;
        });
    }

    // EngineJobSystemAdapter
    EngineJobSystemAdapter::EngineJobSystemAdapter()
    {
    }

    EngineJobSystemAdapter::~EngineJobSystemAdapter()
    {
        // Wait for all pending jobs
        Core::JobSystem::Wait(m_JobContext);
        
        // Clean up barrier pool
        std::lock_guard<std::mutex> lock(m_BarrierMutex);
        for (auto* barrier : m_BarrierPool) {
            delete barrier;
        }
        m_BarrierPool.clear();
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
        return static_cast<int>(m_MaxConcurrency);
    }

    JPH::JobSystem::JobHandle EngineJobSystemAdapter::CreateJob(
        const char* inName,
        JPH::ColorArg /*inColor*/,
        const JobFunction& inJobFunction,
        uint32_t inNumDependencies)
    {
        // Create job with reference count: 1 for the handle + dependencies
        Job* job = new Job(inName, JPH::Color::sWhite, this, inJobFunction, inNumDependencies);
        
        // If no dependencies, queue immediately
        if (inNumDependencies == 0) {
            QueueJob(job);
        }
        
        return JobHandle(job);
    }

    JPH::JobSystem::Barrier* EngineJobSystemAdapter::CreateBarrier()
    {
        std::lock_guard<std::mutex> lock(m_BarrierMutex);
        
        // Reuse from pool if available
        if (!m_BarrierPool.empty()) {
            BarrierImpl* barrier = m_BarrierPool.back();
            m_BarrierPool.pop_back();
            return barrier;
        }
        
        return new BarrierImpl();
    }

    void EngineJobSystemAdapter::DestroyBarrier(Barrier* inBarrier)
    {
        auto* barrier = static_cast<BarrierImpl*>(inBarrier);
        
        std::lock_guard<std::mutex> lock(m_BarrierMutex);
        
        // Return to pool for reuse
        m_BarrierPool.push_back(barrier);
    }

    void EngineJobSystemAdapter::WaitForJobs(Barrier* inBarrier)
    {
        auto* barrier = static_cast<BarrierImpl*>(inBarrier);
        barrier->Wait();
    }

    void EngineJobSystemAdapter::QueueJob(Job* inJob)
    {
        PROFILE_FUNCTION();
        
        // Execute job through engine's job system
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
