// Core/Renderer/RTShadowPass.h

#pragma once
#include "../RHI/RHIRayTracing.h"
#include "../RHI/RHITexture.h"
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace AIEngine::Rendering {

// Forward declarations
class AccelerationStructureBuilder;
struct GBuffer;
struct LightData;

/// Shadow quality preset
enum class RTShadowQuality {
    Off,        ///< Use shadow maps only
    Low,        ///< 1 spp, no denoising
    Medium,     ///< 1 spp, temporal denoising
    High,       ///< 4 spp, spatial + temporal denoising
    Ultra       ///< 8 spp, full denoising
};

/// Per-light shadow settings
struct RTShadowLightSettings {
    bool useRayTracing = true;
    RTShadowQuality quality = RTShadowQuality::Medium;
    float softShadowRadius = 0.1f;  ///< Light source radius for soft shadows
    float maxRayDistance = 1000.0f;
    float shadowBias = 0.001f;
    float normalBias = 0.01f;
    bool castContactShadows = true;
};

/// Shadow ray payload (matches shader)
struct ShadowRayPayload {
    float visibility = 1.0f;
    float hitDistance = 0.0f;
};

/// Shadow pass configuration
struct RTShadowConfig {
    RTShadowQuality globalQuality = RTShadowQuality::Medium;
    bool enableDenoising = true;
    bool enableTemporalAccumulation = true;
    float temporalBlendFactor = 0.1f;
    uint32_t denoiserSpatialRadius = 3;
    float denoiserSigmaColor = 1.0f;
    float denoiserSigmaNormal = 0.5f;
    float denoiserSigmaDepth = 0.1f;
    float resolutionScale = 1.0f;
    bool useCheckerboardRendering = false;
    uint32_t maxLightsWithRTShadows = 4;
};

/// Ray-traced shadow pass
class RTShadowPass {
public:
    RTShadowPass(RHI::RHIDeviceRT* device, AccelerationStructureBuilder* asBuilder);
    ~RTShadowPass();
    
    /// Initialize shadow pass resources
    bool Initialize(uint32_t width, uint32_t height);
    
    /// Shutdown and cleanup
    void Shutdown();
    
    /// Resize shadow textures
    void Resize(uint32_t width, uint32_t height);
    
    /// Render shadows for all lights
    void Render(RHI::RHICommandBufferRT* cmdBuffer,
                const GBuffer& gbuffer,
                const std::vector<LightData>& lights,
                const glm::mat4& viewMatrix,
                const glm::mat4& projMatrix,
                const glm::vec3& cameraPos);
    
    /// Get shadow mask texture (single channel, 1.0 = lit, 0.0 = shadow)
    RHI::RHITexture* GetShadowMask() const { return m_ShadowMask.get(); }
    
    /// Get per-light shadow textures (for debugging)
    RHI::RHITexture* GetLightShadowTexture(uint32_t lightIndex) const;
    
    /// Get denoised shadow output
    RHI::RHITexture* GetDenoisedShadow() const { return m_DenoisedShadow.get(); }
    
    /// Set global configuration
    void SetConfig(const RTShadowConfig& config);
    const RTShadowConfig& GetConfig() const { return m_Config; }
    
    /// Set global quality
    void SetQuality(RTShadowQuality quality);
    RTShadowQuality GetQuality() const { return m_Config.globalQuality; }
    
    /// Set per-light settings
    void SetLightSettings(uint32_t lightIndex, const RTShadowLightSettings& settings);
    const RTShadowLightSettings& GetLightSettings(uint32_t lightIndex) const;
    
    /// Enable/disable denoising
    void SetDenoisingEnabled(bool enabled) { m_Config.enableDenoising = enabled; }
    bool IsDenoisingEnabled() const { return m_Config.enableDenoising; }
    
    /// Set resolution scale (0.5 = half res, 1.0 = full res)
    void SetResolutionScale(float scale);
    float GetResolutionScale() const { return m_Config.resolutionScale; }
    
    /// Get statistics
    struct Stats {
        uint32_t lightsRendered = 0;
        uint32_t raysTraced = 0;
        float renderTimeMs = 0.0f;
        float denoiseTimeMs = 0.0f;
        bool usedFallback = false;
    };
    Stats GetStats() const { return m_Stats; }
    
    /// Check if RT shadows are available
    bool IsAvailable() const { return m_IsAvailable; }
    
    /// Get current frame index (for temporal effects)
    uint32_t GetFrameIndex() const { return m_FrameIndex; }
    
    /// Get samples per pixel for current quality
    static uint32_t GetSamplesForQuality(RTShadowQuality quality);
    
private:
    void CreatePipeline();
    void CreateSBT();
    void CreateDescriptorSets();
    void UpdateDescriptors(const GBuffer& gbuffer);
    void RenderLight(RHI::RHICommandBufferRT* cmdBuffer,
                     const LightData& light,
                     uint32_t lightIndex);
    void DenoiseOutput(RHI::RHICommandBufferRT* cmdBuffer);
    void TemporalAccumulate(RHI::RHICommandBufferRT* cmdBuffer);
    void CombineShadowMasks(RHI::RHICommandBufferRT* cmdBuffer);
    
    uint32_t CalculateInternalWidth() const;
    uint32_t CalculateInternalHeight() const;
    
    RHI::RHIDeviceRT* m_Device = nullptr;
    AccelerationStructureBuilder* m_ASBuilder = nullptr;
    
    // Pipeline
    std::unique_ptr<RHI::RTPipeline> m_Pipeline;
    std::unique_ptr<RHI::ShaderBindingTable> m_SBT;
    
    // Textures
    std::unique_ptr<RHI::RHITexture> m_ShadowMask;
    std::unique_ptr<RHI::RHITexture> m_ShadowRaw;
    std::unique_ptr<RHI::RHITexture> m_ShadowHistory;
    std::unique_ptr<RHI::RHITexture> m_DenoisedShadow;
    std::vector<std::unique_ptr<RHI::RHITexture>> m_PerLightShadows;
    
    // Denoiser resources
    std::unique_ptr<RHI::RHITexture> m_DenoiserTemp;
    std::unique_ptr<RHI::RHITexture> m_VarianceTexture;
    
    // Configuration
    RTShadowConfig m_Config;
    std::vector<RTShadowLightSettings> m_LightSettings;
    
    // State
    bool m_IsAvailable = false;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_FrameIndex = 0;
    
    Stats m_Stats;
    
    // Push constants for shader
    struct PushConstants {
        glm::mat4 invViewProj;
        glm::vec4 cameraPos;
        glm::vec4 lightPosType;     // xyz = pos/dir, w = type
        glm::vec4 lightColorRange;  // rgb = color, a = range
        glm::vec4 lightParams;      // x = softRadius, y = bias, z = normalBias, w = maxDist
        uint32_t frameIndex;
        uint32_t samplesPerPixel;
        uint32_t lightIndex;
        uint32_t padding;
    };
};

/// Light types for shadow casting
enum class ShadowLightType : uint32_t {
    Directional = 0,
    Point = 1,
    Spot = 2
};

/// Light data structure (for shadow pass)
struct LightData {
    glm::vec3 position = glm::vec3(0.0f);
    ShadowLightType type = ShadowLightType::Directional;
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    float range = 100.0f;
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float spotInnerAngle = 30.0f;
    float spotOuterAngle = 45.0f;
    bool castShadows = true;
    bool useRTShadows = true;
};

} // namespace AIEngine::Rendering
