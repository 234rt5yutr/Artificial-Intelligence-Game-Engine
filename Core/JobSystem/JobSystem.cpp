#include "JobSystem.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <algorithm>

namespace Core {
namespace JobSystem {

    namespace {
        constexpr size_t MAX_JOB_QUEUE_SIZE = 65536;
        
        uint32_t numThreads = 0;
        std::vector<std::thread> threadPool;
        std::queue<std::function<void()>> jobQueue;
        std::mutex queueMutex;
        std::condition_variable wakeCondition;
        std::atomic<bool> isRunning{false};

        void WorkerThread() {
            PROFILE_THREAD("Job Worker");
            while (isRunning.load()) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    wakeCondition.wait(lock, [] {
                        return !jobQueue.empty() || !isRunning.load();
                    });

                    if (!isRunning.load() && jobQueue.empty()) {
                        break;
                    }

                    if (!jobQueue.empty()) {
                        job = std::move(jobQueue.front());
                        jobQueue.pop();
                    }
                }

                if (job) {
                    PROFILE_SCOPE("Execute Job");
                    job();
                }
            }
        }
    }

    void Initialize() {
        PROFILE_FUNCTION();
        if (isRunning.load()) return;

        numThreads = std::max(1u, std::thread::hardware_concurrency() - 1u);
        isRunning.store(true);

        for (uint32_t i = 0; i < numThreads; ++i) {
            threadPool.emplace_back(std::thread(&WorkerThread));
        }
    }

    void Shutdown() {
        PROFILE_FUNCTION();
        if (!isRunning.load()) return;

        isRunning.store(false);
        wakeCondition.notify_all();

        for (std::thread& thread : threadPool) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threadPool.clear();
    }

    void Execute(Context& ctx, const std::function<void()>& job) {
        PROFILE_FUNCTION();
        if (!ctx.counter) {
            ctx.counter = std::make_shared<std::atomic<uint32_t>>(0u);
        }
        auto counter = ctx.counter;
        counter->fetch_add(1, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            
            // Prevent unbounded queue growth
            if (jobQueue.size() >= MAX_JOB_QUEUE_SIZE) {
                ENGINE_CORE_ERROR("Job queue full, dropping job");
                counter->fetch_sub(1, std::memory_order_release);
                return;
            }
            
            jobQueue.push([counter, job]() {
                job();
                counter->fetch_sub(1, std::memory_order_release);
            });
        }
        wakeCondition.notify_one();
    }

    void Dispatch(Context& ctx, uint32_t jobCount, uint32_t groupSize, const std::function<void(uint32_t)>& job) {
        PROFILE_FUNCTION();
        if (jobCount == 0 || groupSize == 0) return;
        if (!ctx.counter) {
            ctx.counter = std::make_shared<std::atomic<uint32_t>>(0u);
        }

        uint32_t groupCount = (jobCount + groupSize - 1) / groupSize;
        auto counter = ctx.counter;
        counter->fetch_add(groupCount, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            
            // Check queue capacity before adding all groups
            if (jobQueue.size() + groupCount > MAX_JOB_QUEUE_SIZE) {
                ENGINE_CORE_ERROR("Job queue full, dropping dispatch of {} groups", groupCount);
                counter->fetch_sub(groupCount, std::memory_order_release);
                return;
            }
            
            for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
                jobQueue.push([groupIndex, groupSize, jobCount, counter, job]() {
                    uint32_t groupJobOffset = groupIndex * groupSize;
                    uint32_t groupJobEnd = std::min(groupJobOffset + groupSize, jobCount);

                    for (uint32_t i = groupJobOffset; i < groupJobEnd; ++i) {
                        job(i);
                    }

                    counter->fetch_sub(1, std::memory_order_release);
                });
            }
        }
        wakeCondition.notify_all();
    }

    bool IsBusy(const Context& ctx) {
        return ctx.counter && ctx.counter->load(std::memory_order_acquire) > 0;
    }

    void Wait(const Context& ctx) {
        PROFILE_FUNCTION();
        // Use exponential backoff instead of busy spinning
        uint32_t spinCount = 0;
        constexpr uint32_t MAX_SPIN_COUNT = 1000;
        
        while (IsBusy(ctx)) {
            if (spinCount < MAX_SPIN_COUNT) {
                // First spin a bit
                ++spinCount;
            } else {
                // Then yield to other threads
                std::this_thread::yield();
            }
        }
    }

} // namespace JobSystem
} // namespace Core
