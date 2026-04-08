// Core/MCP/MCPRayTracingTools.h

#pragma once
#include "MCPTool.h"
#include "../Renderer/RayTracingManager.h"
#include "../Renderer/RTShadowPass.h"
#include "../Renderer/RTReflectionPass.h"
#include "../Renderer/GlobalIllumination.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <vector>

namespace Core::MCP {

using json = nlohmann::json;

// Forward declarations
namespace AIEngine::Rendering {
    class RayTracingManager;
    class RTShadowPass;
    class RTReflectionPass;
    class GlobalIllumination;
}

/// Tool to toggle ray tracing features
class ToggleRayTracingFeaturesTool : public MCPTool {
public:
    std::string GetName() const override { return "ToggleRayTracingFeatures"; }
    
    std::string GetDescription() const override {
        return "Enable, disable, or adjust ray tracing features for dynamic graphics quality. "
               "Use to switch between RT shadows, reflections, and GI, or adjust quality settings "
               "for performance tuning and mood transitions.";
    }
    
    json GetInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"feature", {
                    {"type", "string"},
                    {"enum", {"shadows", "reflections", "gi", "ao", "all"}},
                    {"description", "Ray tracing feature to control"}
                }},
                {"enabled", {
                    {"type", "boolean"},
                    {"description", "Whether to enable the feature"}
                }},
                {"quality", {
                    {"type", "string"},
                    {"enum", {"off", "low", "medium", "high", "ultra"}},
                    {"description", "Quality preset to apply"}
                }},
                {"shadowSoftness", {
                    {"type", "number"},
                    {"description", "Soft shadow radius (0.0-1.0)"},
                    {"minimum", 0.0},
                    {"maximum", 1.0}
                }},
                {"reflectionBounces", {
                    {"type", "integer"},
                    {"description", "Number of reflection bounces (1-3)"},
                    {"minimum", 1},
                    {"maximum", 3}
                }},
                {"giIntensity", {
                    {"type", "number"},
                    {"description", "Global illumination intensity (0.0-5.0)"},
                    {"minimum", 0.0},
                    {"maximum", 5.0}
                }},
                {"giTechnique", {
                    {"type", "string"},
                    {"enum", {"none", "screenspace", "voxel", "raytraced", "probes"}},
                    {"description", "GI technique to use"}
                }}
            }},
            {"required", {"feature"}}
        };
    }
    
    json Execute(const json& params, Scene* scene) override {
        std::string feature = params.value("feature", "all");
        
        json result;
        result["success"] = true;
        result["changedFeatures"] = json::array();
        
        // Handle enabled/disabled
        if (params.contains("enabled")) {
            bool enabled = params["enabled"].get<bool>();
            
            if (feature == "shadows" || feature == "all") {
                if (m_ShadowPass) {
                    m_ShadowPass->SetQuality(enabled ? 
                        AIEngine::Rendering::RTShadowQuality::Medium :
                        AIEngine::Rendering::RTShadowQuality::Off);
                    result["changedFeatures"].push_back({
                        {"feature", "shadows"},
                        {"enabled", enabled}
                    });
                }
            }
            
            if (feature == "reflections" || feature == "all") {
                if (m_ReflectionPass) {
                    m_ReflectionPass->SetQuality(enabled ?
                        AIEngine::Rendering::RTReflectionQuality::Medium :
                        AIEngine::Rendering::RTReflectionQuality::Off);
                    result["changedFeatures"].push_back({
                        {"feature", "reflections"},
                        {"enabled", enabled}
                    });
                }
            }
            
            if (feature == "gi" || feature == "all") {
                if (m_GI) {
                    m_GI->SetQuality(enabled ?
                        AIEngine::Rendering::GIQuality::Medium :
                        AIEngine::Rendering::GIQuality::Off);
                    result["changedFeatures"].push_back({
                        {"feature", "gi"},
                        {"enabled", enabled}
                    });
                }
            }
        }
        
        // Handle quality presets
        if (params.contains("quality")) {
            std::string quality = params["quality"].get<std::string>();
            
            if (feature == "shadows" && m_ShadowPass) {
                m_ShadowPass->SetQuality(StringToShadowQuality(quality));
                result["shadowQuality"] = quality;
            }
            if (feature == "reflections" && m_ReflectionPass) {
                m_ReflectionPass->SetQuality(StringToReflectionQuality(quality));
                result["reflectionQuality"] = quality;
            }
            if (feature == "gi" && m_GI) {
                m_GI->SetQuality(StringToGIQuality(quality));
                result["giQuality"] = quality;
            }
        }
        
        // Handle specific settings
        if (params.contains("shadowSoftness") && m_ShadowPass) {
            float softness = params["shadowSoftness"].get<float>();
            auto config = m_ShadowPass->GetConfig();
            // Would need to update per-light settings
            result["shadowSoftness"] = softness;
        }
        
        if (params.contains("reflectionBounces") && m_ReflectionPass) {
            uint32_t bounces = params["reflectionBounces"].get<uint32_t>();
            auto config = m_ReflectionPass->GetConfig();
            config.maxBounces = bounces;
            m_ReflectionPass->SetConfig(config);
            result["reflectionBounces"] = bounces;
        }
        
        if (params.contains("giIntensity") && m_GI) {
            float intensity = params["giIntensity"].get<float>();
            auto config = m_GI->GetConfig();
            config.indirectIntensity = intensity;
            m_GI->SetConfig(config);
            result["giIntensity"] = intensity;
        }
        
        if (params.contains("giTechnique") && m_GI) {
            std::string technique = params["giTechnique"].get<std::string>();
            m_GI->SetTechnique(StringToGITechnique(technique));
            result["giTechnique"] = technique;
        }
        
        return result;
    }
    
    void SetRTShadowPass(AIEngine::Rendering::RTShadowPass* pass) { m_ShadowPass = pass; }
    void SetRTReflectionPass(AIEngine::Rendering::RTReflectionPass* pass) { m_ReflectionPass = pass; }
    void SetGlobalIllumination(AIEngine::Rendering::GlobalIllumination* gi) { m_GI = gi; }
    
private:
    static AIEngine::Rendering::RTShadowQuality StringToShadowQuality(const std::string& s) {
        if (s == "off") return AIEngine::Rendering::RTShadowQuality::Off;
        if (s == "low") return AIEngine::Rendering::RTShadowQuality::Low;
        if (s == "medium") return AIEngine::Rendering::RTShadowQuality::Medium;
        if (s == "high") return AIEngine::Rendering::RTShadowQuality::High;
        if (s == "ultra") return AIEngine::Rendering::RTShadowQuality::Ultra;
        return AIEngine::Rendering::RTShadowQuality::Medium;
    }
    
    static AIEngine::Rendering::RTReflectionQuality StringToReflectionQuality(const std::string& s) {
        if (s == "off") return AIEngine::Rendering::RTReflectionQuality::Off;
        if (s == "low") return AIEngine::Rendering::RTReflectionQuality::Low;
        if (s == "medium") return AIEngine::Rendering::RTReflectionQuality::Medium;
        if (s == "high") return AIEngine::Rendering::RTReflectionQuality::High;
        if (s == "ultra") return AIEngine::Rendering::RTReflectionQuality::Ultra;
        return AIEngine::Rendering::RTReflectionQuality::Medium;
    }
    
    static AIEngine::Rendering::GIQuality StringToGIQuality(const std::string& s) {
        if (s == "off") return AIEngine::Rendering::GIQuality::Off;
        if (s == "low") return AIEngine::Rendering::GIQuality::Low;
        if (s == "medium") return AIEngine::Rendering::GIQuality::Medium;
        if (s == "high") return AIEngine::Rendering::GIQuality::High;
        if (s == "ultra") return AIEngine::Rendering::GIQuality::Ultra;
        return AIEngine::Rendering::GIQuality::Medium;
    }
    
    static AIEngine::Rendering::GITechnique StringToGITechnique(const std::string& s) {
        if (s == "none") return AIEngine::Rendering::GITechnique::None;
        if (s == "screenspace") return AIEngine::Rendering::GITechnique::ScreenSpace;
        if (s == "voxel") return AIEngine::Rendering::GITechnique::VoxelConeTracing;
        if (s == "raytraced") return AIEngine::Rendering::GITechnique::RayTraced;
        if (s == "probes") return AIEngine::Rendering::GITechnique::IrradianceProbes;
        return AIEngine::Rendering::GITechnique::ScreenSpace;
    }
    
    AIEngine::Rendering::RTShadowPass* m_ShadowPass = nullptr;
    AIEngine::Rendering::RTReflectionPass* m_ReflectionPass = nullptr;
    AIEngine::Rendering::GlobalIllumination* m_GI = nullptr;
};

/// Tool to bake global illumination
class BakeGlobalIlluminationTool : public MCPTool {
public:
    std::string GetName() const override { return "BakeGlobalIllumination"; }
    
    std::string GetDescription() const override {
        return "Bake global illumination probes for static or semi-static lighting. "
               "Can transform scene mood by pre-computing indirect lighting.";
    }
    
    json GetInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"volumeBounds", {
                    {"type", "object"},
                    {"properties", {
                        {"minX", {{"type", "number"}}},
                        {"minY", {{"type", "number"}}},
                        {"minZ", {{"type", "number"}}},
                        {"maxX", {{"type", "number"}}},
                        {"maxY", {{"type", "number"}}},
                        {"maxZ", {{"type", "number"}}}
                    }},
                    {"description", "Bounding volume for probe placement"}
                }},
                {"resolution", {
                    {"type", "object"},
                    {"properties", {
                        {"x", {{"type", "integer"}, {"minimum", 2}, {"maximum", 64}}},
                        {"y", {{"type", "integer"}, {"minimum", 2}, {"maximum", 64}}},
                        {"z", {{"type", "integer"}, {"minimum", 2}, {"maximum", 64}}}
                    }},
                    {"description", "Probe grid resolution"}
                }},
                {"bounces", {
                    {"type", "integer"},
                    {"description", "Number of light bounces to simulate"},
                    {"minimum", 1},
                    {"maximum", 5},
                    {"default", 2}
                }},
                {"samplesPerProbe", {
                    {"type", "integer"},
                    {"description", "Samples per probe for baking"},
                    {"minimum", 64},
                    {"maximum", 4096},
                    {"default", 256}
                }},
                {"async", {
                    {"type", "boolean"},
                    {"description", "Bake asynchronously over multiple frames"},
                    {"default", true}
                }}
            }}
        };
    }
    
    json Execute(const json& params, Scene* scene) override {
        if (!m_GI) {
            return {{"success", false}, {"error", "GlobalIllumination not available"}};
        }
        
        json result;
        result["success"] = true;
        
        // Create volume if bounds specified
        if (params.contains("volumeBounds")) {
            auto bounds = params["volumeBounds"];
            glm::vec3 minBounds(
                bounds.value("minX", 0.0f),
                bounds.value("minY", 0.0f),
                bounds.value("minZ", 0.0f)
            );
            glm::vec3 maxBounds(
                bounds.value("maxX", 10.0f),
                bounds.value("maxY", 10.0f),
                bounds.value("maxZ", 10.0f)
            );
            
            glm::ivec3 resolution(8, 4, 8);
            if (params.contains("resolution")) {
                auto res = params["resolution"];
                resolution = glm::ivec3(
                    res.value("x", 8),
                    res.value("y", 4),
                    res.value("z", 8)
                );
            }
            
            uint32_t volumeId = m_GI->AddIrradianceVolume(minBounds, maxBounds, resolution);
            result["volumeId"] = volumeId;
            result["probeCount"] = resolution.x * resolution.y * resolution.z;
        }
        
        bool async = params.value("async", true);
        
        if (async) {
            m_GI->StartAsyncBake(*m_Registry, [&result](float progress, const std::string& status) {
                // Progress callback
            });
            result["status"] = "baking_started";
            result["async"] = true;
        } else {
            // Synchronous bake would need command buffer access
            result["status"] = "synchronous_bake_requires_render_context";
            result["async"] = false;
        }
        
        return result;
    }
    
    void SetGlobalIllumination(AIEngine::Rendering::GlobalIllumination* gi) { m_GI = gi; }
    void SetRegistry(entt::registry* registry) { m_Registry = registry; }
    
private:
    AIEngine::Rendering::GlobalIllumination* m_GI = nullptr;
    entt::registry* m_Registry = nullptr;
};

/// Tool to set reflection quality per material/surface
class SetReflectionQualityTool : public MCPTool {
public:
    std::string GetName() const override { return "SetReflectionQuality"; }
    
    std::string GetDescription() const override {
        return "Set reflection quality for specific materials or scene-wide. "
               "Use to optimize reflective surfaces or highlight important materials.";
    }
    
    json GetInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"scope", {
                    {"type", "string"},
                    {"enum", {"global", "material", "entity"}},
                    {"description", "Scope of quality change"}
                }},
                {"materialId", {
                    {"type", "string"},
                    {"description", "Material ID for material scope"}
                }},
                {"entityId", {
                    {"type", "integer"},
                    {"description", "Entity ID for entity scope"}
                }},
                {"quality", {
                    {"type", "string"},
                    {"enum", {"off", "low", "medium", "high", "ultra"}},
                    {"description", "Reflection quality preset"}
                }},
                {"roughnessThreshold", {
                    {"type", "number"},
                    {"description", "Roughness above which to use cheaper fallback"},
                    {"minimum", 0.0},
                    {"maximum", 1.0}
                }},
                {"maxBounces", {
                    {"type", "integer"},
                    {"description", "Maximum reflection bounces"},
                    {"minimum", 1},
                    {"maximum", 3}
                }}
            }},
            {"required", {"scope", "quality"}}
        };
    }
    
    json Execute(const json& params, Scene* scene) override {
        if (!m_ReflectionPass) {
            return {{"success", false}, {"error", "RTReflectionPass not available"}};
        }
        
        std::string scope = params["scope"].get<std::string>();
        std::string qualityStr = params["quality"].get<std::string>();
        
        json result;
        result["success"] = true;
        result["scope"] = scope;
        
        if (scope == "global") {
            auto quality = StringToQuality(qualityStr);
            m_ReflectionPass->SetQuality(quality);
            
            auto config = m_ReflectionPass->GetConfig();
            
            if (params.contains("roughnessThreshold")) {
                config.roughnessThreshold = params["roughnessThreshold"].get<float>();
            }
            if (params.contains("maxBounces")) {
                config.maxBounces = params["maxBounces"].get<uint32_t>();
            }
            
            m_ReflectionPass->SetConfig(config);
            result["appliedQuality"] = qualityStr;
        }
        else if (scope == "material" && params.contains("materialId")) {
            uint32_t materialId = std::hash<std::string>{}(params["materialId"].get<std::string>());
            
            AIEngine::Rendering::MaterialReflectionOverride override;
            override.hasOverride = true;
            override.quality = StringToQuality(qualityStr);
            
            if (params.contains("maxBounces")) {
                override.maxBounces = params["maxBounces"].get<uint32_t>();
            }
            if (params.contains("roughnessThreshold")) {
                override.roughnessOverride = params["roughnessThreshold"].get<float>();
            }
            
            m_ReflectionPass->SetMaterialOverride(materialId, override);
            result["materialId"] = params["materialId"];
        }
        else if (scope == "entity" && params.contains("entityId")) {
            // Entity-level overrides would need material component access
            result["note"] = "Entity-level overrides require material component modification";
        }
        
        return result;
    }
    
    void SetRTReflectionPass(AIEngine::Rendering::RTReflectionPass* pass) { m_ReflectionPass = pass; }
    
private:
    static AIEngine::Rendering::RTReflectionQuality StringToQuality(const std::string& s) {
        if (s == "off") return AIEngine::Rendering::RTReflectionQuality::Off;
        if (s == "low") return AIEngine::Rendering::RTReflectionQuality::Low;
        if (s == "medium") return AIEngine::Rendering::RTReflectionQuality::Medium;
        if (s == "high") return AIEngine::Rendering::RTReflectionQuality::High;
        if (s == "ultra") return AIEngine::Rendering::RTReflectionQuality::Ultra;
        return AIEngine::Rendering::RTReflectionQuality::Medium;
    }
    
    AIEngine::Rendering::RTReflectionPass* m_ReflectionPass = nullptr;
};

/// Tool to query ray tracing capabilities
class QueryRTCapabilitiesTool : public MCPTool {
public:
    std::string GetName() const override { return "QueryRTCapabilities"; }
    
    std::string GetDescription() const override {
        return "Query hardware ray tracing capabilities and current settings. "
               "Use to determine available features and optimize settings.";
    }
    
    json GetInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"includeHardwareInfo", {
                    {"type", "boolean"},
                    {"description", "Include detailed hardware capability info"},
                    {"default", true}
                }},
                {"includeCurrentSettings", {
                    {"type", "boolean"},
                    {"description", "Include current RT feature settings"},
                    {"default", true}
                }}
            }}
        };
    }
    
    json Execute(const json& params, Scene* scene) override {
        json result;
        result["success"] = true;
        
        bool includeHardware = params.value("includeHardwareInfo", true);
        bool includeSettings = params.value("includeCurrentSettings", true);
        
        result["rayTracingSupported"] = m_Device ? m_Device->IsRayTracingSupported() : false;
        
        if (includeHardware && m_Device) {
            auto caps = m_Device->GetRTCapabilities();
            result["hardware"] = {
                {"maxRayRecursionDepth", caps.maxRayRecursionDepth},
                {"maxGeometryCount", caps.maxGeometryCount},
                {"maxInstanceCount", caps.maxInstanceCount},
                {"maxPrimitiveCount", caps.maxPrimitiveCount},
                {"shaderGroupHandleSize", caps.shaderGroupHandleSize}
            };
        }
        
        if (includeSettings) {
            result["currentSettings"] = {
                {"shadows", {
                    {"available", m_ShadowPass != nullptr},
                    {"quality", m_ShadowPass ? 
                        QualityToString(m_ShadowPass->GetQuality()) : "unavailable"}
                }},
                {"reflections", {
                    {"available", m_ReflectionPass != nullptr},
                    {"quality", m_ReflectionPass ? 
                        ReflectionQualityToString(m_ReflectionPass->GetQuality()) : "unavailable"}
                }},
                {"gi", {
                    {"available", m_GI != nullptr},
                    {"technique", m_GI ? 
                        TechniqueToString(m_GI->GetTechnique()) : "unavailable"},
                    {"quality", m_GI ? 
                        GIQualityToString(m_GI->GetQuality()) : "unavailable"}
                }}
            };
        }
        
        return result;
    }
    
    void SetRHIDevice(AIEngine::RHI::RHIDeviceRT* device) { m_Device = device; }
    void SetRTShadowPass(AIEngine::Rendering::RTShadowPass* pass) { m_ShadowPass = pass; }
    void SetRTReflectionPass(AIEngine::Rendering::RTReflectionPass* pass) { m_ReflectionPass = pass; }
    void SetGlobalIllumination(AIEngine::Rendering::GlobalIllumination* gi) { m_GI = gi; }
    
private:
    static std::string QualityToString(AIEngine::Rendering::RTShadowQuality q) {
        switch (q) {
            case AIEngine::Rendering::RTShadowQuality::Off: return "off";
            case AIEngine::Rendering::RTShadowQuality::Low: return "low";
            case AIEngine::Rendering::RTShadowQuality::Medium: return "medium";
            case AIEngine::Rendering::RTShadowQuality::High: return "high";
            case AIEngine::Rendering::RTShadowQuality::Ultra: return "ultra";
            default: return "unknown";
        }
    }
    
    static std::string ReflectionQualityToString(AIEngine::Rendering::RTReflectionQuality q) {
        switch (q) {
            case AIEngine::Rendering::RTReflectionQuality::Off: return "off";
            case AIEngine::Rendering::RTReflectionQuality::Low: return "low";
            case AIEngine::Rendering::RTReflectionQuality::Medium: return "medium";
            case AIEngine::Rendering::RTReflectionQuality::High: return "high";
            case AIEngine::Rendering::RTReflectionQuality::Ultra: return "ultra";
            default: return "unknown";
        }
    }
    
    static std::string TechniqueToString(AIEngine::Rendering::GITechnique t) {
        switch (t) {
            case AIEngine::Rendering::GITechnique::None: return "none";
            case AIEngine::Rendering::GITechnique::ScreenSpace: return "screenspace";
            case AIEngine::Rendering::GITechnique::VoxelConeTracing: return "voxel";
            case AIEngine::Rendering::GITechnique::RayTraced: return "raytraced";
            case AIEngine::Rendering::GITechnique::IrradianceProbes: return "probes";
            default: return "unknown";
        }
    }
    
    static std::string GIQualityToString(AIEngine::Rendering::GIQuality q) {
        switch (q) {
            case AIEngine::Rendering::GIQuality::Off: return "off";
            case AIEngine::Rendering::GIQuality::Low: return "low";
            case AIEngine::Rendering::GIQuality::Medium: return "medium";
            case AIEngine::Rendering::GIQuality::High: return "high";
            case AIEngine::Rendering::GIQuality::Ultra: return "ultra";
            default: return "unknown";
        }
    }
    
    AIEngine::RHI::RHIDeviceRT* m_Device = nullptr;
    AIEngine::Rendering::RTShadowPass* m_ShadowPass = nullptr;
    AIEngine::Rendering::RTReflectionPass* m_ReflectionPass = nullptr;
    AIEngine::Rendering::GlobalIllumination* m_GI = nullptr;
};

/// Factory function to create all ray tracing MCP tools
inline std::vector<std::unique_ptr<MCPTool>> CreateRayTracingTools() {
    std::vector<std::unique_ptr<MCPTool>> tools;
    tools.push_back(std::make_unique<ToggleRayTracingFeaturesTool>());
    tools.push_back(std::make_unique<BakeGlobalIlluminationTool>());
    tools.push_back(std::make_unique<SetReflectionQualityTool>());
    tools.push_back(std::make_unique<QueryRTCapabilitiesTool>());
    return tools;
}

} // namespace Core::MCP
