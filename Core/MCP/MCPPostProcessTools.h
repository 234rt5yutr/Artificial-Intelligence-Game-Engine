#pragma once

// MCP Post-Processing Tools
// Allows AI agents to control visual mood and camera effects for narrative-driven cinematography

#include "MCPTool.h"
#include "MCPTypes.h"
#include "Core/ECS/Components/PostProcessComponent.h"
#include <entt/entt.hpp>
#include <unordered_map>

namespace Core {

// Forward declarations
namespace ECS {
    class Scene;
}

namespace MCP {

    // ============================================================================
    // Post-Processing Profile Presets
    // ============================================================================
    
    // Predefined mood profiles for quick cinematic changes
    struct PostProcessProfile {
        std::string name;
        ECS::PostProcessSettings settings;
    };

    // Get default cinematic profiles
    inline std::unordered_map<std::string, PostProcessProfile> GetDefaultProfiles() {
        std::unordered_map<std::string, PostProcessProfile> profiles;

        // Cinematic - Film-like look with controlled bloom and slight desaturation
        {
            PostProcessProfile profile;
            profile.name = "cinematic";
            profile.settings.bloomEnabled = true;
            profile.settings.bloomIntensity = 0.8f;
            profile.settings.bloomThreshold = 1.2f;
            profile.settings.ssaoEnabled = true;
            profile.settings.ssaoIntensity = 1.0f;
            profile.settings.dofEnabled = true;
            profile.settings.dofFocalDistance = 5.0f;
            profile.settings.dofMaxBlur = 0.8f;
            profile.settings.colorGradingEnabled = true;
            profile.settings.exposure = 0.0f;
            profile.settings.contrast = 1.1f;
            profile.settings.saturation = 0.9f;
            profile.settings.vignetteEnabled = true;
            profile.settings.vignetteIntensity = 0.4f;
            profile.settings.tonemapOperator = 0;  // ACES
            profiles["cinematic"] = profile;
        }

        // Horror - Dark, desaturated, high contrast
        {
            PostProcessProfile profile;
            profile.name = "horror";
            profile.settings.bloomEnabled = true;
            profile.settings.bloomIntensity = 0.3f;
            profile.settings.bloomThreshold = 1.5f;
            profile.settings.ssaoEnabled = true;
            profile.settings.ssaoIntensity = 1.5f;
            profile.settings.ssaoRadius = 0.8f;
            profile.settings.dofEnabled = false;
            profile.settings.colorGradingEnabled = true;
            profile.settings.exposure = -0.3f;
            profile.settings.contrast = 1.3f;
            profile.settings.saturation = 0.6f;
            profile.settings.temperature = -0.2f;  // Cool
            profile.settings.vignetteEnabled = true;
            profile.settings.vignetteIntensity = 0.7f;
            profile.settings.vignetteSmoothness = 0.2f;
            profile.settings.tonemapOperator = 0;
            profiles["horror"] = profile;
        }

        // Vibrant - Colorful, energetic look
        {
            PostProcessProfile profile;
            profile.name = "vibrant";
            profile.settings.bloomEnabled = true;
            profile.settings.bloomIntensity = 1.2f;
            profile.settings.bloomThreshold = 0.8f;
            profile.settings.ssaoEnabled = true;
            profile.settings.ssaoIntensity = 0.8f;
            profile.settings.dofEnabled = false;
            profile.settings.colorGradingEnabled = true;
            profile.settings.exposure = 0.1f;
            profile.settings.contrast = 1.0f;
            profile.settings.saturation = 1.3f;
            profile.settings.vignetteEnabled = false;
            profile.settings.tonemapOperator = 0;
            profiles["vibrant"] = profile;
        }

        // Noir - Black and white with high contrast
        {
            PostProcessProfile profile;
            profile.name = "noir";
            profile.settings.bloomEnabled = true;
            profile.settings.bloomIntensity = 0.5f;
            profile.settings.bloomThreshold = 1.0f;
            profile.settings.ssaoEnabled = true;
            profile.settings.ssaoIntensity = 1.2f;
            profile.settings.dofEnabled = true;
            profile.settings.dofFocalDistance = 8.0f;
            profile.settings.dofMaxBlur = 0.6f;
            profile.settings.colorGradingEnabled = true;
            profile.settings.exposure = 0.0f;
            profile.settings.contrast = 1.4f;
            profile.settings.saturation = 0.0f;  // Grayscale
            profile.settings.vignetteEnabled = true;
            profile.settings.vignetteIntensity = 0.6f;
            profile.settings.tonemapOperator = 0;
            profiles["noir"] = profile;
        }

        // Dreamy - Soft, warm, ethereal look
        {
            PostProcessProfile profile;
            profile.name = "dreamy";
            profile.settings.bloomEnabled = true;
            profile.settings.bloomIntensity = 1.5f;
            profile.settings.bloomThreshold = 0.6f;
            profile.settings.bloomSoftKnee = 0.8f;
            profile.settings.ssaoEnabled = true;
            profile.settings.ssaoIntensity = 0.5f;
            profile.settings.dofEnabled = true;
            profile.settings.dofFocalDistance = 3.0f;
            profile.settings.dofMaxBlur = 1.2f;
            profile.settings.colorGradingEnabled = true;
            profile.settings.exposure = 0.2f;
            profile.settings.contrast = 0.9f;
            profile.settings.saturation = 1.1f;
            profile.settings.temperature = 0.15f;  // Warm
            profile.settings.vignetteEnabled = true;
            profile.settings.vignetteIntensity = 0.3f;
            profile.settings.tonemapOperator = 0;
            profiles["dreamy"] = profile;
        }

        return profiles;
    }

    // ============================================================================
    // SetPostProcessProfile Tool
    // ============================================================================
    
    class SetPostProcessProfileTool : public MCPTool {
    public:
        SetPostProcessProfileTool()
            : MCPTool("SetPostProcessProfile",
                      "Apply a post-processing profile to set the visual mood of the scene. "
                      "Choose from predefined profiles (cinematic, horror, vibrant, noir, dreamy) "
                      "or provide custom settings for fine-grained control.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Required = {"profile"};
            
            // Profile name
            ToolInputSchema::SchemaProperty profileProp;
            profileProp.Type = "string";
            profileProp.Description = "Profile name: 'cinematic', 'horror', 'vibrant', 'noir', 'dreamy', or 'custom'";
            schema.Properties["profile"] = profileProp;

            // Custom settings (optional)
            ToolInputSchema::SchemaProperty customProp;
            customProp.Type = "object";
            customProp.Description = "Custom settings when profile is 'custom'. All fields are optional.";
            schema.Properties["customSettings"] = customProp;

            // Entity ID (optional - defaults to main camera)
            ToolInputSchema::SchemaProperty entityProp;
            entityProp.Type = "integer";
            entityProp.Description = "Entity ID to apply profile to. Defaults to the active camera.";
            schema.Properties["entityId"] = entityProp;

            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override;
    };

    // ============================================================================
    // BlendCameraEffects Tool
    // ============================================================================
    
    class BlendCameraEffectsTool : public MCPTool {
    public:
        BlendCameraEffectsTool()
            : MCPTool("BlendCameraEffects",
                      "Smoothly transition camera effects over time. Use this to create "
                      "cinematic moments like focus pulls, blur transitions, or mood shifts.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Required = {"effect"};
            
            // Effect type
            ToolInputSchema::SchemaProperty effectProp;
            effectProp.Type = "string";
            effectProp.Description = "Effect to blend: 'depthOfField', 'motionBlur', 'bloom', 'colorGrading', or 'all'";
            schema.Properties["effect"] = effectProp;

            // Target values (based on effect type)
            ToolInputSchema::SchemaProperty focalDistProp;
            focalDistProp.Type = "number";
            focalDistProp.Description = "Target focal distance for depth of field";
            schema.Properties["focalDistance"] = focalDistProp;

            ToolInputSchema::SchemaProperty focalRangeProp;
            focalRangeProp.Type = "number";
            focalRangeProp.Description = "Target focal range for depth of field";
            schema.Properties["focalRange"] = focalRangeProp;

            ToolInputSchema::SchemaProperty apertureProp;
            apertureProp.Type = "number";
            apertureProp.Description = "Target aperture (f-stop) for depth of field";
            schema.Properties["aperture"] = apertureProp;

            ToolInputSchema::SchemaProperty motionBlurProp;
            motionBlurProp.Type = "number";
            motionBlurProp.Description = "Target motion blur strength (0-1)";
            schema.Properties["motionBlurStrength"] = motionBlurProp;

            ToolInputSchema::SchemaProperty bloomProp;
            bloomProp.Type = "number";
            bloomProp.Description = "Target bloom intensity";
            schema.Properties["bloomIntensity"] = bloomProp;

            ToolInputSchema::SchemaProperty exposureProp;
            exposureProp.Type = "number";
            exposureProp.Description = "Target exposure value";
            schema.Properties["exposure"] = exposureProp;

            ToolInputSchema::SchemaProperty saturationProp;
            saturationProp.Type = "number";
            saturationProp.Description = "Target saturation (0-2)";
            schema.Properties["saturation"] = saturationProp;

            // Transition duration
            ToolInputSchema::SchemaProperty durationProp;
            durationProp.Type = "number";
            durationProp.Description = "Transition duration in seconds (default: 1.0)";
            schema.Properties["transitionDuration"] = durationProp;

            // Entity ID (optional)
            ToolInputSchema::SchemaProperty entityProp;
            entityProp.Type = "integer";
            entityProp.Description = "Entity ID to apply blend to. Defaults to the active camera.";
            schema.Properties["entityId"] = entityProp;

            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override;
    };

    // ============================================================================
    // GetPostProcessInfo Tool
    // ============================================================================
    
    class GetPostProcessInfoTool : public MCPTool {
    public:
        GetPostProcessInfoTool()
            : MCPTool("GetPostProcessInfo",
                      "Query the current post-processing settings and active profiles. "
                      "Useful for understanding the current visual state before making changes.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            
            // Entity ID (optional)
            ToolInputSchema::SchemaProperty entityProp;
            entityProp.Type = "integer";
            entityProp.Description = "Entity ID to query. Defaults to the active camera.";
            schema.Properties["entityId"] = entityProp;

            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override;
    };

    // ============================================================================
    // Factory Function
    // ============================================================================
    
    inline std::vector<MCPToolPtr> CreatePostProcessTools() {
        std::vector<MCPToolPtr> tools;
        
        tools.push_back(std::make_shared<SetPostProcessProfileTool>());
        tools.push_back(std::make_shared<BlendCameraEffectsTool>());
        tools.push_back(std::make_shared<GetPostProcessInfoTool>());
        
        return tools;
    }

    // ============================================================================
    // Tool Implementations
    // ============================================================================
    
    inline ToolResult SetPostProcessProfileTool::Execute(const Json& arguments, ECS::Scene* scene) {
        ToolResult result;
        
        if (!scene) {
            result.IsError = true;
            result.Content = {{"text", "No scene available"}};
            return result;
        }

        std::string profileName = arguments.value("profile", "cinematic");
        
        // Get the post-process component
        auto& registry = scene->GetRegistry();
        
        // Find entity with PostProcessComponent (or create one)
        entt::entity targetEntity = entt::null;
        uint32_t entityId = arguments.value("entityId", 0u);
        
        if (entityId > 0) {
            targetEntity = static_cast<entt::entity>(entityId);
        } else {
            // Find first entity with PostProcessComponent
            auto view = registry.view<ECS::PostProcessComponent>();
            for (auto entity : view) {
                targetEntity = entity;
                break;
            }
        }

        if (targetEntity == entt::null || !registry.valid(targetEntity)) {
            result.IsError = true;
            result.Content = {{"text", "No entity with PostProcessComponent found"}};
            return result;
        }

        auto& postProcess = registry.get<ECS::PostProcessComponent>(targetEntity);
        
        if (profileName == "custom") {
            // Apply custom settings
            if (arguments.contains("customSettings")) {
                const auto& custom = arguments["customSettings"];
                
                if (custom.contains("bloomIntensity"))
                    postProcess.settings.bloomIntensity = custom["bloomIntensity"];
                if (custom.contains("bloomEnabled"))
                    postProcess.settings.bloomEnabled = custom["bloomEnabled"];
                if (custom.contains("ssaoIntensity"))
                    postProcess.settings.ssaoIntensity = custom["ssaoIntensity"];
                if (custom.contains("ssaoEnabled"))
                    postProcess.settings.ssaoEnabled = custom["ssaoEnabled"];
                if (custom.contains("exposure"))
                    postProcess.settings.exposure = custom["exposure"];
                if (custom.contains("saturation"))
                    postProcess.settings.saturation = custom["saturation"];
                if (custom.contains("contrast"))
                    postProcess.settings.contrast = custom["contrast"];
                if (custom.contains("vignetteIntensity"))
                    postProcess.settings.vignetteIntensity = custom["vignetteIntensity"];
                if (custom.contains("vignetteEnabled"))
                    postProcess.settings.vignetteEnabled = custom["vignetteEnabled"];
            }
            
            result.Content = {{"text", "Applied custom post-processing settings"}};
        } else {
            // Apply predefined profile
            auto profiles = GetDefaultProfiles();
            auto it = profiles.find(profileName);
            
            if (it == profiles.end()) {
                result.IsError = true;
                result.Content = {{"text", "Unknown profile: " + profileName + 
                    ". Available: cinematic, horror, vibrant, noir, dreamy, custom"}};
                return result;
            }

            postProcess.settings = it->second.settings;
            result.Content = {{"text", "Applied '" + profileName + "' post-processing profile"}};
        }

        result.IsError = false;
        return result;
    }

    inline ToolResult BlendCameraEffectsTool::Execute(const Json& arguments, ECS::Scene* scene) {
        ToolResult result;
        
        if (!scene) {
            result.IsError = true;
            result.Content = {{"text", "No scene available"}};
            return result;
        }

        std::string effectType = arguments.value("effect", "all");
        float duration = arguments.value("transitionDuration", 1.0f);
        
        auto& registry = scene->GetRegistry();
        
        // Find entity
        entt::entity targetEntity = entt::null;
        uint32_t entityId = arguments.value("entityId", 0u);
        
        if (entityId > 0) {
            targetEntity = static_cast<entt::entity>(entityId);
        } else {
            auto view = registry.view<ECS::PostProcessComponent>();
            for (auto entity : view) {
                targetEntity = entity;
                break;
            }
        }

        if (targetEntity == entt::null || !registry.valid(targetEntity)) {
            result.IsError = true;
            result.Content = {{"text", "No entity with PostProcessComponent found"}};
            return result;
        }

        auto& postProcess = registry.get<ECS::PostProcessComponent>(targetEntity);
        
        // Copy current settings as starting point for blend
        postProcess.targetSettings = postProcess.settings;
        
        // Update target settings based on effect type
        if (effectType == "depthOfField" || effectType == "all") {
            if (arguments.contains("focalDistance"))
                postProcess.targetSettings.dofFocalDistance = arguments["focalDistance"];
            if (arguments.contains("focalRange"))
                postProcess.targetSettings.dofFocalRange = arguments["focalRange"];
            
            // Enable DoF if we're targeting it
            if (arguments.contains("focalDistance") || arguments.contains("focalRange"))
                postProcess.targetSettings.dofEnabled = true;
        }

        if (effectType == "motionBlur" || effectType == "all") {
            if (arguments.contains("motionBlurStrength")) {
                postProcess.targetSettings.motionBlurScale = arguments["motionBlurStrength"];
                postProcess.targetSettings.motionBlurEnabled = 
                    postProcess.targetSettings.motionBlurScale > 0.0f;
            }
        }

        if (effectType == "bloom" || effectType == "all") {
            if (arguments.contains("bloomIntensity"))
                postProcess.targetSettings.bloomIntensity = arguments["bloomIntensity"];
        }

        if (effectType == "colorGrading" || effectType == "all") {
            if (arguments.contains("exposure"))
                postProcess.targetSettings.exposure = arguments["exposure"];
            if (arguments.contains("saturation"))
                postProcess.targetSettings.saturation = arguments["saturation"];
        }

        // Start blending
        postProcess.blendProgress = 0.0f;
        postProcess.blendDuration = duration;
        postProcess.isBlending = true;

        result.IsError = false;
        result.Content = {{"text", "Started " + std::to_string(duration) + 
            "s blend transition for '" + effectType + "' effects"}};
        return result;
    }

    inline ToolResult GetPostProcessInfoTool::Execute(const Json& arguments, ECS::Scene* scene) {
        ToolResult result;
        
        if (!scene) {
            result.IsError = true;
            result.Content = {{"text", "No scene available"}};
            return result;
        }

        auto& registry = scene->GetRegistry();
        
        // Find entity
        entt::entity targetEntity = entt::null;
        uint32_t entityId = arguments.value("entityId", 0u);
        
        if (entityId > 0) {
            targetEntity = static_cast<entt::entity>(entityId);
        } else {
            auto view = registry.view<ECS::PostProcessComponent>();
            for (auto entity : view) {
                targetEntity = entity;
                break;
            }
        }

        if (targetEntity == entt::null || !registry.valid(targetEntity)) {
            result.IsError = true;
            result.Content = {{"text", "No entity with PostProcessComponent found"}};
            return result;
        }

        const auto& postProcess = registry.get<ECS::PostProcessComponent>(targetEntity);
        const auto& s = postProcess.settings;

        Json info;
        info["entityId"] = static_cast<uint32_t>(targetEntity);
        
        // Bloom
        info["bloom"]["enabled"] = s.bloomEnabled;
        info["bloom"]["intensity"] = s.bloomIntensity;
        info["bloom"]["threshold"] = s.bloomThreshold;
        
        // SSAO
        info["ssao"]["enabled"] = s.ssaoEnabled;
        info["ssao"]["intensity"] = s.ssaoIntensity;
        info["ssao"]["radius"] = s.ssaoRadius;
        
        // Depth of Field
        info["depthOfField"]["enabled"] = s.dofEnabled;
        info["depthOfField"]["focalDistance"] = s.dofFocalDistance;
        info["depthOfField"]["focalRange"] = s.dofFocalRange;
        info["depthOfField"]["maxBlur"] = s.dofMaxBlur;
        
        // Motion Blur
        info["motionBlur"]["enabled"] = s.motionBlurEnabled;
        info["motionBlur"]["scale"] = s.motionBlurScale;
        
        // Color Grading
        info["colorGrading"]["enabled"] = s.colorGradingEnabled;
        info["colorGrading"]["exposure"] = s.exposure;
        info["colorGrading"]["contrast"] = s.contrast;
        info["colorGrading"]["saturation"] = s.saturation;
        info["colorGrading"]["temperature"] = s.temperature;
        info["colorGrading"]["tint"] = s.tint;
        info["colorGrading"]["tonemapMode"] = s.tonemapOperator;
        
        // Vignette
        info["vignette"]["enabled"] = s.vignetteEnabled;
        info["vignette"]["intensity"] = s.vignetteIntensity;
        
        // Blend state
        info["isBlending"] = postProcess.isBlending;
        info["blendProgress"] = postProcess.blendProgress;
        
        // Available profiles
        info["availableProfiles"] = Json::array({"cinematic", "horror", "vibrant", "noir", "dreamy", "custom"});

        result.IsError = false;
        result.Content = {{"text", info.dump(2)}};
        return result;
    }

} // namespace MCP
} // namespace Core
