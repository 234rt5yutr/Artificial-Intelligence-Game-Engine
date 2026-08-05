#pragma once

// Simulation / render thread split.
//
// Everything used to run on one thread: simulate, build UI, submit, present,
// repeat. That caps the engine at `simulate + submit` per frame no matter how
// much of either is idle waiting on the other.
//
// The split here is deliberately the small one that pays: the simulation for
// frame N+1 runs while the render thread submits frame N. The synchronisation
// point sits immediately before ImGui's NewFrame, which is what makes it safe
// without deep-copying ImGui's draw lists:
//
//   [main]   Simulate(N+1) .......... WaitForFrame() -> BuildUI(N+1) -> Submit(N+1)
//   [render]        submit(N) ......^
//
// ponytail: UI construction does not overlap. It is a few hundred microseconds
// against a multi-millisecond simulation; give it its own double-buffered draw
// data only if a capture says otherwise.

#include "Core/Renderer/SceneRenderer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace Core {
namespace RHI { class VulkanContext; }

namespace Renderer {

    struct RenderThreadStats {
        uint64_t FramesSubmitted = 0;
        uint64_t FramesRendered = 0;
        float LastRenderMs = 0.0f;
        float LastWaitMs = 0.0f;
        bool Running = false;
    };

    class RenderThread {
    public:
        RenderThread() = default;
        ~RenderThread();

        RenderThread(const RenderThread&) = delete;
        RenderThread& operator=(const RenderThread&) = delete;

        bool Start(RHI::VulkanContext* context);
        void Stop();
        bool IsRunning() const { return m_Running.load(std::memory_order_acquire); }

        // Blocks until the render thread has finished whatever was last handed
        // to it. Safe to call when nothing is in flight, and when the thread was
        // never started.
        void WaitForFrame();

        // Hands the frame over and returns immediately. Undefined unless
        // WaitForFrame() was called since the previous submit.
        void SubmitFrame(FrameRenderData&& frame);

        RenderThreadStats GetStats() const;

    private:
        void ThreadMain();
        void RenderOneFrame(FrameRenderData& frame);

        RHI::VulkanContext* m_Context = nullptr;
        std::thread m_Thread;

        mutable std::mutex m_Mutex;
        std::condition_variable m_WorkAvailable;
        std::condition_variable m_WorkComplete;

        FrameRenderData m_PendingFrame;
        bool m_HasWork = false;
        bool m_ShuttingDown = false;

        std::atomic<bool> m_Running{false};
        std::atomic<uint64_t> m_FramesSubmitted{0};
        std::atomic<uint64_t> m_FramesRendered{0};
        std::atomic<float> m_LastRenderMs{0.0f};
        std::atomic<float> m_LastWaitMs{0.0f};
    };

} // namespace Renderer
} // namespace Core
