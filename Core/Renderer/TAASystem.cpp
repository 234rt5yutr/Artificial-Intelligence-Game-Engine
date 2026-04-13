#include "Core/Renderer/TAASystem.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include <algorithm>
#include <chrono>
#include <cmath>

// Use engine core log macros
#define LOG_INFO    ENGINE_CORE_INFO
#define LOG_WARN    ENGINE_CORE_WARN
#define LOG_DEBUG   ENGINE_CORE_TRACE
#define LOG_ERROR   ENGINE_CORE_ERROR

namespace Core {
namespace Renderer {

    //=========================================================================
    // Quality Presets
    //=========================================================================

    TAAQualitySettings TAAConfig::GetPresetSettings(TAAQuality quality)
    {
        TAAQualitySettings settings;

        switch (quality) {
            case TAAQuality::Low:
                settings.BlendFactor = 0.8f;
                settings.VarianceClipGamma = 1.5f;
                settings.Sharpness = 0.0f;
                settings.EnableVarianceClipping = false;
                settings.EnableSharpening = false;
                settings.UseCatmullRomSampling = false;
                settings.Pattern = JitterPattern::Grid4x4;
                break;

            case TAAQuality::Medium:
                settings.BlendFactor = 0.85f;
                settings.VarianceClipGamma = 1.25f;
                settings.Sharpness = 0.15f;
                settings.EnableVarianceClipping = true;
                settings.EnableSharpening = false;
                settings.UseCatmullRomSampling = true;
                settings.Pattern = JitterPattern::Halton23;
                break;

            case TAAQuality::High:
                settings.BlendFactor = 0.9f;
                settings.VarianceClipGamma = 1.25f;
                settings.Sharpness = 0.25f;
                settings.EnableVarianceClipping = true;
                settings.EnableSharpening = true;
                settings.UseCatmullRomSampling = true;
                settings.Pattern = JitterPattern::Halton23;
                break;

            case TAAQuality::Ultra:
                settings.BlendFactor = 0.95f;
                settings.VarianceClipGamma = 1.0f;
                settings.Sharpness = 0.3f;
                settings.EnableVarianceClipping = true;
                settings.EnableSharpening = true;
                settings.UseCatmullRomSampling = true;
                settings.Pattern = JitterPattern::R2;
                break;
        }

        return settings;
    }

    //=========================================================================
    // Constructor / Destructor
    //=========================================================================

    TAASystem::TAASystem()
        : m_CurrentView(1.0f)
        , m_CurrentProjection(1.0f)
        , m_PreviousView(1.0f)
        , m_PreviousProjection(1.0f)
    {
    }

    TAASystem::~TAASystem()
    {
        Shutdown();
    }

    //=========================================================================
    // Initialization
    //=========================================================================

    void TAASystem::Initialize(std::shared_ptr<RHI::RHIDevice> device, uint32_t width, uint32_t height)
    {
        if (m_Initialized) {
            LOG_WARN("TAASystem already initialized");
            return;
        }

        m_Device = device;
        m_Width = width;
        m_Height = height;

        CreateTextures();
        CreateBuffers();

        // Initialize parameters
        m_Params.Resolution = Math::Vec4(
            static_cast<float>(width),
            static_cast<float>(height),
            1.0f / static_cast<float>(width),
            1.0f / static_cast<float>(height)
        );

        // Apply default quality preset
        SetQuality(m_Config.Quality);

        m_Initialized = true;
        LOG_INFO("TAASystem initialized ({}x{})", width, height);
    }

    void TAASystem::Shutdown()
    {
        if (!m_Initialized) {
            return;
        }

        m_HistoryColor[0].reset();
        m_HistoryColor[1].reset();
        m_ResolvedColor.reset();
        m_MotionVectorTexture.reset();
        m_TAAParamsBuffer.reset();
        m_MotionVectorUBOBuffer.reset();
        m_Device.reset();

        m_Initialized = false;
        LOG_INFO("TAASystem shutdown");
    }

    void TAASystem::Resize(uint32_t width, uint32_t height)
    {
        if (width == m_Width && height == m_Height) {
            return;
        }

        m_Width = width;
        m_Height = height;

        // Recreate textures at new size
        CreateTextures();

        // Update resolution in params
        m_Params.Resolution = Math::Vec4(
            static_cast<float>(width),
            static_cast<float>(height),
            1.0f / static_cast<float>(width),
            1.0f / static_cast<float>(height)
        );

        // Invalidate history on resize
        InvalidateHistory();

        LOG_INFO("TAASystem resized to {}x{}", width, height);
    }

    void TAASystem::CreateTextures()
    {
        if (!m_Device) {
            return;
        }

        // History textures (RGBA16F for HDR)
        RHI::TextureDescriptor historyDesc;
        historyDesc.width = m_Width;
        historyDesc.height = m_Height;
        historyDesc.format = RHI::TextureFormat::RGBA8_UNORM;  // Use RGBA16F when available
        historyDesc.usage = RHI::TextureUsage::RenderTarget;
        historyDesc.mipLevels = 1;

        m_HistoryColor[0] = m_Device->CreateTexture(historyDesc);
        m_HistoryColor[1] = m_Device->CreateTexture(historyDesc);

        // Resolved output texture
        RHI::TextureDescriptor resolvedDesc = historyDesc;
        resolvedDesc.usage = RHI::TextureUsage::Storage;
        m_ResolvedColor = m_Device->CreateTexture(resolvedDesc);

        // Motion vector texture (RG16F for 2D motion)
        RHI::TextureDescriptor motionDesc;
        motionDesc.width = m_Width;
        motionDesc.height = m_Height;
        motionDesc.format = RHI::TextureFormat::RGBA8_UNORM;  // Use RG16F when available
        motionDesc.usage = RHI::TextureUsage::RenderTarget;
        motionDesc.mipLevels = 1;
        m_MotionVectorTexture = m_Device->CreateTexture(motionDesc);

        LOG_DEBUG("Created TAA textures");
    }

    void TAASystem::CreateBuffers()
    {
        if (!m_Device) {
            return;
        }

        // TAA parameters uniform buffer
        RHI::BufferDescriptor paramsDesc;
        paramsDesc.size = sizeof(TAAParams);
        paramsDesc.usage = RHI::BufferUsage::Uniform;
        paramsDesc.mapped = true;
        m_TAAParamsBuffer = m_Device->CreateBuffer(paramsDesc);

        // Motion vector UBO
        RHI::BufferDescriptor motionDesc;
        motionDesc.size = sizeof(MotionVectorUBO);
        motionDesc.usage = RHI::BufferUsage::Uniform;
        motionDesc.mapped = true;
        m_MotionVectorUBOBuffer = m_Device->CreateBuffer(motionDesc);

        LOG_DEBUG("Created TAA buffers");
    }

    //=========================================================================
    // Configuration
    //=========================================================================

    void TAASystem::SetConfig(const TAAConfig& config)
    {
        m_Config = config;
        
        if (config.Quality != TAAQuality::High) {
            SetQuality(config.Quality);
        } else {
            // Apply manual settings
            m_Params.BlendFactor = config.BlendFactor;
            m_Params.VarianceClipGamma = config.VarianceClipGamma;
            m_Params.Sharpness = config.Sharpness;
            m_Params.MotionScale = config.MotionScale;
            
            m_Params.Flags = 0;
            if (config.EnableVarianceClipping) m_Params.Flags |= 1;
            if (config.EnableSharpening) m_Params.Flags |= 2;
        }
    }

    void TAASystem::SetQuality(TAAQuality quality)
    {
        m_Config.Quality = quality;
        TAAQualitySettings settings = TAAConfig::GetPresetSettings(quality);

        m_Config.BlendFactor = settings.BlendFactor;
        m_Config.VarianceClipGamma = settings.VarianceClipGamma;
        m_Config.Sharpness = settings.Sharpness;
        m_Config.EnableVarianceClipping = settings.EnableVarianceClipping;
        m_Config.EnableSharpening = settings.EnableSharpening;
        m_Config.Pattern = settings.Pattern;

        m_Params.BlendFactor = settings.BlendFactor;
        m_Params.VarianceClipGamma = settings.VarianceClipGamma;
        m_Params.Sharpness = settings.Sharpness;

        m_Params.Flags = 0;
        if (settings.EnableVarianceClipping) m_Params.Flags |= 1;
        if (settings.EnableSharpening) m_Params.Flags |= 2;
    }

    void TAASystem::SetBlendFactor(float factor)
    {
        m_Config.BlendFactor = std::clamp(factor, 0.0f, 1.0f);
        m_Params.BlendFactor = m_Config.BlendFactor;
    }

    void TAASystem::SetSharpness(float sharpness)
    {
        m_Config.Sharpness = std::clamp(sharpness, 0.0f, 1.0f);
        m_Params.Sharpness = m_Config.Sharpness;
    }

    void TAASystem::SetVarianceClipGamma(float gamma)
    {
        m_Config.VarianceClipGamma = std::clamp(gamma, 0.5f, 3.0f);
        m_Params.VarianceClipGamma = m_Config.VarianceClipGamma;
    }

    void TAASystem::SetJitterPattern(JitterPattern pattern)
    {
        m_Config.Pattern = pattern;
    }

    //=========================================================================
    // Per-Frame Operations
    //=========================================================================

    Math::Vec2 TAASystem::GetJitterOffsetPixels() const
    {
        return m_CurrentJitter * Math::Vec2(static_cast<float>(m_Width), static_cast<float>(m_Height));
    }

    void TAASystem::BeginFrame()
    {
        PROFILE_FUNCTION();

        // Store previous jitter
        m_PreviousJitter = m_CurrentJitter;

        // Calculate new jitter
        m_CurrentJitter = CalculateJitter();

        // Update parameters
        m_Params.JitterOffset = Math::Vec4(
            m_CurrentJitter.x, m_CurrentJitter.y,
            m_PreviousJitter.x, m_PreviousJitter.y
        );
        m_Params.FrameIndex = m_FrameIndex;

        // Update stats
        m_Stats.FrameIndex = m_FrameIndex;
        m_Stats.CurrentJitter = m_CurrentJitter;

        m_FrameIndex++;
    }

    void TAASystem::SetPreviousMatrices(const Math::Mat4& view, const Math::Mat4& projection)
    {
        m_PreviousView = view;
        m_PreviousProjection = projection;
    }

    void TAASystem::SetCurrentMatrices(const Math::Mat4& view, const Math::Mat4& projection)
    {
        m_CurrentView = view;
        m_CurrentProjection = projection;
    }

    Math::Vec2 TAASystem::CalculateJitter()
    {
        if (m_Config.Pattern == JitterPattern::None || !m_Config.Enabled) {
            return Math::Vec2(0.0f);
        }

        uint32_t jitterIndex = m_FrameIndex % TAA_JITTER_SEQUENCE_LENGTH;
        return GenerateJitter(m_Config.Pattern, jitterIndex, m_Width, m_Height);
    }

    //=========================================================================
    // Jitter Generation
    //=========================================================================

    float TAASystem::HaltonSequence(uint32_t index, uint32_t base)
    {
        float result = 0.0f;
        float fraction = 1.0f / static_cast<float>(base);
        uint32_t i = index;

        while (i > 0) {
            result += fraction * static_cast<float>(i % base);
            i /= base;
            fraction /= static_cast<float>(base);
        }

        return result;
    }

    Math::Vec2 TAASystem::Halton23(uint32_t index)
    {
        // Halton sequence with base 2 and 3
        float x = 0.0f;
        float y = 0.0f;
        
        // Base 2
        float fraction = 0.5f;
        uint32_t i = index + 1;
        while (i > 0) {
            x += fraction * static_cast<float>(i & 1);
            i >>= 1;
            fraction *= 0.5f;
        }

        // Base 3
        fraction = 1.0f / 3.0f;
        i = index + 1;
        while (i > 0) {
            y += fraction * static_cast<float>(i % 3);
            i /= 3;
            fraction /= 3.0f;
        }

        // Center around 0 (range [-0.5, 0.5])
        return Math::Vec2(x - 0.5f, y - 0.5f);
    }

    Math::Vec2 TAASystem::R2Sequence(uint32_t index)
    {
        // Roberts R2 sequence - optimal 2D low-discrepancy sequence
        const float g = 1.32471795724474602596f;  // Plastic constant
        const float a1 = 1.0f / g;
        const float a2 = 1.0f / (g * g);

        float x = std::fmod(0.5f + a1 * static_cast<float>(index + 1), 1.0f);
        float y = std::fmod(0.5f + a2 * static_cast<float>(index + 1), 1.0f);

        // Center around 0
        return Math::Vec2(x - 0.5f, y - 0.5f);
    }

    Math::Vec2 TAASystem::Grid4x4(uint32_t index)
    {
        // Simple 4x4 grid pattern
        static const Math::Vec2 offsets[16] = {
            {-0.375f, -0.375f}, {-0.125f, -0.375f}, { 0.125f, -0.375f}, { 0.375f, -0.375f},
            {-0.375f, -0.125f}, {-0.125f, -0.125f}, { 0.125f, -0.125f}, { 0.375f, -0.125f},
            {-0.375f,  0.125f}, {-0.125f,  0.125f}, { 0.125f,  0.125f}, { 0.375f,  0.125f},
            {-0.375f,  0.375f}, {-0.125f,  0.375f}, { 0.125f,  0.375f}, { 0.375f,  0.375f}
        };

        return offsets[index % 16];
    }

    Math::Vec2 TAASystem::GenerateJitter(JitterPattern pattern, uint32_t index, 
                                          uint32_t width, uint32_t height)
    {
        Math::Vec2 jitter;

        switch (pattern) {
            case JitterPattern::Halton23:
                jitter = Halton23(index);
                break;
            case JitterPattern::R2:
                jitter = R2Sequence(index);
                break;
            case JitterPattern::Grid4x4:
                jitter = Grid4x4(index);
                break;
            case JitterPattern::BlueNoise:
                // Fallback to Halton for now
                jitter = Halton23(index);
                break;
            case JitterPattern::None:
            default:
                jitter = Math::Vec2(0.0f);
                break;
        }

        // Convert from pixel offset to NDC offset
        // Divide by resolution to get the offset in clip space
        jitter.x /= static_cast<float>(width);
        jitter.y /= static_cast<float>(height);

        // Scale to clip space (-1 to 1 range, so multiply by 2)
        jitter *= 2.0f;

        return jitter;
    }

    Math::Mat4 TAASystem::ApplyJitterToProjection(const Math::Mat4& projection,
                                                   const Math::Vec2& jitterPixels,
                                                   uint32_t width, uint32_t height)
    {
        Math::Mat4 jitteredProj = projection;

        // Convert pixel jitter to clip space offset
        float jitterX = (2.0f * jitterPixels.x) / static_cast<float>(width);
        float jitterY = (2.0f * jitterPixels.y) / static_cast<float>(height);

        // Apply jitter to projection matrix (translation in clip space)
        jitteredProj[2][0] += jitterX;
        jitteredProj[2][1] += jitterY;

        return jitteredProj;
    }

    void TAASystem::InvalidateHistory()
    {
        m_HistoryValid = false;
        m_PreviousJitter = Math::Vec2(0.0f);
        LOG_DEBUG("TAA history invalidated");
    }

    void TAASystem::SetExternalTemporalUpscalerBackend(const TemporalUpscalerBackend backend, const bool forceHistoryReset)
    {
        if (m_ExternalUpscalerBackend == backend && !forceHistoryReset) {
            return;
        }

        const TemporalUpscalerBackend previousBackend = m_ExternalUpscalerBackend;
        m_ExternalUpscalerBackend = backend;

        if (previousBackend != backend || forceHistoryReset) {
            InvalidateHistory();
            ++m_HistoryResetSerial;
            m_LastHistoryResetReason = "UPSCALE_BACKEND_SWITCH_" + std::string(ToString(previousBackend)) +
                                       "_TO_" + std::string(ToString(backend));
            LOG_INFO("TAA history reset coordinated for backend transition: {} -> {}",
                     ToString(previousBackend),
                     ToString(backend));
        }
    }

    //=========================================================================
    // Render Passes
    //=========================================================================

    void TAASystem::RecordMotionVectorPass(std::shared_ptr<RHI::RHICommandList> /*commandList*/)
    {
        PROFILE_FUNCTION();

        // Update motion vector UBO
        if (m_MotionVectorUBOBuffer) {
            MotionVectorUBO ubo;
            ubo.CurrentModel = Math::Mat4(1.0f);  // Per-object, set during draw
            ubo.CurrentViewProjection = m_CurrentProjection * m_CurrentView;
            ubo.PreviousModel = Math::Mat4(1.0f);
            ubo.PreviousViewProjection = m_PreviousProjection * m_PreviousView;
            ubo.JitterOffset = m_CurrentJitter;
            ubo.PrevJitterOffset = m_PreviousJitter;

            void* data = nullptr;
            m_MotionVectorUBOBuffer->Map(&data);
            if (data) {
                std::memcpy(data, &ubo, sizeof(ubo));
                m_MotionVectorUBOBuffer->Unmap();
            }
        }

        // Motion vector pass would be recorded here
        // This is typically done during the main geometry pass or as a separate pass
    }

    void TAASystem::UpdateParams()
    {
        if (!m_TAAParamsBuffer) {
            return;
        }

        void* data = nullptr;
        m_TAAParamsBuffer->Map(&data);
        if (data) {
            std::memcpy(data, &m_Params, sizeof(m_Params));
            m_TAAParamsBuffer->Unmap();
        }
    }

    void TAASystem::ExecuteResolve(std::shared_ptr<RHI::RHICommandList> commandList,
                                   std::shared_ptr<RHI::RHITexture> /*currentColor*/,
                                   std::shared_ptr<RHI::RHITexture> /*currentDepth*/,
                                   std::shared_ptr<RHI::RHITexture> /*motionVectors*/)
    {
        PROFILE_FUNCTION();

        if (!m_Initialized || !m_Config.Enabled) {
            return;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        // Update parameters buffer
        UpdateParams();

        // Dispatch TAA resolve compute shader
        uint32_t groupsX = (m_Width + TAA_WORKGROUP_SIZE - 1) / TAA_WORKGROUP_SIZE;
        uint32_t groupsY = (m_Height + TAA_WORKGROUP_SIZE - 1) / TAA_WORKGROUP_SIZE;
        
        commandList->Dispatch(groupsX, groupsY, 1);

        // Swap history buffers for next frame
        SwapHistoryBuffers();

        auto endTime = std::chrono::high_resolution_clock::now();
        m_Stats.ResolveTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        m_HistoryValid = true;
    }

    void TAASystem::SwapHistoryBuffers()
    {
        m_HistoryIndex = 1 - m_HistoryIndex;
    }

} // namespace Renderer
} // namespace Core
