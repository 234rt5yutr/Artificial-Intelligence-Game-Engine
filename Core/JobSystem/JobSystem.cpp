#include "JobSystem.h"
#include "Core/Profile.h"
#include <algorithm>

namespace Core {
namespace JobSystem {

    namespace {
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
        ctx.counter.fetch_add(1, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            jobQueue.push([&ctx, job]() {
                job();
                ctx.counter.fetch_sub(1, std::memory_order_release);
            });
        }
        wakeCondition.notify_one();
    }

    void Dispatch(Context& ctx, uint32_t jobCount, uint32_t groupSize, const std::function<void(uint32_t)>& job) {
        PROFILE_FUNCTION();
        if (jobCount == 0 || groupSize == 0) return;

        uint32_t groupCount = (jobCount + groupSize - 1) / groupSize;
        ctx.counter.fetch_add(groupCount, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
                jobQueue.push([groupIndex, groupSize, jobCount, &ctx, job]() {
                    uint32_t groupJobOffset = groupIndex * groupSize;
                    uint32_t groupJobEnd = std::min(groupJobOffset + groupSize, jobCount);

                    for (uint32_t i = groupJobOffset; i < groupJobEnd; ++i) {
                        job(i);
                    }

                    ctx.counter.fetch_sub(1, std::memory_order_release);
                });
            }
        }
        wakeCondition.notify_all();
    }

    bool IsBusy(const Context& ctx) {
        return ctx.counter.load(std::memory_order_acquire) > 0;
    }

    void Wait(const Context& ctx) {
        PROFILE_FUNCTION();
        while (IsBusy(ctx)) {
            // Optional: calling thread can help execute jobs instead of just yielding
            std::this_thread::yield();
        }
    }

} // namespace JobSystem
} // namespace Core
