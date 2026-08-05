#include "RenderThread.h"

#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/UI/UIManager.h"

#include <chrono>

namespace Core {
namespace Renderer {

    RenderThread::~RenderThread() {
        Stop();
    }

    bool RenderThread::Start(RHI::VulkanContext* context) {
        if (!context) {
            return false;
        }
        if (IsRunning()) {
            return true;
        }

        m_Context = context;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_ShuttingDown = false;
            m_HasWork = false;
        }
        m_Running.store(true, std::memory_order_release);
        m_Thread = std::thread(&RenderThread::ThreadMain, this);
        ENGINE_CORE_INFO("Render thread started; simulation and submission now overlap");
        return true;
    }

    void RenderThread::Stop() {
        if (!m_Thread.joinable()) {
            m_Running.store(false, std::memory_order_release);
            return;
        }

        // Drain first: tearing down while a frame is still recording would
        // destroy Vulkan objects the render thread is mid-way through using.
        WaitForFrame();
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_ShuttingDown = true;
        }
        m_WorkAvailable.notify_all();
        m_Thread.join();
        m_Running.store(false, std::memory_order_release);
        m_Context = nullptr;
        ENGINE_CORE_INFO("Render thread stopped");
    }

    void RenderThread::WaitForFrame() {
        if (!m_Thread.joinable()) {
            return;
        }
        const auto waitStart = std::chrono::high_resolution_clock::now();
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_WorkComplete.wait(lock, [this] { return !m_HasWork; });
        }
        const auto waitEnd = std::chrono::high_resolution_clock::now();
        m_LastWaitMs.store(std::chrono::duration<float, std::milli>(waitEnd - waitStart).count(),
                           std::memory_order_relaxed);
    }

    void RenderThread::SubmitFrame(FrameRenderData&& frame) {
        if (!m_Thread.joinable()) {
            // No render thread: render inline so callers need no second path.
            RenderOneFrame(frame);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_PendingFrame = std::move(frame);
            m_HasWork = true;
        }
        m_FramesSubmitted.fetch_add(1, std::memory_order_relaxed);
        m_WorkAvailable.notify_one();
    }

    void RenderThread::ThreadMain() {
        for (;;) {
            FrameRenderData frame;
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_WorkAvailable.wait(lock, [this] { return m_HasWork || m_ShuttingDown; });
                if (m_ShuttingDown && !m_HasWork) {
                    return;
                }
                frame = std::move(m_PendingFrame);
            }

            RenderOneFrame(frame);

            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_HasWork = false;
            }
            m_WorkComplete.notify_all();
        }
    }

    void RenderThread::RenderOneFrame(FrameRenderData& frame) {
        PROFILE_SCOPE("RenderThread Frame");
        if (!m_Context) {
            return;
        }

        const auto start = std::chrono::high_resolution_clock::now();

        m_Context->SubmitFrameRenderData(frame);
        // DrawFrame records the UI as well; UIManager::Update already built the
        // panels on the simulation thread, so nothing here reads scene state.
        m_Context->DrawFrame();
        m_Context->ClearSceneDrawData();
        UI::UIManager::Get().EndFrame();

        const auto end = std::chrono::high_resolution_clock::now();
        m_LastRenderMs.store(std::chrono::duration<float, std::milli>(end - start).count(),
                             std::memory_order_relaxed);
        m_FramesRendered.fetch_add(1, std::memory_order_relaxed);
    }

    RenderThreadStats RenderThread::GetStats() const {
        RenderThreadStats stats;
        stats.FramesSubmitted = m_FramesSubmitted.load(std::memory_order_relaxed);
        stats.FramesRendered = m_FramesRendered.load(std::memory_order_relaxed);
        stats.LastRenderMs = m_LastRenderMs.load(std::memory_order_relaxed);
        stats.LastWaitMs = m_LastWaitMs.load(std::memory_order_relaxed);
        stats.Running = m_Running.load(std::memory_order_acquire);
        return stats;
    }

} // namespace Renderer
} // namespace Core
