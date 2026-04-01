#include "Core/Renderer/Particles/ParticlePool.h"
#include "Core/Log.h"
#include <cstring>

namespace Core {
namespace Renderer {
namespace Particles {

    //=========================================================================
    // Constructor / Destructor
    //=========================================================================

    ParticlePool::ParticlePool()
    {
    }

    ParticlePool::~ParticlePool()
    {
        Shutdown();
    }

    //=========================================================================
    // Initialization
    //=========================================================================

    bool ParticlePool::Initialize(std::shared_ptr<RHI::RHIDevice> device, const ParticlePoolConfig& config)
    {
        if (m_Initialized) {
            ENGINE_CORE_WARN("ParticlePool already initialized");
            return false;
        }

        if (!device) {
            ENGINE_CORE_ERROR("ParticlePool::Initialize: device is null");
            return false;
        }

        // Validate config
        if (config.MaxParticles < MIN_POOL_SIZE || config.MaxParticles > MAX_POOL_SIZE) {
            ENGINE_CORE_ERROR("ParticlePool::Initialize: MaxParticles {} out of range [{}, {}]",
                config.MaxParticles, MIN_POOL_SIZE, MAX_POOL_SIZE);
            return false;
        }

        m_Device = device;
        m_Config = config;

        CreateBuffers();

        // Initialize the dead list with all available indices
        InitializeDeadList();

        m_Initialized = true;
        m_Stats.AllocatedParticles = config.MaxParticles;
        m_Stats.FreeSlots = config.MaxParticles;
        m_Stats.TotalMemoryBytes = sizeof(Particle) * config.MaxParticles * 2 +   // Double-buffered particles
                                    sizeof(uint32_t) * config.MaxParticles * 4 +   // Alive lists (x2) + dead list + sorted indices
                                    sizeof(float) * config.MaxParticles +          // Sort keys
                                    sizeof(DrawIndirectCommand) +                  // Draw indirect
                                    sizeof(uint32_t) * 4 +                         // Counters
                                    sizeof(uint32_t) * 3;                          // Sort dispatch

        ENGINE_CORE_INFO("ParticlePool initialized: {} particles, {:.2f} MB",
            config.MaxParticles,
            static_cast<double>(m_Stats.TotalMemoryBytes) / (1024.0 * 1024.0));

        return true;
    }

    void ParticlePool::Shutdown()
    {
        if (!m_Initialized) {
            return;
        }

        DestroyBuffers();

        m_Device.reset();
        m_Stats.Reset();
        m_Initialized = false;

        ENGINE_CORE_INFO("ParticlePool shutdown");
    }

    void ParticlePool::CreateBuffers()
    {
        if (!m_Device) {
            ENGINE_CORE_ERROR("ParticlePool::CreateBuffers: no device");
            return;
        }

        const uint32_t maxParticles = m_Config.MaxParticles;

        // Particle data buffers (double-buffered)
        RHI::BufferDescriptor particleDesc;
        particleDesc.size = sizeof(Particle) * maxParticles;
        particleDesc.usage = RHI::BufferUsage::Storage;
        particleDesc.mapped = false;

        m_ParticleBuffer[0] = m_Device->CreateBuffer(particleDesc);
        m_ParticleBuffer[1] = m_Device->CreateBuffer(particleDesc);

        // Alive list buffers (double-buffered, stores indices of active particles)
        RHI::BufferDescriptor aliveListDesc;
        aliveListDesc.size = sizeof(uint32_t) * maxParticles;
        aliveListDesc.usage = RHI::BufferUsage::Storage;
        aliveListDesc.mapped = false;

        m_AliveListBuffer[0] = m_Device->CreateBuffer(aliveListDesc);
        m_AliveListBuffer[1] = m_Device->CreateBuffer(aliveListDesc);

        // Dead list buffer (indices of free slots)
        RHI::BufferDescriptor deadListDesc;
        deadListDesc.size = sizeof(uint32_t) * maxParticles;
        deadListDesc.usage = RHI::BufferUsage::Storage;
        deadListDesc.mapped = true;  // Need initial upload
        m_DeadListBuffer = m_Device->CreateBuffer(deadListDesc);

        // Counter buffer: [aliveCount, deadCount, emitCount, sortCount]
        RHI::BufferDescriptor counterDesc;
        counterDesc.size = sizeof(uint32_t) * 4;
        counterDesc.usage = RHI::BufferUsage::Storage;
        counterDesc.mapped = true;  // Need CPU access for reset/readback
        m_CounterBuffer = m_Device->CreateBuffer(counterDesc);

        // Indirect draw command buffer
        RHI::BufferDescriptor drawIndirectDesc;
        drawIndirectDesc.size = sizeof(DrawIndirectCommand);
        drawIndirectDesc.usage = RHI::BufferUsage::Storage;
        drawIndirectDesc.mapped = true;
        m_DrawIndirectBuffer = m_Device->CreateBuffer(drawIndirectDesc);

        // Sort dispatch indirect buffer (3 uints for dispatch dimensions)
        RHI::BufferDescriptor sortDispatchDesc;
        sortDispatchDesc.size = sizeof(uint32_t) * 3;
        sortDispatchDesc.usage = RHI::BufferUsage::Storage;
        sortDispatchDesc.mapped = false;
        m_SortDispatchBuffer = m_Device->CreateBuffer(sortDispatchDesc);

        // Sorted index buffer
        RHI::BufferDescriptor sortedIndexDesc;
        sortedIndexDesc.size = sizeof(uint32_t) * maxParticles;
        sortedIndexDesc.usage = RHI::BufferUsage::Storage;
        sortedIndexDesc.mapped = false;
        m_SortedIndexBuffer = m_Device->CreateBuffer(sortedIndexDesc);

        // Sort key buffer (depth values)
        RHI::BufferDescriptor sortKeyDesc;
        sortKeyDesc.size = sizeof(float) * maxParticles;
        sortKeyDesc.usage = RHI::BufferUsage::Storage;
        sortKeyDesc.mapped = false;
        m_SortKeyBuffer = m_Device->CreateBuffer(sortKeyDesc);

        // Staging buffer for readback
        RHI::BufferDescriptor stagingDesc;
        stagingDesc.size = sizeof(uint32_t) * 4;
        stagingDesc.usage = RHI::BufferUsage::Staging;
        stagingDesc.mapped = true;
        m_StagingBuffer = m_Device->CreateBuffer(stagingDesc);

        ENGINE_CORE_TRACE("ParticlePool buffers created");
    }

    void ParticlePool::DestroyBuffers()
    {
        m_ParticleBuffer[0].reset();
        m_ParticleBuffer[1].reset();
        m_AliveListBuffer[0].reset();
        m_AliveListBuffer[1].reset();
        m_DeadListBuffer.reset();
        m_CounterBuffer.reset();
        m_DrawIndirectBuffer.reset();
        m_SortDispatchBuffer.reset();
        m_SortedIndexBuffer.reset();
        m_SortKeyBuffer.reset();
        m_StagingBuffer.reset();
    }

    //=========================================================================
    // Configuration
    //=========================================================================

    bool ParticlePool::Resize(uint32_t newMaxParticles)
    {
        if (!m_Initialized) {
            ENGINE_CORE_WARN("ParticlePool::Resize: not initialized");
            return false;
        }

        if (newMaxParticles < MIN_POOL_SIZE || newMaxParticles > MAX_POOL_SIZE) {
            ENGINE_CORE_ERROR("ParticlePool::Resize: {} out of range [{}, {}]",
                newMaxParticles, MIN_POOL_SIZE, MAX_POOL_SIZE);
            return false;
        }

        if (newMaxParticles == m_Config.MaxParticles) {
            return true;  // No change needed
        }

        // Must wait for GPU to finish before resizing
        m_Device->WaitIdle();

        // Destroy old buffers
        DestroyBuffers();

        // Update config and recreate
        m_Config.MaxParticles = newMaxParticles;
        CreateBuffers();
        InitializeDeadList();

        m_Stats.AllocatedParticles = newMaxParticles;
        m_Stats.FreeSlots = newMaxParticles;
        m_Stats.ActiveParticles = 0;

        ENGINE_CORE_INFO("ParticlePool resized to {} particles", newMaxParticles);
        return true;
    }

    //=========================================================================
    // Buffer Access
    //=========================================================================

    std::shared_ptr<RHI::RHIBuffer> ParticlePool::GetParticleBuffer() const
    {
        return m_ParticleBuffer[m_CurrentBuffer];
    }

    std::shared_ptr<RHI::RHIBuffer> ParticlePool::GetParticleBufferPrev() const
    {
        return m_ParticleBuffer[1 - m_CurrentBuffer];
    }

    //=========================================================================
    // Frame Management
    //=========================================================================

    void ParticlePool::SwapBuffers()
    {
        if (m_Config.EnableDoubleBuffering) {
            m_CurrentBuffer = 1 - m_CurrentBuffer;
        }
    }

    void ParticlePool::ResetCounters(std::shared_ptr<RHI::RHICommandList> /*commandList*/)
    {
        if (!m_CounterBuffer) {
            return;
        }

        // Reset alive count to 0, keep dead count, reset emit count to 0
        void* data = nullptr;
        m_CounterBuffer->Map(&data);
        if (data) {
            uint32_t* counters = static_cast<uint32_t*>(data);
            // counters[0] = aliveCount (will be written by update shader)
            // counters[1] = deadCount (preserved from previous frame)
            // counters[2] = emitCount (reset to 0)
            // counters[3] = sortCount (reset to 0)
            counters[2] = 0;
            counters[3] = 0;
            m_CounterBuffer->Unmap();
        }
    }

    void ParticlePool::InitializeDeadList()
    {
        if (!m_DeadListBuffer || !m_CounterBuffer) {
            return;
        }

        const uint32_t maxParticles = m_Config.MaxParticles;

        // Fill dead list with all indices (all particles start as dead/free)
        void* deadListData = nullptr;
        m_DeadListBuffer->Map(&deadListData);
        if (deadListData) {
            uint32_t* indices = static_cast<uint32_t*>(deadListData);
            for (uint32_t i = 0; i < maxParticles; ++i) {
                indices[i] = i;
            }
            m_DeadListBuffer->Unmap();
        }

        // Initialize counters: 0 alive, all dead
        void* counterData = nullptr;
        m_CounterBuffer->Map(&counterData);
        if (counterData) {
            uint32_t* counters = static_cast<uint32_t*>(counterData);
            counters[0] = 0;              // aliveCount
            counters[1] = maxParticles;   // deadCount
            counters[2] = 0;              // emitCount
            counters[3] = 0;              // sortCount
            m_CounterBuffer->Unmap();
        }

        // Initialize draw indirect command with 0 instances
        void* drawData = nullptr;
        m_DrawIndirectBuffer->Map(&drawData);
        if (drawData) {
            DrawIndirectCommand* cmd = static_cast<DrawIndirectCommand*>(drawData);
            cmd->VertexCount = 4;         // Quad: 4 vertices
            cmd->InstanceCount = 0;       // No particles yet
            cmd->FirstVertex = 0;
            cmd->FirstInstance = 0;
            m_DrawIndirectBuffer->Unmap();
        }

        ENGINE_CORE_TRACE("ParticlePool dead list initialized with {} slots", maxParticles);
    }

    //=========================================================================
    // Statistics
    //=========================================================================

    uint32_t ParticlePool::UpdateStats()
    {
        if (!m_CounterBuffer) {
            return 0;
        }

        // Read counters
        void* data = nullptr;
        m_CounterBuffer->Map(&data);
        if (data) {
            uint32_t* counters = static_cast<uint32_t*>(data);
            m_Stats.ActiveParticles = counters[0];
            m_Stats.FreeSlots = counters[1];
            m_CounterBuffer->Unmap();
        }

        return m_Stats.ActiveParticles;
    }

} // namespace Particles
} // namespace Renderer
} // namespace Core
