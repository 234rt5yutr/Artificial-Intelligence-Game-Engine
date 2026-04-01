#include "ParticleRenderPass.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>

namespace Core {
namespace Renderer {
namespace Particles {

    //=========================================================================
    // Constants
    //=========================================================================

    namespace {
        constexpr uint32_t PARTICLE_SORT_WORKGROUP_SIZE = 256;
        constexpr uint32_t PARTICLE_QUAD_VERTICES = 4;
    }

    //=========================================================================
    // Constructor / Destructor
    //=========================================================================

    ParticleRenderPass::ParticleRenderPass() = default;

    ParticleRenderPass::~ParticleRenderPass() {
        if (m_Initialized) {
            Shutdown();
        }
    }

    //=========================================================================
    // Initialization
    //=========================================================================

    void ParticleRenderPass::Initialize(
        std::shared_ptr<RHI::RHIDevice> device,
        const ParticleRenderPassConfig& config)
    {
        if (m_Initialized) {
            ENGINE_CORE_WARN("ParticleRenderPass already initialized");
            return;
        }

        ENGINE_CORE_INFO("Initializing ParticleRenderPass...");

        m_Device = device;
        m_Config = config;

        // Create resources
        CreateRenderTargets();
        CreateRenderPasses();
        CreatePipelines();
        CreateSortPipeline();
        CreateDescriptorSets();

        m_Initialized = true;
        ENGINE_CORE_INFO("ParticleRenderPass initialized successfully");
    }

    void ParticleRenderPass::Shutdown() {
        if (!m_Initialized) {
            return;
        }

        ENGINE_CORE_INFO("Shutting down ParticleRenderPass...");

        // Wait for GPU to finish
        if (m_Device) {
            m_Device->WaitIdle();
        }

        // Release pipelines
        m_SortPipeline.reset();
        m_OpaquePipeline.reset();
        for (auto& pipeline : m_BlendPipelines) {
            pipeline.reset();
        }

        // Release render passes
        m_OpaqueRenderPass.reset();
        m_TransparentRenderPass.reset();

        // Release textures
        m_ColorTexture.reset();
        m_DepthTexture.reset();
        m_SceneDepthTexture.reset();

        // Release buffers
        m_CameraBuffer.reset();

        m_Device.reset();
        m_Initialized = false;

        ENGINE_CORE_INFO("ParticleRenderPass shutdown complete");
    }

    void ParticleRenderPass::Resize(uint32_t width, uint32_t height) {
        if (!m_Initialized) {
            return;
        }

        if (width == m_Config.Width && height == m_Config.Height) {
            return;
        }

        ENGINE_CORE_INFO("Resizing ParticleRenderPass to {}x{}", width, height);

        m_Config.Width = width;
        m_Config.Height = height;

        // Recreate render targets
        m_Device->WaitIdle();
        m_ColorTexture.reset();
        m_DepthTexture.reset();
        m_OpaqueRenderPass.reset();
        m_TransparentRenderPass.reset();

        CreateRenderTargets();
        CreateRenderPasses();
    }

    //=========================================================================
    // Configuration
    //=========================================================================

    void ParticleRenderPass::SetSoftParticlesEnabled(bool enabled) {
        m_Config.EnableSoftParticles = enabled;
    }

    void ParticleRenderPass::SetSoftParticleScale(float scale) {
        m_Config.SoftParticleScale = std::max(0.001f, scale);
    }

    void ParticleRenderPass::SetDepthSortingEnabled(bool enabled) {
        m_Config.EnableDepthSorting = enabled;
    }

    void ParticleRenderPass::SetSceneDepthTexture(std::shared_ptr<RHI::RHITexture> depthTexture) {
        m_SceneDepthTexture = depthTexture;
    }

    //=========================================================================
    // Camera Update
    //=========================================================================

    void ParticleRenderPass::UpdateCamera(
        const Math::Mat4& view,
        const Math::Mat4& projection,
        const Math::Vec3& cameraPosition)
    {
        m_ViewMatrix = view;
        m_ProjectionMatrix = projection;
        m_ViewProjectionMatrix = projection * view;
        m_CameraPosition = cameraPosition;

        // Extract camera right and up vectors from view matrix
        // View matrix is the inverse of camera transform, so we transpose to get camera axes
        m_CameraRight = Math::Vec3(view[0][0], view[1][0], view[2][0]);
        m_CameraUp = Math::Vec3(view[0][1], view[1][1], view[2][1]);

        m_CameraBufferDirty = true;
    }

    //=========================================================================
    // Sorting
    //=========================================================================

    void ParticleRenderPass::SortParticlesByDepth(
        std::shared_ptr<RHI::RHICommandList> commandList,
        ParticleSystem* particleSystem)
    {
        if (!m_Initialized || !particleSystem || !particleSystem->IsInitialized()) {
            return;
        }

        if (!m_Config.EnableDepthSorting) {
            return;
        }

        const uint32_t particleCount = particleSystem->GetActiveParticleCount();
        if (particleCount <= 1) {
            return;
        }

        // Calculate sort stages
        auto sortStages = CalculateSortStages(particleCount);
        m_Stats.SortIterations = 0;

        // Execute bitonic sort passes
        for (const auto& stage : sortStages) {
            for (uint32_t pass = 0; pass <= stage.PassCount; ++pass) {
                SortPushConstants pc{};
                pc.StageIndex = stage.StageIndex;
                pc.PassIndex = pass;
                pc.SortDirection = 1; // Descending (back-to-front)
                pc.MaxElements = particleCount;

                // Bind sort pipeline and dispatch
                // Note: In a real implementation, this would:
                // 1. Bind the compute pipeline
                // 2. Bind descriptor sets (sort keys, sorted indices, counters)
                // 3. Push constants
                // 4. Dispatch compute

                uint32_t dispatchX = CalculateDispatchGroups(particleCount / 2, PARTICLE_SORT_WORKGROUP_SIZE);
                commandList->Dispatch(dispatchX, 1, 1);

                m_Stats.SortIterations++;

                // Memory barrier between passes
                // In Vulkan, this would be vkCmdPipelineBarrier
            }
        }
    }

    std::vector<SortStageInfo> ParticleRenderPass::CalculateSortStages(uint32_t particleCount) const {
        std::vector<SortStageInfo> stages;

        if (particleCount <= 1) {
            return stages;
        }

        // Round up to next power of two for bitonic sort
        uint32_t n = NextPowerOfTwo(particleCount);
        uint32_t numStages = static_cast<uint32_t>(std::log2(static_cast<double>(n)));

        // Limit stages if configured
        numStages = std::min(numStages, m_Config.MaxSortIterations);

        for (uint32_t stage = 0; stage < numStages; ++stage) {
            SortStageInfo info{};
            info.StageIndex = stage;
            info.PassCount = stage;
            info.DispatchSize = CalculateDispatchGroups(n / 2, PARTICLE_SORT_WORKGROUP_SIZE);
            stages.push_back(info);
        }

        return stages;
    }

    //=========================================================================
    // Rendering
    //=========================================================================

    void ParticleRenderPass::BeginPass(std::shared_ptr<RHI::RHICommandList> commandList) {
        if (!m_Initialized || !commandList) {
            return;
        }

        if (m_PassActive) {
            ENGINE_CORE_WARN("ParticleRenderPass::BeginPass called while pass is already active");
            return;
        }

        // Update camera buffer if dirty
        if (m_CameraBufferDirty) {
            UpdateCameraBuffer();
            m_CameraBufferDirty = false;
        }

        // Begin the transparent render pass (most common case)
        if (m_TransparentRenderPass) {
            commandList->BeginRenderPass(m_TransparentRenderPass);
        }

        m_PassActive = true;
    }

    void ParticleRenderPass::EndPass(std::shared_ptr<RHI::RHICommandList> commandList) {
        if (!m_PassActive || !commandList) {
            return;
        }

        commandList->EndRenderPass();
        m_PassActive = false;
    }

    void ParticleRenderPass::RenderParticles(
        std::shared_ptr<RHI::RHICommandList> commandList,
        ParticleSystem* particleSystem,
        ParticleBlendMode blendMode)
    {
        if (!m_Initialized || !particleSystem || !particleSystem->IsInitialized()) {
            return;
        }

        const uint32_t particleCount = particleSystem->GetActiveParticleCount();
        if (particleCount == 0) {
            return;
        }

        // Update camera buffer if needed
        if (m_CameraBufferDirty) {
            UpdateCameraBuffer();
            m_CameraBufferDirty = false;
        }

        m_Stats.ParticlesRendered = particleCount;
        m_Stats.TransparentParticles = particleCount; // Assume all transparent for now
        m_Stats.BatchCount = 1;

        // Configure push constants
        RenderPushConstants pc{};
        pc.UseSortedIndices = m_Config.EnableDepthSorting ? 1u : 0u;
        pc.SoftParticles = m_Config.EnableSoftParticles && m_SceneDepthTexture ? 1u : 0u;
        pc.SoftParticleScale = m_Config.SoftParticleScale;
        pc.UseTexture = 0u; // Procedural particles by default
        pc.BlendMode = static_cast<uint32_t>(blendMode);

        // In a full implementation:
        // 1. Bind the appropriate blend pipeline
        // 2. Bind descriptor sets (camera UBO, particle buffer, sorted indices, textures)
        // 3. Set push constants
        // 4. Issue indirect draw call

        // Draw particles using indirect rendering
        // The draw command buffer was populated by the particle system's compute shaders
        auto drawBuffer = particleSystem->GetDrawIndirectBuffer();
        if (drawBuffer) {
            // vkCmdDrawIndirect equivalent
            // For now, use regular draw as placeholder
            commandList->Draw(
                PARTICLE_QUAD_VERTICES,  // vertexCount (quad)
                particleCount,           // instanceCount
                0,                       // firstVertex
                0                        // firstInstance
            );
        }
    }

    void ParticleRenderPass::RenderParticleBatch(
        std::shared_ptr<RHI::RHICommandList> commandList,
        ParticleSystem* particleSystem,
        ParticleBlendMode blendMode,
        uint32_t particleOffset,
        uint32_t particleCount)
    {
        if (!m_Initialized || !particleSystem || particleCount == 0) {
            return;
        }

        // Get the pipeline for this blend mode
        auto pipeline = GetBlendPipeline(blendMode);
        if (!pipeline) {
            ENGINE_CORE_WARN("No pipeline for blend mode: {}", BlendModeToString(blendMode));
            return;
        }

        // Configure push constants
        RenderPushConstants pc{};
        pc.UseSortedIndices = m_Config.EnableDepthSorting ? 1u : 0u;
        pc.SoftParticles = m_Config.EnableSoftParticles && m_SceneDepthTexture ? 1u : 0u;
        pc.SoftParticleScale = m_Config.SoftParticleScale;
        pc.BlendMode = static_cast<uint32_t>(blendMode);

        // In a full implementation:
        // 1. Bind pipeline
        // 2. Bind descriptor sets
        // 3. Push constants
        // 4. Draw with offset

        commandList->Draw(
            PARTICLE_QUAD_VERTICES,
            particleCount,
            0,
            particleOffset
        );

        m_Stats.ParticlesRendered += particleCount;
        m_Stats.BatchCount++;
    }

    void ParticleRenderPass::RenderParticleBatches(
        std::shared_ptr<RHI::RHICommandList> commandList,
        ParticleSystem* particleSystem,
        const std::vector<ParticleBatch>& batches)
    {
        if (!m_Initialized || !particleSystem || batches.empty()) {
            return;
        }

        // Separate opaque and transparent batches
        std::vector<const ParticleBatch*> opaqueBatches;
        std::vector<const ParticleBatch*> transparentBatches;

        for (const auto& batch : batches) {
            if (batch.IsOpaque) {
                opaqueBatches.push_back(&batch);
            } else {
                transparentBatches.push_back(&batch);
            }
        }

        // Render opaque particles first (with depth writing)
        if (!opaqueBatches.empty() && m_OpaqueRenderPass) {
            commandList->BeginRenderPass(m_OpaqueRenderPass);
            
            for (const auto* batch : opaqueBatches) {
                RenderParticleBatch(
                    commandList,
                    particleSystem,
                    batch->BlendMode,
                    batch->ParticleOffset,
                    batch->ParticleCount
                );
                m_Stats.OpaqueParticles += batch->ParticleCount;
            }
            
            commandList->EndRenderPass();
        }

        // Render transparent particles (without depth writing, sorted)
        if (!transparentBatches.empty() && m_TransparentRenderPass) {
            commandList->BeginRenderPass(m_TransparentRenderPass);
            
            // Render in sorted order (batches should already be sorted by depth)
            for (const auto* batch : transparentBatches) {
                RenderParticleBatch(
                    commandList,
                    particleSystem,
                    batch->BlendMode,
                    batch->ParticleOffset,
                    batch->ParticleCount
                );
                m_Stats.TransparentParticles += batch->ParticleCount;
            }
            
            commandList->EndRenderPass();
        }
    }

    //=========================================================================
    // Pipeline Access
    //=========================================================================

    std::shared_ptr<RHI::RHIPipelineState> ParticleRenderPass::GetBlendPipeline(
        ParticleBlendMode blendMode) const
    {
        auto index = static_cast<size_t>(blendMode);
        if (index < m_BlendPipelines.size()) {
            return m_BlendPipelines[index];
        }
        return nullptr;
    }

    //=========================================================================
    // Blend State Factory
    //=========================================================================

    RHI::RenderTargetBlendState ParticleRenderPass::CreateBlendState(ParticleBlendMode blendMode) {
        RHI::RenderTargetBlendState state{};
        state.blendEnable = true;
        state.colorWriteMask = RHI::ColorWriteMask::All;

        switch (blendMode) {
            case ParticleBlendMode::Additive:
                // Result = SrcAlpha * SrcColor + DstColor
                // Fire, magic, glowing effects
                state.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
                state.dstColorBlendFactor = RHI::BlendFactor::One;
                state.colorBlendOp = RHI::BlendOp::Add;
                state.srcAlphaBlendFactor = RHI::BlendFactor::SrcAlpha;
                state.dstAlphaBlendFactor = RHI::BlendFactor::One;
                state.alphaBlendOp = RHI::BlendOp::Add;
                break;

            case ParticleBlendMode::AlphaBlend:
                // Result = SrcAlpha * SrcColor + (1 - SrcAlpha) * DstColor
                // Smoke, dust, clouds
                state.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
                state.dstColorBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
                state.colorBlendOp = RHI::BlendOp::Add;
                state.srcAlphaBlendFactor = RHI::BlendFactor::SrcAlpha;
                state.dstAlphaBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
                state.alphaBlendOp = RHI::BlendOp::Add;
                break;

            case ParticleBlendMode::Premultiplied:
                // Result = SrcColor + (1 - SrcAlpha) * DstColor
                // Pre-multiplied alpha textures
                state.srcColorBlendFactor = RHI::BlendFactor::One;
                state.dstColorBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
                state.colorBlendOp = RHI::BlendOp::Add;
                state.srcAlphaBlendFactor = RHI::BlendFactor::One;
                state.dstAlphaBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
                state.alphaBlendOp = RHI::BlendOp::Add;
                break;

            case ParticleBlendMode::Multiply:
                // Result = SrcColor * DstColor
                // Dark effects, shadows
                state.srcColorBlendFactor = RHI::BlendFactor::DstColor;
                state.dstColorBlendFactor = RHI::BlendFactor::Zero;
                state.colorBlendOp = RHI::BlendOp::Add;
                state.srcAlphaBlendFactor = RHI::BlendFactor::DstAlpha;
                state.dstAlphaBlendFactor = RHI::BlendFactor::Zero;
                state.alphaBlendOp = RHI::BlendOp::Add;
                break;

            case ParticleBlendMode::SoftAdditive:
                // Result = (1 - SrcColor) * SrcColor + DstColor
                // Softer glow effects
                state.srcColorBlendFactor = RHI::BlendFactor::OneMinusDstColor;
                state.dstColorBlendFactor = RHI::BlendFactor::One;
                state.colorBlendOp = RHI::BlendOp::Add;
                state.srcAlphaBlendFactor = RHI::BlendFactor::OneMinusDstAlpha;
                state.dstAlphaBlendFactor = RHI::BlendFactor::One;
                state.alphaBlendOp = RHI::BlendOp::Add;
                break;

            default:
                // Default to alpha blend
                state.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
                state.dstColorBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
                state.colorBlendOp = RHI::BlendOp::Add;
                state.srcAlphaBlendFactor = RHI::BlendFactor::SrcAlpha;
                state.dstAlphaBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
                state.alphaBlendOp = RHI::BlendOp::Add;
                break;
        }

        return state;
    }

    //=========================================================================
    // Internal Resource Creation
    //=========================================================================

    void ParticleRenderPass::CreateRenderTargets() {
        // Create color texture for particle output
        RHI::TextureDescriptor colorDesc{};
        colorDesc.dimension = RHI::TextureDimension::Texture2D;
        colorDesc.width = m_Config.Width;
        colorDesc.height = m_Config.Height;
        colorDesc.depth = 1;
        colorDesc.mipLevels = 1;
        colorDesc.arrayLayers = 1;
        colorDesc.format = RHI::TextureFormat::RGBA16_SFLOAT; // HDR support
        colorDesc.usage = RHI::TextureUsage::ColorAttachment;

        m_ColorTexture = m_Device->CreateTexture(colorDesc);

        // Create depth texture for particle depth testing
        RHI::TextureDescriptor depthDesc{};
        depthDesc.dimension = RHI::TextureDimension::Texture2D;
        depthDesc.width = m_Config.Width;
        depthDesc.height = m_Config.Height;
        depthDesc.depth = 1;
        depthDesc.mipLevels = 1;
        depthDesc.arrayLayers = 1;
        depthDesc.format = RHI::TextureFormat::D32_SFLOAT;
        depthDesc.usage = RHI::TextureUsage::DepthStencil;

        m_DepthTexture = m_Device->CreateTexture(depthDesc);
    }

    void ParticleRenderPass::CreateRenderPasses() {
        // Opaque particle render pass (depth write enabled)
        {
            RHI::RenderPassDescriptor passDesc{};
            passDesc.name = m_Config.DebugName + "_Opaque";
            passDesc.width = m_Config.Width;
            passDesc.height = m_Config.Height;

            RHI::RenderPassColorAttachment colorAttachment{};
            colorAttachment.texture = m_ColorTexture;
            colorAttachment.loadOp = RHI::RenderPassLoadOp::Load;
            colorAttachment.storeOp = RHI::RenderPassStoreOp::Store;
            colorAttachment.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
            passDesc.colorAttachments.push_back(colorAttachment);

            passDesc.hasDepthStencil = true;
            passDesc.depthStencilAttachment.texture = m_DepthTexture;
            passDesc.depthStencilAttachment.depthLoadOp = RHI::RenderPassLoadOp::Load;
            passDesc.depthStencilAttachment.depthStoreOp = RHI::RenderPassStoreOp::Store;
            passDesc.depthStencilAttachment.stencilLoadOp = RHI::RenderPassLoadOp::DontCare;
            passDesc.depthStencilAttachment.stencilStoreOp = RHI::RenderPassStoreOp::DontCare;
            passDesc.depthStencilAttachment.clearDepth = 1.0f;

            m_OpaqueRenderPass = m_Device->CreateRenderPass(passDesc);
        }

        // Transparent particle render pass (depth write disabled via pipeline state)
        {
            RHI::RenderPassDescriptor passDesc{};
            passDesc.name = m_Config.DebugName + "_Transparent";
            passDesc.width = m_Config.Width;
            passDesc.height = m_Config.Height;

            RHI::RenderPassColorAttachment colorAttachment{};
            colorAttachment.texture = m_ColorTexture;
            colorAttachment.loadOp = RHI::RenderPassLoadOp::Load;
            colorAttachment.storeOp = RHI::RenderPassStoreOp::Store;
            colorAttachment.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
            passDesc.colorAttachments.push_back(colorAttachment);

            passDesc.hasDepthStencil = true;
            passDesc.depthStencilAttachment.texture = m_DepthTexture;
            // Load depth for depth testing, but don't store (particles don't write depth)
            passDesc.depthStencilAttachment.depthLoadOp = RHI::RenderPassLoadOp::Load;
            passDesc.depthStencilAttachment.depthStoreOp = RHI::RenderPassStoreOp::DontCare;
            passDesc.depthStencilAttachment.stencilLoadOp = RHI::RenderPassLoadOp::DontCare;
            passDesc.depthStencilAttachment.stencilStoreOp = RHI::RenderPassStoreOp::DontCare;

            m_TransparentRenderPass = m_Device->CreateRenderPass(passDesc);
        }
    }

    void ParticleRenderPass::CreatePipelines() {
        // Create a pipeline for each blend mode
        for (uint32_t i = 0; i < static_cast<uint32_t>(ParticleBlendMode::Count); ++i) {
            auto blendMode = static_cast<ParticleBlendMode>(i);
            
            RHI::GraphicsPipelineDescriptor pipelineDesc{};
            
            // No vertex buffer - procedural quad generation in vertex shader
            pipelineDesc.vertexBindings.clear();
            pipelineDesc.vertexAttributes.clear();

            // Triangle strip for quad rendering
            pipelineDesc.topology = RHI::PrimitiveTopology::TriangleStrip;
            pipelineDesc.polygonMode = RHI::PolygonMode::Fill;
            pipelineDesc.cullMode = RHI::CullMode::None; // Particles are billboards, no culling

            // Depth testing enabled, but no depth writing for transparent particles
            pipelineDesc.depthTestEnable = true;
            pipelineDesc.depthWriteEnable = false;
            pipelineDesc.depthCompareOp = RHI::CompareOp::Less;

            // Configure blend state for this mode
            pipelineDesc.blendStates.push_back(CreateBlendState(blendMode));
            pipelineDesc.renderTargetCount = 1;

            // Note: Shaders would be loaded here
            // pipelineDesc.shaders = { vertexShader, fragmentShader };

            m_BlendPipelines[i] = m_Device->CreateGraphicsPipelineState(pipelineDesc);
        }

        // Create opaque particle pipeline (with depth writing)
        {
            RHI::GraphicsPipelineDescriptor pipelineDesc{};
            pipelineDesc.vertexBindings.clear();
            pipelineDesc.vertexAttributes.clear();
            pipelineDesc.topology = RHI::PrimitiveTopology::TriangleStrip;
            pipelineDesc.polygonMode = RHI::PolygonMode::Fill;
            pipelineDesc.cullMode = RHI::CullMode::None;

            // Opaque particles write depth
            pipelineDesc.depthTestEnable = true;
            pipelineDesc.depthWriteEnable = true;
            pipelineDesc.depthCompareOp = RHI::CompareOp::Less;

            // No blending for opaque
            RHI::RenderTargetBlendState blendState{};
            blendState.blendEnable = false;
            pipelineDesc.blendStates.push_back(blendState);
            pipelineDesc.renderTargetCount = 1;

            m_OpaquePipeline = m_Device->CreateGraphicsPipelineState(pipelineDesc);
        }
    }

    void ParticleRenderPass::CreateSortPipeline() {
        // Sort pipeline is a compute pipeline
        // In a full implementation, this would create a VkComputePipeline
        // with the particle_sort.comp shader

        // Placeholder - actual implementation would:
        // 1. Load particle_sort.comp SPIR-V
        // 2. Create compute pipeline with appropriate layout
        // 3. Set up descriptor set layout for sort buffers

        // m_SortPipeline = m_Device->CreateComputePipelineState(computeDesc);
    }

    void ParticleRenderPass::CreateDescriptorSets() {
        // Create camera uniform buffer
        RHI::BufferDescriptor cameraBufferDesc{};
        cameraBufferDesc.size = sizeof(float) * 64; // View, Proj, VP matrices + camera data
        cameraBufferDesc.usage = RHI::BufferUsage::Uniform;

        m_CameraBuffer = m_Device->CreateBuffer(cameraBufferDesc);

        // In a full implementation:
        // 1. Create descriptor pool
        // 2. Create descriptor set layouts
        // 3. Allocate descriptor sets
        // 4. Write descriptors for camera buffer, particle buffers, textures
    }

    void ParticleRenderPass::UpdateCameraBuffer() {
        // In a full implementation, this would map the buffer and update:
        // - View matrix
        // - Projection matrix
        // - ViewProjection matrix
        // - Camera position
        // - Camera right/up vectors
        // - Time

        // The buffer layout matches the CameraUBO in particle.vert
    }

    //=========================================================================
    // Sort Helpers
    //=========================================================================

    uint32_t ParticleRenderPass::CalculateDispatchGroups(uint32_t count, uint32_t workgroupSize) const {
        return (count + workgroupSize - 1) / workgroupSize;
    }

    uint32_t ParticleRenderPass::NextPowerOfTwo(uint32_t n) const {
        if (n == 0) return 1;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        return n + 1;
    }

} // namespace Particles
} // namespace Renderer
} // namespace Core
