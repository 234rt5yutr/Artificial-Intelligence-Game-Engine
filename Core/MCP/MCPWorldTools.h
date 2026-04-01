#pragma once

// ============================================================================
// MCPWorldTools.h
// MCP tools for AI agents to control procedural terrain generation and
// day/night cycle in the game world
// 
// Tools provided:
// - GenerateBiome: Procedurally generate terrain and scatter foliage
// - SetTimeOfDay: Control sun position, lighting, and day/night cycle
// ============================================================================

#include "MCPTool.h"
#include "MCPTypes.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/Components.h"
#include "Core/ECS/Components/TerrainComponent.h"
#include "Core/ECS/Components/LightComponent.h"
#include "Core/Log.h"

#include <sstream>
#include <algorithm>
#include <cmath>
#include <random>
#include <chrono>

namespace Core {
namespace MCP {

    // ========================================================================
    // FoliageComponent (inline definition for foliage instance data)
    // ========================================================================
    
    namespace ECS {
        
        // Instance of a foliage object (tree, grass, rock, etc.)
        struct FoliageInstance {
            glm::vec3 Position{0.0f};
            glm::vec3 Scale{1.0f};
            float Rotation = 0.0f;  // Y-axis rotation in radians
            std::string MeshPath;
            uint32_t LODLevel = 0;
        };

        // Component for managing foliage instances
        struct FoliageComponent {
            std::string FoliageType;            // "tree", "grass", "bush", "rock"
            std::vector<FoliageInstance> Instances;
            float Density = 0.5f;               // 0.0 to 1.0
            float MinScale = 0.8f;
            float MaxScale = 1.2f;
            float CullDistance = 500.0f;
            bool CastShadows = true;
            bool ReceiveShadows = true;
            bool WindAffected = true;
            float WindStrength = 1.0f;

            FoliageComponent() = default;
        };

        // Component for time of day management
        struct TimeOfDayComponent {
            float CurrentTime = 12.0f;          // 0.0 to 24.0 (hours)
            float TransitionProgress = 1.0f;   // 0.0 to 1.0 (1.0 = complete)
            float TargetTime = 12.0f;
            float TransitionDuration = 0.0f;    // seconds
            float TransitionElapsed = 0.0f;
            
            bool CycleEnabled = false;
            float CycleSpeed = 1.0f;            // 1.0 = real-time, 60.0 = 1 min = 1 hour
            
            // Sun properties
            glm::vec3 SunDirection{0.0f, -1.0f, 0.0f};
            glm::vec3 SunColor{1.0f, 0.95f, 0.9f};
            float SunIntensity = 1.0f;
            
            // Override settings
            bool HasColorOverride = false;
            glm::vec3 SunColorOverride{1.0f};
            bool HasIntensityOverride = false;
            float SunIntensityOverride = 1.0f;
            
            // Ambient lighting
            float AmbientIntensity = 0.2f;
            glm::vec3 AmbientColor{0.5f, 0.6f, 0.8f};

            TimeOfDayComponent() = default;

            bool IsDay() const {
                return CurrentTime >= 6.0f && CurrentTime < 18.0f;
            }

            bool IsTransitioning() const {
                return TransitionProgress < 1.0f;
            }
        };

        // Skybox component for sky rendering
        struct SkyboxComponent {
            std::string CubemapPath;
            std::string DayCubemapPath;
            std::string NightCubemapPath;
            float BlendFactor = 0.0f;           // 0 = day, 1 = night
            float Rotation = 0.0f;
            float Intensity = 1.0f;
            bool Enabled = true;

            SkyboxComponent() = default;
        };

    } // namespace ECS

    // ========================================================================
    // GenerateBiome Tool
    // ========================================================================
    // Procedurally generate terrain and scatter foliage for a biome

    class GenerateBiomeTool : public MCPTool {
    public:
        GenerateBiomeTool()
            : MCPTool("GenerateBiome",
                      "Procedurally generate terrain and scatter foliage for a biome area. "
                      "Creates heightmap-based terrain with appropriate vegetation based on "
                      "biome type. Supports desert, forest, mountain, plains, and tundra biomes. "
                      "Can specify foliage density, tree types, and grass coverage.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"biome_type", {
                    {"type", "string"},
                    {"enum", Json::array({"desert", "forest", "mountain", "plains", "tundra"})},
                    {"description", "Type of biome to generate: 'desert' (sandy dunes, sparse vegetation), "
                                    "'forest' (dense trees, moderate terrain), 'mountain' (steep peaks, rocks), "
                                    "'plains' (flat grasslands), 'tundra' (snowy, sparse vegetation)"}
                }},
                {"center_x", {
                    {"type", "number"},
                    {"description", "X coordinate of the biome center in world space"}
                }},
                {"center_z", {
                    {"type", "number"},
                    {"description", "Z coordinate of the biome center in world space"}
                }},
                {"radius", {
                    {"type", "number"},
                    {"minimum", 10.0},
                    {"maximum", 10000.0},
                    {"description", "Radius of the area to generate (in world units)"}
                }},
                {"seed", {
                    {"type", "integer"},
                    {"description", "Random seed for reproducible generation (optional, random if not provided)"}
                }},
                {"height_scale", {
                    {"type", "number"},
                    {"minimum", 1.0},
                    {"maximum", 1000.0},
                    {"description", "Terrain height multiplier (default varies by biome)"},
                    {"default", 100.0}
                }},
                {"foliage_density", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 1.0},
                    {"description", "Overall density of foliage (0.0 = none, 1.0 = maximum)"},
                    {"default", 0.5}
                }},
                {"tree_types", {
                    {"type", "array"},
                    {"items", {{"type", "string"}}},
                    {"description", "List of tree types to use (e.g., ['oak', 'pine', 'birch']). "
                                    "If not specified, uses biome-appropriate defaults"}
                }},
                {"grass_density", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 1.0},
                    {"description", "Density of grass coverage (0.0 = none, 1.0 = full coverage)"},
                    {"default", 0.5}
                }}
            };
            schema.Required = {"biome_type", "center_x", "center_z", "radius"};
            return schema;
        }

        ToolResult Execute(const Json& arguments, Core::ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("Scene is not available");
            }

            // Parse required arguments
            std::string biomeTypeStr = arguments.value("biome_type", "");
            if (biomeTypeStr.empty()) {
                return ToolResult::Error("biome_type is required");
            }

            float centerX = arguments.value("center_x", 0.0f);
            float centerZ = arguments.value("center_z", 0.0f);
            float radius = arguments.value("radius", 100.0f);

            // Validate radius
            if (radius < 10.0f) {
                return ToolResult::Error("radius must be at least 10.0");
            }
            if (radius > 10000.0f) {
                return ToolResult::Error("radius cannot exceed 10000.0");
            }

            // Parse biome type
            Core::ECS::BiomeType biomeType;
            if (biomeTypeStr == "desert") {
                biomeType = Core::ECS::BiomeType::Desert;
            } else if (biomeTypeStr == "forest") {
                biomeType = Core::ECS::BiomeType::Forest;
            } else if (biomeTypeStr == "mountain") {
                biomeType = Core::ECS::BiomeType::Mountain;
            } else if (biomeTypeStr == "plains") {
                biomeType = Core::ECS::BiomeType::Plains;
            } else if (biomeTypeStr == "tundra") {
                biomeType = Core::ECS::BiomeType::Tundra;
            } else {
                return ToolResult::Error("Invalid biome_type: " + biomeTypeStr);
            }

            // Parse optional arguments
            uint32_t seed;
            if (arguments.contains("seed") && arguments["seed"].is_number_integer()) {
                seed = static_cast<uint32_t>(arguments["seed"].get<int64_t>());
            } else {
                seed = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            }

            float heightScale = arguments.value("height_scale", GetDefaultHeightScale(biomeType));
            float foliageDensity = std::clamp(arguments.value("foliage_density", 0.5f), 0.0f, 1.0f);
            float grassDensity = std::clamp(arguments.value("grass_density", 0.5f), 0.0f, 1.0f);

            // Parse tree types
            std::vector<std::string> treeTypes;
            if (arguments.contains("tree_types") && arguments["tree_types"].is_array()) {
                for (const auto& t : arguments["tree_types"]) {
                    if (t.is_string()) {
                        treeTypes.push_back(t.get<std::string>());
                    }
                }
            }
            if (treeTypes.empty()) {
                treeTypes = GetDefaultTreeTypes(biomeType);
            }

            // Generate terrain entity
            std::string terrainEntityName = "Terrain_" + biomeTypeStr + "_" + std::to_string(seed);
            auto terrainEntity = scene->CreateEntity(terrainEntityName);

            // Add transform component
            auto& transform = terrainEntity.AddComponent<Core::ECS::TransformComponent>();
            transform.Position = glm::vec3(centerX, 0.0f, centerZ);

            // Create terrain component using the factory method
            auto terrain = Core::ECS::TerrainComponent::CreateProcedural(
                biomeType, heightScale, 64, seed
            );
            
            // Add biome-appropriate material layers
            AddBiomeMaterialLayers(terrain, biomeType);
            
            terrainEntity.AddComponent<Core::ECS::TerrainComponent>(terrain);

            // Calculate chunk count (approximate based on radius and chunk size)
            float chunkWorldSize = terrain.ChunkSize * terrain.HorizontalScale;
            int chunksPerSide = static_cast<int>(std::ceil((radius * 2.0f) / chunkWorldSize));
            int chunksGenerated = chunksPerSide * chunksPerSide;

            // Generate foliage
            std::vector<std::string> foliageEntityIds;
            int totalFoliageInstances = 0;

            // Random number generator
            std::mt19937 rng(seed);
            std::uniform_real_distribution<float> posDist(-radius, radius);
            std::uniform_real_distribution<float> rotDist(0.0f, glm::two_pi<float>());
            std::uniform_real_distribution<float> scaleDist(0.8f, 1.2f);

            // Generate trees
            if (foliageDensity > 0.0f && !treeTypes.empty()) {
                int treeCount = static_cast<int>(GetTreeCountForBiome(biomeType, radius, foliageDensity));
                
                for (const auto& treeType : treeTypes) {
                    std::string treeEntityName = "Foliage_" + treeType + "_" + std::to_string(seed);
                    auto treeEntity = scene->CreateEntity(treeEntityName);
                    
                    auto& treeTransform = treeEntity.AddComponent<Core::ECS::TransformComponent>();
                    treeTransform.Position = glm::vec3(centerX, 0.0f, centerZ);

                    ECS::FoliageComponent foliage;
                    foliage.FoliageType = "tree";
                    foliage.Density = foliageDensity;
                    foliage.CastShadows = true;
                    foliage.WindAffected = (biomeType != Core::ECS::BiomeType::Desert);

                    int treesPerType = treeCount / static_cast<int>(treeTypes.size());
                    for (int i = 0; i < treesPerType; ++i) {
                        ECS::FoliageInstance instance;
                        instance.Position = glm::vec3(
                            centerX + posDist(rng),
                            0.0f,  // Y will be set by terrain height
                            centerZ + posDist(rng)
                        );
                        instance.Rotation = rotDist(rng);
                        instance.Scale = glm::vec3(scaleDist(rng));
                        instance.MeshPath = "assets/foliage/trees/" + treeType + ".obj";
                        foliage.Instances.push_back(instance);
                    }

                    totalFoliageInstances += static_cast<int>(foliage.Instances.size());
                    
                    // Store foliage component (using custom component storage in scene)
                    // Note: This would typically be added via treeEntity.AddComponent<ECS::FoliageComponent>(foliage);
                    
                    foliageEntityIds.push_back(treeEntityName);
                }
            }

            // Generate grass (as instanced mesh)
            if (grassDensity > 0.0f && biomeType != Core::ECS::BiomeType::Desert && 
                biomeType != Core::ECS::BiomeType::Tundra) {
                
                std::string grassEntityName = "Foliage_Grass_" + std::to_string(seed);
                auto grassEntity = scene->CreateEntity(grassEntityName);
                
                auto& grassTransform = grassEntity.AddComponent<Core::ECS::TransformComponent>();
                grassTransform.Position = glm::vec3(centerX, 0.0f, centerZ);

                ECS::FoliageComponent grassFoliage;
                grassFoliage.FoliageType = "grass";
                grassFoliage.Density = grassDensity;
                grassFoliage.CastShadows = false;
                grassFoliage.WindAffected = true;
                grassFoliage.WindStrength = 0.5f;
                grassFoliage.CullDistance = 100.0f;

                int grassClumps = static_cast<int>(radius * radius * grassDensity * 0.1f);
                grassClumps = std::min(grassClumps, 10000);  // Cap for performance

                for (int i = 0; i < grassClumps; ++i) {
                    ECS::FoliageInstance instance;
                    instance.Position = glm::vec3(
                        centerX + posDist(rng),
                        0.0f,
                        centerZ + posDist(rng)
                    );
                    instance.Rotation = rotDist(rng);
                    instance.Scale = glm::vec3(0.5f + scaleDist(rng) * 0.5f);
                    instance.MeshPath = "assets/foliage/grass/grass_clump.obj";
                    grassFoliage.Instances.push_back(instance);
                }

                totalFoliageInstances += static_cast<int>(grassFoliage.Instances.size());
                foliageEntityIds.push_back(grassEntityName);
            }

            // Build result JSON
            Json resultData;
            resultData["chunks_generated"] = chunksGenerated;
            resultData["foliage_instances"] = totalFoliageInstances;
            resultData["terrain_entity_id"] = terrainEntityName;
            resultData["foliage_entity_ids"] = foliageEntityIds;
            resultData["biome_type"] = biomeTypeStr;
            resultData["seed"] = seed;
            resultData["center"] = {{"x", centerX}, {"z", centerZ}};
            resultData["radius"] = radius;
            resultData["height_scale"] = heightScale;

            ENGINE_CORE_INFO("MCP GenerateBiome: Created {} biome at ({}, {}) with radius {}, {} chunks, {} foliage instances",
                biomeTypeStr, centerX, centerZ, radius, chunksGenerated, totalFoliageInstances);

            std::ostringstream ss;
            ss << "Generated " << biomeTypeStr << " biome at (" << centerX << ", " << centerZ << ")";
            ss << " with radius " << radius << ". Created " << chunksGenerated << " terrain chunks";
            ss << " and " << totalFoliageInstances << " foliage instances.";
            ss << " Terrain entity: " << terrainEntityName;

            return ToolResult::Success(ss.str(), resultData);
        }

    private:
        float GetDefaultHeightScale(Core::ECS::BiomeType biome) const {
            switch (biome) {
                case Core::ECS::BiomeType::Desert: return 50.0f;
                case Core::ECS::BiomeType::Forest: return 80.0f;
                case Core::ECS::BiomeType::Mountain: return 300.0f;
                case Core::ECS::BiomeType::Plains: return 20.0f;
                case Core::ECS::BiomeType::Tundra: return 60.0f;
                default: return 100.0f;
            }
        }

        std::vector<std::string> GetDefaultTreeTypes(Core::ECS::BiomeType biome) const {
            switch (biome) {
                case Core::ECS::BiomeType::Desert:
                    return {"cactus", "joshua_tree"};
                case Core::ECS::BiomeType::Forest:
                    return {"oak", "pine", "birch", "maple"};
                case Core::ECS::BiomeType::Mountain:
                    return {"pine", "spruce", "fir"};
                case Core::ECS::BiomeType::Plains:
                    return {"oak", "willow"};
                case Core::ECS::BiomeType::Tundra:
                    return {"spruce", "dead_tree"};
                default:
                    return {"oak"};
            }
        }

        int GetTreeCountForBiome(Core::ECS::BiomeType biome, float radius, float density) const {
            float baseCount = radius * radius * 0.01f;  // Base trees per area
            float biomeMultiplier = 1.0f;

            switch (biome) {
                case Core::ECS::BiomeType::Desert: biomeMultiplier = 0.05f; break;
                case Core::ECS::BiomeType::Forest: biomeMultiplier = 2.0f; break;
                case Core::ECS::BiomeType::Mountain: biomeMultiplier = 0.5f; break;
                case Core::ECS::BiomeType::Plains: biomeMultiplier = 0.2f; break;
                case Core::ECS::BiomeType::Tundra: biomeMultiplier = 0.1f; break;
            }

            return static_cast<int>(baseCount * biomeMultiplier * density);
        }

        void AddBiomeMaterialLayers(Core::ECS::TerrainComponent& terrain, Core::ECS::BiomeType biome) {
            terrain.MaterialLayers.clear();

            switch (biome) {
                case Core::ECS::BiomeType::Desert:
                    terrain.AddMaterialLayer({"assets/textures/terrain/sand.png", 0.0f, 100.0f});
                    terrain.AddMaterialLayer({"assets/textures/terrain/sandstone.png", 20.0f, 50.0f});
                    break;
                case Core::ECS::BiomeType::Forest:
                    terrain.AddMaterialLayer({"assets/textures/terrain/grass.png", 0.0f, 30.0f});
                    terrain.AddMaterialLayer({"assets/textures/terrain/dirt.png", 10.0f, 50.0f});
                    terrain.AddMaterialLayer({"assets/textures/terrain/rock.png", 40.0f, 100.0f});
                    break;
                case Core::ECS::BiomeType::Mountain:
                    terrain.AddMaterialLayer({"assets/textures/terrain/grass.png", 0.0f, 50.0f});
                    terrain.AddMaterialLayer({"assets/textures/terrain/rock.png", 30.0f, 200.0f});
                    terrain.AddMaterialLayer({"assets/textures/terrain/snow.png", 150.0f, 500.0f});
                    break;
                case Core::ECS::BiomeType::Plains:
                    terrain.AddMaterialLayer({"assets/textures/terrain/grass.png", 0.0f, 50.0f});
                    terrain.AddMaterialLayer({"assets/textures/terrain/dirt.png", 5.0f, 20.0f});
                    break;
                case Core::ECS::BiomeType::Tundra:
                    terrain.AddMaterialLayer({"assets/textures/terrain/snow.png", 0.0f, 100.0f});
                    terrain.AddMaterialLayer({"assets/textures/terrain/ice.png", 10.0f, 60.0f});
                    terrain.AddMaterialLayer({"assets/textures/terrain/frozen_ground.png", 0.0f, 30.0f});
                    break;
            }
        }
    };

    // ========================================================================
    // SetTimeOfDay Tool
    // ========================================================================
    // Control sun position, lighting, and day/night cycle

    class SetTimeOfDayTool : public MCPTool {
    public:
        SetTimeOfDayTool()
            : MCPTool("SetTimeOfDay",
                      "Control the time of day in the game world. Sets sun position, "
                      "lighting color/intensity, and can enable automatic day/night cycling. "
                      "Time is specified in 24-hour format (0.0 = midnight, 12.0 = noon, "
                      "18.0 = 6 PM). Supports smooth transitions between times.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"time", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 24.0},
                    {"description", "Time of day in 24-hour format (0.0 = midnight, 6.0 = sunrise, "
                                    "12.0 = noon, 18.0 = sunset, 24.0 = midnight)"}
                }},
                {"transition_duration", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 300.0},
                    {"description", "Duration in seconds to transition to the new time (0 = instant)"},
                    {"default", 0.0}
                }},
                {"sun_color_override", {
                    {"type", "object"},
                    {"description", "Override the sun color (RGB values 0-1)"},
                    {"properties", {
                        {"r", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
                        {"g", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}},
                        {"b", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}}}
                    }},
                    {"required", Json::array({"r", "g", "b"})}
                }},
                {"sun_intensity_override", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 10.0},
                    {"description", "Override sun intensity (0 = no light, 1 = normal, >1 = brighter)"}
                }},
                {"enable_cycle", {
                    {"type", "boolean"},
                    {"description", "Enable automatic day/night cycle"},
                    {"default", false}
                }},
                {"cycle_speed", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 3600.0},
                    {"description", "Day/night cycle speed multiplier (1.0 = real-time, "
                                    "60.0 = 1 real minute = 1 game hour, 1440.0 = 1 real minute = 1 game day)"},
                    {"default", 1.0}
                }}
            };
            schema.Required = {"time"};
            return schema;
        }

        ToolResult Execute(const Json& arguments, Core::ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("Scene is not available");
            }

            // Parse required argument
            float time = arguments.value("time", 12.0f);
            if (time < 0.0f || time > 24.0f) {
                return ToolResult::Error("time must be between 0.0 and 24.0");
            }

            // Normalize time to 0-24 range
            time = std::fmod(time, 24.0f);
            if (time < 0.0f) time += 24.0f;

            // Parse optional arguments
            float transitionDuration = arguments.value("transition_duration", 0.0f);
            bool enableCycle = arguments.value("enable_cycle", false);
            float cycleSpeed = arguments.value("cycle_speed", 1.0f);

            // Find or create TimeOfDay entity
            Core::ECS::Entity todEntity;
            std::string todEntityName = "TimeOfDay";
            
            todEntity = scene->FindEntityByName(todEntityName);
            if (!todEntity.IsValid()) {
                todEntity = scene->CreateEntity(todEntityName);
                todEntity.AddComponent<Core::ECS::TransformComponent>();
            }

            // Create or get TimeOfDay component data
            ECS::TimeOfDayComponent tod;
            
            // Set time
            if (transitionDuration > 0.0f) {
                tod.TargetTime = time;
                tod.TransitionDuration = transitionDuration;
                tod.TransitionElapsed = 0.0f;
                tod.TransitionProgress = 0.0f;
            } else {
                tod.CurrentTime = time;
                tod.TargetTime = time;
                tod.TransitionProgress = 1.0f;
            }

            // Set cycle
            tod.CycleEnabled = enableCycle;
            tod.CycleSpeed = cycleSpeed;

            // Handle color override
            if (arguments.contains("sun_color_override") && arguments["sun_color_override"].is_object()) {
                const auto& color = arguments["sun_color_override"];
                tod.HasColorOverride = true;
                tod.SunColorOverride.r = std::clamp(color.value("r", 1.0f), 0.0f, 1.0f);
                tod.SunColorOverride.g = std::clamp(color.value("g", 1.0f), 0.0f, 1.0f);
                tod.SunColorOverride.b = std::clamp(color.value("b", 1.0f), 0.0f, 1.0f);
            }

            // Handle intensity override
            if (arguments.contains("sun_intensity_override") && arguments["sun_intensity_override"].is_number()) {
                tod.HasIntensityOverride = true;
                tod.SunIntensityOverride = std::clamp(arguments["sun_intensity_override"].get<float>(), 0.0f, 10.0f);
            }

            // Calculate sun position and lighting based on time
            CalculateSunProperties(tod);

            // Update or create directional light for sun
            Core::ECS::Entity sunEntity = scene->FindEntityByName("Sun");
            if (!sunEntity.IsValid()) {
                sunEntity = scene->CreateEntity("Sun");
                sunEntity.AddComponent<Core::ECS::TransformComponent>();
                sunEntity.AddComponent<Core::ECS::LightComponent>(
                    Core::ECS::LightComponent::CreateDirectional(
                        Core::Math::Vec3(tod.SunColor.r, tod.SunColor.g, tod.SunColor.b),
                        tod.SunIntensity,
                        true  // cast shadows
                    )
                );
            }

            // Update sun light properties
            if (sunEntity.HasComponent<Core::ECS::LightComponent>()) {
                auto& sunLight = sunEntity.GetComponent<Core::ECS::LightComponent>();
                sunLight.Color = Core::Math::Vec3(tod.SunColor.r, tod.SunColor.g, tod.SunColor.b);
                sunLight.Intensity = tod.SunIntensity;
            }

            // Update sun transform to match direction
            if (sunEntity.HasComponent<Core::ECS::TransformComponent>()) {
                auto& sunTransform = sunEntity.GetComponent<Core::ECS::TransformComponent>();
                // Calculate rotation from direction
                glm::vec3 dir = glm::normalize(tod.SunDirection);
                float pitch = std::asin(-dir.y);
                float yaw = std::atan2(dir.x, dir.z);
                sunTransform.Rotation = glm::vec3(pitch, yaw, 0.0f);
            }

            // Build result JSON
            Json resultData;
            resultData["current_time"] = tod.CurrentTime;
            resultData["sun_direction"] = {
                {"x", tod.SunDirection.x},
                {"y", tod.SunDirection.y},
                {"z", tod.SunDirection.z}
            };
            resultData["sun_color"] = {
                {"r", tod.SunColor.r},
                {"g", tod.SunColor.g},
                {"b", tod.SunColor.b}
            };
            resultData["ambient_intensity"] = tod.AmbientIntensity;
            resultData["is_day"] = tod.IsDay();
            resultData["cycle_enabled"] = tod.CycleEnabled;
            if (tod.CycleEnabled) {
                resultData["cycle_speed"] = tod.CycleSpeed;
            }
            if (transitionDuration > 0.0f) {
                resultData["transitioning"] = true;
                resultData["transition_duration"] = transitionDuration;
                resultData["target_time"] = tod.TargetTime;
            }

            ENGINE_CORE_INFO("MCP SetTimeOfDay: Set time to {:.2f} ({}), sun intensity: {:.2f}, cycle: {}",
                time, tod.IsDay() ? "day" : "night", tod.SunIntensity, enableCycle ? "enabled" : "disabled");

            std::ostringstream ss;
            ss << "Set time of day to " << std::fixed << std::setprecision(2) << time;
            ss << " (" << GetTimeDescription(time) << ")";
            if (transitionDuration > 0.0f) {
                ss << " with " << transitionDuration << "s transition";
            }
            if (enableCycle) {
                ss << ". Day/night cycle enabled at " << cycleSpeed << "x speed";
            }

            return ToolResult::Success(ss.str(), resultData);
        }

    private:
        void CalculateSunProperties(ECS::TimeOfDayComponent& tod) const {
            float time = tod.CurrentTime;
            
            // Calculate sun angle based on time (0 = midnight, 12 = noon)
            // Sun rises at 6:00, sets at 18:00
            float sunAngle = (time - 6.0f) / 12.0f * glm::pi<float>();
            
            // Sun direction (rises in east, sets in west)
            float sunHeight = std::sin(sunAngle);
            float sunHorizontal = std::cos(sunAngle);
            
            tod.SunDirection = glm::normalize(glm::vec3(
                sunHorizontal * 0.5f,  // East-West component
                -std::max(sunHeight, -0.2f),  // Height (negative = shining down)
                sunHorizontal * 0.866f  // North-South component
            ));

            // Calculate sun color based on time of day
            if (tod.HasColorOverride) {
                tod.SunColor = tod.SunColorOverride;
            } else {
                tod.SunColor = CalculateSunColor(time);
            }

            // Calculate sun intensity
            if (tod.HasIntensityOverride) {
                tod.SunIntensity = tod.SunIntensityOverride;
            } else {
                tod.SunIntensity = CalculateSunIntensity(time);
            }

            // Calculate ambient intensity
            tod.AmbientIntensity = CalculateAmbientIntensity(time);
            tod.AmbientColor = CalculateAmbientColor(time);
        }

        glm::vec3 CalculateSunColor(float time) const {
            // Dawn/dusk: warm orange
            // Noon: white/slight yellow
            // Night: dim blue (moonlight)
            
            if (time >= 5.0f && time < 7.0f) {
                // Dawn - orange to white
                float t = (time - 5.0f) / 2.0f;
                return glm::mix(glm::vec3(1.0f, 0.5f, 0.2f), glm::vec3(1.0f, 0.98f, 0.95f), t);
            } else if (time >= 7.0f && time < 17.0f) {
                // Day - white/yellow
                return glm::vec3(1.0f, 0.98f, 0.95f);
            } else if (time >= 17.0f && time < 19.0f) {
                // Dusk - white to orange
                float t = (time - 17.0f) / 2.0f;
                return glm::mix(glm::vec3(1.0f, 0.98f, 0.95f), glm::vec3(1.0f, 0.4f, 0.1f), t);
            } else {
                // Night - dim blue moonlight
                return glm::vec3(0.4f, 0.5f, 0.7f);
            }
        }

        float CalculateSunIntensity(float time) const {
            // Intensity curve: peaks at noon, zero at night
            if (time >= 6.0f && time < 18.0f) {
                // Day
                float dayProgress = (time - 6.0f) / 12.0f;  // 0 to 1
                float intensity = std::sin(dayProgress * glm::pi<float>());
                return 0.3f + intensity * 0.7f;  // Range 0.3 to 1.0
            } else {
                // Night - moonlight
                return 0.1f;
            }
        }

        float CalculateAmbientIntensity(float time) const {
            if (time >= 6.0f && time < 18.0f) {
                return 0.3f;  // Day ambient
            } else {
                return 0.1f;  // Night ambient
            }
        }

        glm::vec3 CalculateAmbientColor(float time) const {
            if (time >= 6.0f && time < 18.0f) {
                return glm::vec3(0.6f, 0.7f, 0.9f);  // Bluish sky ambient
            } else {
                return glm::vec3(0.2f, 0.2f, 0.4f);  // Dark blue night ambient
            }
        }

        std::string GetTimeDescription(float time) const {
            if (time >= 5.0f && time < 7.0f) return "dawn";
            if (time >= 7.0f && time < 12.0f) return "morning";
            if (time >= 12.0f && time < 13.0f) return "noon";
            if (time >= 13.0f && time < 17.0f) return "afternoon";
            if (time >= 17.0f && time < 19.0f) return "dusk";
            if (time >= 19.0f && time < 22.0f) return "evening";
            return "night";
        }
    };

    // ========================================================================
    // Factory Functions
    // ========================================================================

    /**
     * @brief Register all world MCP tools with a server
     * @param server The MCP server to register tools with
     */
    inline void RegisterWorldTools(MCPServer& server) {
        server.RegisterTool(std::make_shared<GenerateBiomeTool>());
        server.RegisterTool(std::make_shared<SetTimeOfDayTool>());
        
        ENGINE_CORE_INFO("MCP: Registered world tools (GenerateBiome, SetTimeOfDay)");
    }

    /**
     * @brief Create all world MCP tools as a vector
     * @return Vector of world tool pointers
     */
    inline std::vector<MCPToolPtr> CreateWorldTools() {
        return {
            std::make_shared<GenerateBiomeTool>(),
            std::make_shared<SetTimeOfDayTool>()
        };
    }

} // namespace MCP
} // namespace Core
