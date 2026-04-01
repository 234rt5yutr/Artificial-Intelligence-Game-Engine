#include "Core/Renderer/Particles/ParticleSystem.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace Core {
namespace Renderer {
namespace Particles {

    //=========================================================================
    // Constructor / Destructor
    //=========================================================================

    ParticleSystem::ParticleSystem()
    {
    }

    ParticleSystem::~ParticleSystem()
    {
        Shutdown();
    }

    //=========================================================================
    // Initialization
    //=========================================================================

    void ParticleSystem::Initialize(std::shared_ptr<RHI::RHIDevice> device, const ParticleSystemConfig& config)
    {
        if (m_Initialized) {
            ENGINE_CORE_WARN("ParticleSystem already initialized");
            return;
        }

        if (!device) {
            ENGINE_CORE_ERROR("ParticleSystem::Initialize: device is null");
            return;
        }

        m_Device = device;
        m_Config = config;

        // Create particle pool
        m_Pool = std::make_unique<ParticlePool>();
        ParticlePoolConfig poolConfig;
        poolConfig.MaxParticles = config.MaxParticles;
        poolConfig.EnableSorting = config.EnableSorting;
        poolConfig.EnableDoubleBuffering = true;
        poolConfig.DebugName = config.DebugName + "_Pool";

        if (!m_Pool->Initialize(device, poolConfig)) {
            ENGINE_CORE_ERROR("ParticleSystem: Failed to initialize particle pool");
            return;
        }

        // Reserve emitter slots
        m_Emitters.resize(config.MaxEmitters);

        // Create GPU buffers
        CreateBuffers();

        // Initialize update parameters
        m_UpdateParams.MaxParticles = config.MaxParticles;
        m_UpdateParams.Gravity = config.GlobalGravity;
        m_UpdateParams.EnableSorting = config.EnableSorting ? 1 : 0;
        m_UpdateParams.EnableCollision = config.EnableCollision ? 1 : 0;

        m_Initialized = true;

        ENGINE_CORE_INFO("ParticleSystem initialized: {} max particles, {} max emitters",
            config.MaxParticles, config.MaxEmitters);
    }

    void ParticleSystem::Shutdown()
    {
        if (!m_Initialized) {
            return;
        }

        m_EmitterParamsBuffer.reset();
        m_UpdateParamsBuffer.reset();

        if (m_Pool) {
            m_Pool->Shutdown();
            m_Pool.reset();
        }

        m_Emitters.clear();
        m_Device.reset();
        m_Stats.Reset();
        m_Initialized = false;

        ENGINE_CORE_INFO("ParticleSystem shutdown");
    }

    void ParticleSystem::CreateBuffers()
    {
        if (!m_Device) {
            ENGINE_CORE_ERROR("ParticleSystem::CreateBuffers: no device");
            return;
        }

        // Emitter parameters buffer (array of EmitterParams)
        RHI::BufferDescriptor emitterDesc;
        emitterDesc.size = sizeof(EmitterParams) * m_Config.MaxEmitters;
        emitterDesc.usage = RHI::BufferUsage::Storage;
        emitterDesc.mapped = true;
        m_EmitterParamsBuffer = m_Device->CreateBuffer(emitterDesc);

        // Update parameters buffer
        RHI::BufferDescriptor updateDesc;
        updateDesc.size = sizeof(UpdateParams);
        updateDesc.usage = RHI::BufferUsage::Uniform;
        updateDesc.mapped = true;
        m_UpdateParamsBuffer = m_Device->CreateBuffer(updateDesc);

        ENGINE_CORE_TRACE("ParticleSystem buffers created");
    }

    //=========================================================================
    // Configuration
    //=========================================================================

    void ParticleSystem::SetGravity(const Math::Vec3& gravity)
    {
        m_Config.GlobalGravity = gravity;
        m_UpdateParams.Gravity = gravity;
    }

    void ParticleSystem::SetWind(const Math::Vec3& direction, float strength)
    {
        m_Config.EnableWind = (strength > 0.0f);
        m_UpdateParams.WindDirection = glm::normalize(direction);
        m_UpdateParams.WindStrength = strength;
    }

    void ParticleSystem::SetSortingEnabled(bool enabled)
    {
        m_Config.EnableSorting = enabled;
        m_UpdateParams.EnableSorting = enabled ? 1 : 0;
    }

    void ParticleSystem::SetCollisionEnabled(bool enabled)
    {
        m_Config.EnableCollision = enabled;
        m_UpdateParams.EnableCollision = enabled ? 1 : 0;
    }

    void ParticleSystem::SetCollisionPlane(const Math::Vec4& plane)
    {
        m_UpdateParams.CollisionPlane = plane;
    }

    //=========================================================================
    // Emitter Management
    //=========================================================================

    EmitterHandle ParticleSystem::CreateEmitter(const EmitterParams& params)
    {
        if (!m_Initialized) {
            ENGINE_CORE_WARN("ParticleSystem::CreateEmitter: not initialized");
            return EmitterHandle{};
        }

        // Find free slot
        for (uint32_t i = 0; i < m_Emitters.size(); ++i) {
            if (!m_Emitters[i].Active) {
                m_Emitters[i].Params = params;
                m_Emitters[i].Params.EmitterId = m_NextEmitterId++;
                m_Emitters[i].Active = true;
                m_Emitters[i].Enabled = true;
                m_Emitters[i].EmitAccumulator = 0.0f;
                
                m_ActiveEmitterCount++;
                m_EmitterBufferDirty = true;

                ENGINE_CORE_TRACE("ParticleSystem: Created emitter {} at slot {}",
                    m_Emitters[i].Params.EmitterId, i);

                return EmitterHandle{ i };
            }
        }

        ENGINE_CORE_WARN("ParticleSystem::CreateEmitter: no free slots");
        return EmitterHandle{};
    }

    void ParticleSystem::UpdateEmitter(EmitterHandle handle, const EmitterParams& params)
    {
        if (!handle.IsValid() || handle.Id >= m_Emitters.size()) {
            ENGINE_CORE_WARN("ParticleSystem::UpdateEmitter: invalid handle");
            return;
        }

        if (!m_Emitters[handle.Id].Active) {
            ENGINE_CORE_WARN("ParticleSystem::UpdateEmitter: emitter not active");
            return;
        }

        // Preserve emitter ID
        uint32_t emitterId = m_Emitters[handle.Id].Params.EmitterId;
        m_Emitters[handle.Id].Params = params;
        m_Emitters[handle.Id].Params.EmitterId = emitterId;
        m_EmitterBufferDirty = true;
    }

    void ParticleSystem::DestroyEmitter(EmitterHandle handle)
    {
        if (!handle.IsValid() || handle.Id >= m_Emitters.size()) {
            ENGINE_CORE_WARN("ParticleSystem::DestroyEmitter: invalid handle");
            return;
        }

        if (m_Emitters[handle.Id].Active) {
            m_Emitters[handle.Id].Active = false;
            m_Emitters[handle.Id].Enabled = false;
            m_ActiveEmitterCount--;
            m_EmitterBufferDirty = true;

            ENGINE_CORE_TRACE("ParticleSystem: Destroyed emitter at slot {}", handle.Id);
        }
    }

    void ParticleSystem::SetEmitterPosition(EmitterHandle handle, const Math::Vec3& position)
    {
        if (!handle.IsValid() || handle.Id >= m_Emitters.size() || !m_Emitters[handle.Id].Active) {
            return;
        }
        m_Emitters[handle.Id].Params.Position = position;
        m_EmitterBufferDirty = true;
    }

    void ParticleSystem::SetEmitterDirection(EmitterHandle handle, const Math::Vec3& direction)
    {
        if (!handle.IsValid() || handle.Id >= m_Emitters.size() || !m_Emitters[handle.Id].Active) {
            return;
        }
        m_Emitters[handle.Id].Params.Direction = glm::normalize(direction);
        m_EmitterBufferDirty = true;
    }

    void ParticleSystem::SetEmitterSpawnRate(EmitterHandle handle, float rate)
    {
        if (!handle.IsValid() || handle.Id >= m_Emitters.size() || !m_Emitters[handle.Id].Active) {
            return;
        }
        m_Emitters[handle.Id].Params.SpawnRate = rate;
        m_EmitterBufferDirty = true;
    }

    void ParticleSystem::SetEmitterEnabled(EmitterHandle handle, bool enabled)
    {
        if (!handle.IsValid() || handle.Id >= m_Emitters.size() || !m_Emitters[handle.Id].Active) {
            return;
        }
        m_Emitters[handle.Id].Enabled = enabled;
    }

    const EmitterParams* ParticleSystem::GetEmitterParams(EmitterHandle handle) const
    {
        if (!handle.IsValid() || handle.Id >= m_Emitters.size() || !m_Emitters[handle.Id].Active) {
            return nullptr;
        }
        return &m_Emitters[handle.Id].Params;
    }

    void ParticleSystem::Burst(EmitterHandle handle, uint32_t count)
    {
        if (!handle.IsValid() || handle.Id >= m_Emitters.size() || !m_Emitters[handle.Id].Active) {
            ENGINE_CORE_WARN("ParticleSystem::Burst: invalid emitter");
            return;
        }

        // Add burst count to accumulator to emit next frame
        m_Emitters[handle.Id].EmitAccumulator += static_cast<float>(count);
        m_EmitterBufferDirty = true;
    }

    //=========================================================================
    // Camera Update
    //=========================================================================

    void ParticleSystem::UpdateCamera(const Math::Mat4& viewProjection, const Math::Vec3& cameraPosition)
    {
        m_UpdateParams.ViewProjection = viewProjection;
        m_UpdateParams.CameraPosition = cameraPosition;
    }

    //=========================================================================
    // Simulation
    //=========================================================================

    void ParticleSystem::Update(std::shared_ptr<RHI::RHICommandList> commandList, float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_Initialized || !commandList) {
            return;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        m_TotalTime += deltaTime;
        m_UpdateParams.DeltaTime = deltaTime;

        // Swap double buffers
        m_Pool->SwapBuffers();

        // Reset counters for new frame
        m_Pool->ResetCounters(commandList);

        // Step 1: Emit new particles
        Emit(commandList);

        // Step 2: Update/simulate existing particles
        Simulate(commandList, deltaTime);

        // Step 3: Sort particles by depth (if enabled)
        if (m_Config.EnableSorting) {
            Sort(commandList);
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        m_Stats.TotalTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    }

    void ParticleSystem::Emit(std::shared_ptr<RHI::RHICommandList> commandList)
    {
        PROFILE_FUNCTION();

        if (m_ActiveEmitterCount == 0) {
            return;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        // Update emitter buffer with current frame data
        UpdateEmitterBuffer();

        // Calculate total particles to emit this frame
        uint32_t totalEmit = 0;
        for (auto& emitter : m_Emitters) {
            if (emitter.Active && emitter.Enabled) {
                emitter.EmitAccumulator += emitter.Params.SpawnRate * m_UpdateParams.DeltaTime;
                uint32_t toEmit = static_cast<uint32_t>(emitter.EmitAccumulator);
                emitter.EmitAccumulator -= static_cast<float>(toEmit);
                emitter.Params.EmitAccumulator = static_cast<float>(toEmit);
                totalEmit += toEmit;
            }
        }

        if (totalEmit > 0) {
            // Re-upload emitter buffer with emit counts
            UpdateEmitterBuffer();

            // Dispatch emit compute shader
            // Each workgroup handles PARTICLE_WORKGROUP_SIZE emissions
            uint32_t dispatchGroups = CalculateDispatchGroups(totalEmit, PARTICLE_WORKGROUP_SIZE);
            commandList->Dispatch(dispatchGroups, 1, 1);

            m_Stats.ParticlesSpawned = totalEmit;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        m_Stats.EmitTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    }

    void ParticleSystem::Simulate(std::shared_ptr<RHI::RHICommandList> commandList, float /*deltaTime*/)
    {
        PROFILE_FUNCTION();

        auto startTime = std::chrono::high_resolution_clock::now();

        // Update parameters buffer
        UpdateParamsBuffer();

        // Dispatch update compute shader
        // Process all potentially active particles
        uint32_t dispatchGroups = CalculateDispatchGroups(m_Config.MaxParticles, PARTICLE_WORKGROUP_SIZE);
        commandList->Dispatch(dispatchGroups, 1, 1);

        auto endTime = std::chrono::high_resolution_clock::now();
        m_Stats.UpdateTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    }

    void ParticleSystem::Sort(std::shared_ptr<RHI::RHICommandList> commandList)
    {
        PROFILE_FUNCTION();

        if (!m_Config.EnableSorting) {
            return;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        // Dispatch sort compute shader (bitonic sort)
        // This typically requires multiple passes for large particle counts
        // For now, dispatch based on estimated alive count
        uint32_t estimatedAlive = m_Stats.ActiveParticles;
        if (estimatedAlive == 0) {
            estimatedAlive = m_Config.MaxParticles / 4; // Conservative estimate
        }

        uint32_t dispatchGroups = CalculateDispatchGroups(estimatedAlive, SORT_WORKGROUP_SIZE);
        commandList->Dispatch(dispatchGroups, 1, 1);

        auto endTime = std::chrono::high_resolution_clock::now();
        m_Stats.SortTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    }

    void ParticleSystem::UpdateEmitterBuffer()
    {
        if (!m_EmitterParamsBuffer || !m_EmitterBufferDirty) {
            return;
        }

        void* data = nullptr;
        m_EmitterParamsBuffer->Map(&data);
        if (data) {
            EmitterParams* buffer = static_cast<EmitterParams*>(data);
            for (uint32_t i = 0; i < m_Emitters.size(); ++i) {
                if (m_Emitters[i].Active && m_Emitters[i].Enabled) {
                    m_Emitters[i].Params.Time = m_TotalTime;
                    m_Emitters[i].Params.DeltaTime = m_UpdateParams.DeltaTime;
                    buffer[i] = m_Emitters[i].Params;
                } else {
                    // Zero out inactive emitters
                    std::memset(&buffer[i], 0, sizeof(EmitterParams));
                }
            }
            m_EmitterParamsBuffer->Unmap();
        }

        m_EmitterBufferDirty = false;
    }

    void ParticleSystem::UpdateParamsBuffer()
    {
        if (!m_UpdateParamsBuffer) {
            return;
        }

        void* data = nullptr;
        m_UpdateParamsBuffer->Map(&data);
        if (data) {
            std::memcpy(data, &m_UpdateParams, sizeof(UpdateParams));
            m_UpdateParamsBuffer->Unmap();
        }
    }

    uint32_t ParticleSystem::CalculateDispatchGroups(uint32_t count, uint32_t workgroupSize) const
    {
        return (count + workgroupSize - 1) / workgroupSize;
    }

    //=========================================================================
    // Rendering Buffers
    //=========================================================================

    std::shared_ptr<RHI::RHIBuffer> ParticleSystem::GetParticleBuffer() const
    {
        return m_Pool ? m_Pool->GetParticleBuffer() : nullptr;
    }

    std::shared_ptr<RHI::RHIBuffer> ParticleSystem::GetSortedIndexBuffer() const
    {
        return m_Pool ? m_Pool->GetSortedIndexBuffer() : nullptr;
    }

    std::shared_ptr<RHI::RHIBuffer> ParticleSystem::GetAliveListBuffer() const
    {
        return m_Pool ? m_Pool->GetAliveListBuffer() : nullptr;
    }

    std::shared_ptr<RHI::RHIBuffer> ParticleSystem::GetDrawIndirectBuffer() const
    {
        return m_Pool ? m_Pool->GetDrawIndirectBuffer() : nullptr;
    }

    //=========================================================================
    // Statistics
    //=========================================================================

    void ParticleSystem::UpdateStats()
    {
        if (!m_Pool) {
            return;
        }

        m_Stats.ActiveParticles = m_Pool->UpdateStats();
        m_Stats.ActiveEmitters = m_ActiveEmitterCount;
    }

    //=========================================================================
    // Clear
    //=========================================================================

    void ParticleSystem::ClearAllParticles()
    {
        if (!m_Pool) {
            return;
        }

        // Re-initialize the dead list to mark all particles as free
        m_Pool->InitializeDeadList();
        m_Stats.ActiveParticles = 0;

        ENGINE_CORE_TRACE("ParticleSystem: Cleared all particles");
    }

    void ParticleSystem::ClearEmitterParticles(EmitterHandle handle)
    {
        if (!handle.IsValid() || handle.Id >= m_Emitters.size() || !m_Emitters[handle.Id].Active) {
            ENGINE_CORE_WARN("ParticleSystem::ClearEmitterParticles: invalid emitter");
            return;
        }

        // Note: Clearing specific emitter particles would require a compute shader pass
        // to mark particles with matching emitter ID as dead. For now, log a warning.
        ENGINE_CORE_WARN("ParticleSystem::ClearEmitterParticles: per-emitter clear not yet implemented");
    }

} // namespace Particles
} // namespace Renderer
} // namespace Core
