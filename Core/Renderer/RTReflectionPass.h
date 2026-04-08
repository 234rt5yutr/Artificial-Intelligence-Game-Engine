// Core/Renderer/RTReflectionPass.h

#pragma once
#include "../RHI/RHIRayTracing.h"
#include "../RHI/RHITexture.h"
#include <memory>
#include <glm/glm.hpp>

namespace AIEngine::Rendering {

// Forward declarations
class AccelerationStructureBuilder;
struct GBuffer;

/// Reflection quality preset
enum class RTReflectionQuality {
    Off,            ///< SSR + cubemap only
    Low,            ///< 0.5x resolution, 1 bounce
    Medium,         ///< 0.75x resolution, 1 bounce, denoising
    High,           ///< Full resolution, 2 bounces, denoising
    Ultra           ///< Full resolution, 3 bounces, full denoising
};

/// Reflection fallback mode
enum class ReflectionFallback {
    None,           ///< No fallback, black if miss
    ScreenSpace,    ///< SSR for screen-visible geometry
    Cubemap,        ///< Environment cubemap
    Hybrid          ///< SSR first, then cubemap
};

/// Reflection pass configuration
struct RTReflectionConfig {
    RTReflectionQuality quality = RTReflectionQuality::Medium;
    uint32_t maxBounces = 1;
    float roughnessThreshold = 0.5f;    ///< Above this, use fallback
    float metallicThreshold = 0.1f;     ///< Below this, skip reflections
    float maxRayDistance = 500.0f;
    bool enableSSRFallback = true;
    bool enableCubemapFallback = true;
    float resolutionScale = 1.0f;
    uint32_t samplesPerPixel = 1;
    
    // Denoiser settings
    bool enableDenoising = true;
    bool enableTemporalAccumulation = true;
    float temporalBlendFactor = 0.1f;
    uint32_t denoiserIterations = 3;
    float denoiserColorSigma = 1.0f;
    
    // Performance
    bool useHalfResTrace = false;
    bool useStochasticRoughness = true;
    float fireflySuppression = 10.0f;
};

/// Per-material reflection override
struct MaterialReflectionOverride {
    bool hasOverride = false;
    RTReflectionQuality quality = RTReflectionQuality::Medium;
    uint32_t maxBounces = 1;
    float roughnessOverride = -1.0f;  ///< Negative = use material value
};

/// Reflection ray payload (matches shader)
struct ReflectionRayPayload {
    glm::vec3 color = glm::vec3(0.0f);
    float hitDistance = -1.0f;
    glm::vec3 normal = glm::vec3(0.0f);
    float roughness = 0.0f;
    uint32_t bounceCount = 0;
    bool hitSky = false;
};

/// Ray-traced reflection pass
class RTReflectionPass {
public:
    RTReflectionPass(RHI::RHIDeviceRT* device, 
                     AccelerationStructureBuilder* asBuilder);
    ~RTReflectionPass();
    
    /// Initialize reflection pass resources
    bool Initialize(uint32_t width, uint32_t height);
    
    /// Shutdown and cleanup
    void Shutdown();
    
    /// Resize reflection textures
    void Resize(uint32_t width, uint32_t height);
    
    /// Render reflections
    void Render(RHI::RHICommandBufferRT* cmdBuffer,
                const GBuffer& gbuffer,
                RHI::RHITexture* colorBuffer,
                RHI::RHITexture* depthBuffer,
                const glm::mat4& viewMatrix,
                const glm::mat4& projMatrix,
                const glm::vec3& cameraPos);
    
    /// Get reflection color texture
    RHI::RHITexture* GetReflectionColor() const { return m_ReflectionColor.get(); }
    
    /// Get raw reflection (before denoising)
    RHI::RHITexture* GetReflectionRaw() const { return m_ReflectionRaw.get(); }
    
    /// Get reflection direction texture (for debugging)
    RHI::RHITexture* GetReflectionDirection() const { return m_ReflectionDirection.get(); }
    
    /// Get hit distance texture (for spatial filtering)
    RHI::RHITexture* GetHitDistance() const { return m_HitDistance.get(); }
    
    /// Set configuration
    void SetConfig(const RTReflectionConfig& config);
    const RTReflectionConfig& GetConfig() const { return m_Config; }
    
    /// Set quality preset
    void SetQuality(RTReflectionQuality quality);
    RTReflectionQuality GetQuality() const { return m_Config.quality; }
    
    /// Set environment cubemap for fallback
    void SetEnvironmentCubemap(RHI::RHITexture* cubemap) { 
        m_EnvironmentCubemap = cubemap; 
    }
    
    /// Set SSR texture for fallback blending
    void SetSSRTexture(RHI::RHITexture* ssr) {
        m_SSRTexture = ssr;
    }
    
    /// Set material reflection override
    void SetMaterialOverride(uint32_t materialId, const MaterialReflectionOverride& override);
    void ClearMaterialOverride(uint32_t materialId);
    void ClearAllMaterialOverrides();
    
    /// Get statistics
    struct Stats {
        uint32_t pixelsProcessed = 0;
        uint32_t raysTraced = 0;
        uint32_t rayBounces = 0;
        uint32_t skyHits = 0;
        uint32_t fallbackPixels = 0;
        float renderTimeMs = 0.0f;
        float denoiseTimeMs = 0.0f;
    };
    Stats GetStats() const { return m_Stats; }
    
    /// Check if RT reflections are available
    bool IsAvailable() const { return m_IsAvailable; }
    
    /// Get samples per pixel for quality level
    static uint32_t GetSamplesForQuality(RTReflectionQuality quality);
    
    /// Get max bounces for quality level
    static uint32_t GetBouncesForQuality(RTReflectionQuality quality);
    
    /// Get resolution scale for quality level
    static float GetResolutionScaleForQuality(RTReflectionQuality quality);
    
private:
    void CreatePipeline();
    void CreateSBT();
    void CreateDescriptorSets();
    void UpdateDescriptors(const GBuffer& gbuffer,
                           RHI::RHITexture* colorBuffer,
                           RHI::RHITexture* depthBuffer);
    void TraceReflections(RHI::RHICommandBufferRT* cmdBuffer);
    void DenoiseOutput(RHI::RHICommandBufferRT* cmdBuffer);
    void CompositeWithFallbacks(RHI::RHICommandBufferRT* cmdBuffer);
    void TemporalFilter(RHI::RHICommandBufferRT* cmdBuffer);
    
    uint32_t CalculateInternalWidth() const;
    uint32_t CalculateInternalHeight() const;
    
    RHI::RHIDeviceRT* m_Device = nullptr;
    AccelerationStructureBuilder* m_ASBuilder = nullptr;
    
    // Pipeline
    std::unique_ptr<RHI::RTPipeline> m_Pipeline;
    std::unique_ptr<RHI::ShaderBindingTable> m_SBT;
    
    // Output textures
    std::unique_ptr<RHI::RHITexture> m_ReflectionColor;
    std::unique_ptr<RHI::RHITexture> m_ReflectionRaw;
    std::unique_ptr<RHI::RHITexture> m_ReflectionHistory;
    std::unique_ptr<RHI::RHITexture> m_ReflectionDirection;
    std::unique_ptr<RHI::RHITexture> m_HitDistance;
    
    // Denoiser textures
    std::unique_ptr<RHI::RHITexture> m_DenoiserTemp;
    std::unique_ptr<RHI::RHITexture> m_VarianceBuffer;
    
    // Fallback textures (external)
    RHI::RHITexture* m_EnvironmentCubemap = nullptr;
    RHI::RHITexture* m_SSRTexture = nullptr;
    
    // Configuration
    RTReflectionConfig m_Config;
    std::unordered_map<uint32_t, MaterialReflectionOverride> m_MaterialOverrides;
    
    // State
    bool m_IsAvailable = false;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_FrameIndex = 0;
    
    Stats m_Stats;
    
    // Push constants for shader
    struct PushConstants {
        glm::mat4 invViewProj;
        glm::mat4 prevViewProj;
        glm::vec4 cameraPos;
        glm::vec4 cameraRight;
        glm::vec4 cameraUp;
        float roughnessThreshold;
        float metallicThreshold;
        float maxRayDistance;
        uint32_t maxBounces;
        uint32_t samplesPerPixel;
        uint32_t frameIndex;
        float fireflySuppression;
        uint32_t flags;  // Packed boolean flags
    };
    
    glm::mat4 m_PrevViewProj = glm::mat4(1.0f);
};

} // namespace AIEngine::Rendering
