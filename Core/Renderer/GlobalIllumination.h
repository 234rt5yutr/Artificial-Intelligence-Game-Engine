// Core/Renderer/GlobalIllumination.h

#pragma once
#include "../RHI/RHIRayTracing.h"
#include "../RHI/RHITexture.h"
#include <entt/entt.hpp>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace AIEngine::Rendering {

// Forward declarations
class AccelerationStructureBuilder;
struct GBuffer;
struct LightData;

/// GI technique selection
enum class GITechnique {
    None,               ///< No indirect lighting
    ScreenSpace,        ///< SSGI (fast, limited)
    VoxelConeTracing,   ///< VXGI (medium, full coverage)
    RayTraced,          ///< RTGI (highest quality, RT required)
    IrradianceProbes    ///< Baked + dynamic (balanced)
};

/// GI quality preset
enum class GIQuality {
    Off,
    Low,        ///< 1 bounce, low resolution
    Medium,     ///< 1 bounce, medium resolution
    High,       ///< 2 bounces, high resolution
    Ultra       ///< 3 bounces, full resolution
};

/// Irradiance probe data
struct IrradianceProbe {
    glm::vec3 position = glm::vec3(0.0f);
    float radius = 10.0f;
    glm::vec3 irradiance[6] = {};  ///< Cube directions (+X,-X,+Y,-Y,+Z,-Z)
    float ambient = 0.0f;
    bool isDynamic = false;
    bool needsUpdate = true;
    uint32_t updatePriority = 0;
    uint64_t lastUpdateFrame = 0;
};

/// Irradiance probe volume
struct IrradianceVolume {
    glm::vec3 minBounds = glm::vec3(0.0f);
    glm::vec3 maxBounds = glm::vec3(10.0f);
    glm::ivec3 resolution = glm::ivec3(8, 4, 8);
    std::vector<IrradianceProbe> probes;
    
    std::unique_ptr<RHI::RHITexture> probeTexture;
    std::unique_ptr<RHI::RHITexture> irradianceAtlas;
    std::unique_ptr<RHI::RHITexture> depthAtlas;
    
    bool isBuilt = false;
    bool needsRebuild = false;
};

/// GI configuration
struct GIConfig {
    GITechnique technique = GITechnique::ScreenSpace;
    GIQuality quality = GIQuality::Medium;
    
    // SSGI settings
    float ssgiRadius = 5.0f;
    uint32_t ssgiSamples = 8;
    float ssgiIntensity = 1.0f;
    float ssgiFalloff = 1.0f;
    
    // VXGI settings
    uint32_t voxelResolution = 128;
    float voxelScale = 0.25f;
    uint32_t vxgiConeCount = 6;
    float vxgiConeAngle = 0.5f;
    uint32_t vxgiMaxSteps = 64;
    
    // RTGI settings
    uint32_t rtgiBounces = 1;
    uint32_t rtgiSamplesPerPixel = 1;
    float rtgiMaxDistance = 100.0f;
    float rtgiRussianRouletteDepth = 2;
    
    // Probe settings
    uint32_t probeRaysPerUpdate = 256;
    float probeHysteresis = 0.98f;
    float probeNormalBias = 0.25f;
    float probeViewBias = 0.3f;
    
    // Common
    float indirectIntensity = 1.0f;
    bool enableEmissiveContribution = true;
    float emissiveBoost = 1.0f;
    bool enableTemporalFilter = true;
    float temporalBlendFactor = 0.1f;
};

/// Baking progress callback
using BakeProgressCallback = std::function<void(float progress, const std::string& status)>;

/// Global illumination manager
class GlobalIllumination {
public:
    GlobalIllumination(RHI::RHIDeviceRT* device,
                       AccelerationStructureBuilder* asBuilder);
    ~GlobalIllumination();
    
    /// Initialize GI resources
    bool Initialize(uint32_t width, uint32_t height);
    
    /// Shutdown and cleanup
    void Shutdown();
    
    /// Resize GI textures
    void Resize(uint32_t width, uint32_t height);
    
    /// Compute indirect lighting
    void Compute(RHI::RHICommandBufferRT* cmdBuffer,
                 const GBuffer& gbuffer,
                 const std::vector<LightData>& lights,
                 const glm::mat4& viewMatrix,
                 const glm::mat4& projMatrix,
                 const glm::vec3& cameraPos);
    
    /// Get indirect lighting texture
    RHI::RHITexture* GetIndirectLighting() const { return m_IndirectLighting.get(); }
    
    /// Get ambient occlusion texture (from SSGI)
    RHI::RHITexture* GetAmbientOcclusion() const { return m_AmbientOcclusion.get(); }
    
    /// Set GI configuration
    void SetConfig(const GIConfig& config);
    const GIConfig& GetConfig() const { return m_Config; }
    
    /// Set technique (may trigger resource reallocation)
    void SetTechnique(GITechnique technique);
    GITechnique GetTechnique() const { return m_Config.technique; }
    
    /// Set quality preset
    void SetQuality(GIQuality quality);
    GIQuality GetQuality() const { return m_Config.quality; }
    
    /// Add irradiance volume
    uint32_t AddIrradianceVolume(const glm::vec3& minBounds,
                                  const glm::vec3& maxBounds,
                                  const glm::ivec3& resolution);
    
    /// Remove irradiance volume
    void RemoveIrradianceVolume(uint32_t volumeIndex);
    
    /// Get irradiance volume
    IrradianceVolume* GetIrradianceVolume(uint32_t index);
    
    /// Bake irradiance probes (synchronous)
    void BakeIrradianceProbes(RHI::RHICommandBufferRT* cmdBuffer,
                               entt::registry& registry);
    
    /// Start async probe baking
    void StartAsyncBake(entt::registry& registry,
                        BakeProgressCallback callback = nullptr);
    
    /// Update async bake (call each frame)
    bool UpdateAsyncBake(RHI::RHICommandBufferRT* cmdBuffer);
    
    /// Cancel async bake
    void CancelAsyncBake();
    
    /// Is async bake in progress?
    bool IsAsyncBakeInProgress() const { return m_AsyncBakeInProgress; }
    
    /// Update dynamic probes
    void UpdateDynamicProbes(RHI::RHICommandBufferRT* cmdBuffer);
    
    /// Get voxel grid texture (for VXGI)
    RHI::RHITexture* GetVoxelGrid() const { return m_VoxelGrid.get(); }
    
    /// Get voxel radiance texture
    RHI::RHITexture* GetVoxelRadiance() const { return m_VoxelRadiance.get(); }
    
    /// Set scene emissives for GI contribution
    void SetEmissiveTexture(RHI::RHITexture* emissive) { m_EmissiveTexture = emissive; }
    
    /// Get statistics
    struct Stats {
        float computeTimeMs = 0.0f;
        uint32_t raysTraced = 0;
        uint32_t probesUpdated = 0;
        uint32_t voxelsUpdated = 0;
        float asyncBakeProgress = 0.0f;
        GITechnique activeTechnique = GITechnique::None;
    };
    Stats GetStats() const { return m_Stats; }
    
    /// Check if GI is available
    bool IsAvailable() const { return m_IsAvailable; }
    
    /// Check if specific technique is available
    bool IsTechniqueAvailable(GITechnique technique) const;
    
private:
    void ComputeSSGI(RHI::RHICommandBufferRT* cmdBuffer,
                     const GBuffer& gbuffer);
    void ComputeVXGI(RHI::RHICommandBufferRT* cmdBuffer,
                     const GBuffer& gbuffer,
                     const std::vector<LightData>& lights);
    void ComputeRTGI(RHI::RHICommandBufferRT* cmdBuffer,
                     const GBuffer& gbuffer);
    void ComputeProbeGI(RHI::RHICommandBufferRT* cmdBuffer,
                        const GBuffer& gbuffer);
    
    void VoxelizeScene(RHI::RHICommandBufferRT* cmdBuffer,
                       const std::vector<LightData>& lights);
    void InjectRadiance(RHI::RHICommandBufferRT* cmdBuffer,
                        const std::vector<LightData>& lights);
    void PropagateRadiance(RHI::RHICommandBufferRT* cmdBuffer);
    void TraceVoxelCones(RHI::RHICommandBufferRT* cmdBuffer,
                         const GBuffer& gbuffer);
    
    void CreateSSGIResources();
    void CreateVXGIResources();
    void CreateRTGIResources();
    void CreateProbeResources();
    void ReleaseResources();
    
    void ApplyQualityPreset(GIQuality quality);
    
    RHI::RHIDeviceRT* m_Device = nullptr;
    AccelerationStructureBuilder* m_ASBuilder = nullptr;
    
    GIConfig m_Config;
    
    // Output
    std::unique_ptr<RHI::RHITexture> m_IndirectLighting;
    std::unique_ptr<RHI::RHITexture> m_IndirectHistory;
    std::unique_ptr<RHI::RHITexture> m_AmbientOcclusion;
    
    // SSGI resources
    std::unique_ptr<RHI::RHIPipeline> m_SSGIPipeline;
    std::unique_ptr<RHI::RHITexture> m_SSGITemp;
    
    // VXGI resources
    std::unique_ptr<RHI::RHITexture> m_VoxelGrid;
    std::unique_ptr<RHI::RHITexture> m_VoxelRadiance;
    std::unique_ptr<RHI::RHITexture> m_VoxelNormal;
    std::unique_ptr<RHI::RHIPipeline> m_VoxelizePipeline;
    std::unique_ptr<RHI::RHIPipeline> m_VoxelInjectPipeline;
    std::unique_ptr<RHI::RHIPipeline> m_VoxelConePipeline;
    
    // RTGI resources
    std::unique_ptr<RHI::RTPipeline> m_RTGIPipeline;
    std::unique_ptr<RHI::ShaderBindingTable> m_RTGISBT;
    
    // Probe resources
    std::vector<IrradianceVolume> m_IrradianceVolumes;
    std::unique_ptr<RHI::RHIPipeline> m_ProbeSamplePipeline;
    std::unique_ptr<RHI::RHIPipeline> m_ProbeUpdatePipeline;
    
    // External textures
    RHI::RHITexture* m_EmissiveTexture = nullptr;
    
    // State
    bool m_IsAvailable = false;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_FrameIndex = 0;
    
    // Async bake state
    bool m_AsyncBakeInProgress = false;
    uint32_t m_AsyncBakeCurrentVolume = 0;
    uint32_t m_AsyncBakeCurrentProbe = 0;
    BakeProgressCallback m_BakeCallback;
    
    Stats m_Stats;
    
    // Previous matrices for temporal
    glm::mat4 m_PrevViewProj = glm::mat4(1.0f);
};

} // namespace AIEngine::Rendering
