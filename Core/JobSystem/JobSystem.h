#pragma once

#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

namespace Core {
namespace JobSystem {

    // A job context allows waiting for a set of jobs to finish
    struct Context {
        std::atomic<uint32_t> counter{0};
    };

    // Initialize the Job System with the maximum number of logical cores
    void Initialize();

    // Shut down the Job System and wait for all threads to finish
    void Shutdown();

    // Execute a single job asynchronously
    void Execute(Context& ctx, const std::function<void()>& job);

    // Dispatch a job across multiple threads 
    void Dispatch(Context& ctx, uint32_t jobCount, uint32_t groupSize, const std::function<void(uint32_t)>& job);

    // Wait for the context to reach a counter of 0
    void Wait(const Context& ctx);
    
    // Check if the context implies jobs are busy
    bool IsBusy(const Context& ctx);

} // namespace JobSystem
} // namespace Core
