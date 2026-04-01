#pragma once

// Auto-Level Designer
// Translates abstract user prompts into sequences of MCP tool calls
// Enables AI agents to create complex game levels from high-level descriptions

#include "MCPTool.h"
#include "MCPTypes.h"
#include "ActionValidator.h"
#include "SceneSerialization.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/Components.h"
#include "Core/Log.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <random>
#include <regex>
#include <memory>
#include <chrono>

namespace Core {
namespace MCP {

    // ============================================================================
    // Design Intent Structures
    // ============================================================================

    // Represents an element to be placed in the level
    struct LevelElement {
        std::string Type;           // "tree", "rock", "light", "building", etc.
        std::string Template;       // MCP template to use (empty, mesh, light, etc.)
        glm::vec3 Position{0.0f};
        glm::vec3 Rotation{0.0f};   // Euler angles in degrees
        glm::vec3 Scale{1.0f};
        Json CustomProperties;      // Additional component properties
        std::string Name;           // Entity name
        int Priority = 0;           // Spawn priority (higher = sooner)
    };

    // Represents a zone or region in the level
    struct LevelZone {
        std::string Name;
        std::string Type;           // "spawn", "combat", "safe", "ambient"
        glm::vec3 Center{0.0f};
        glm::vec3 Extents{10.0f};   // Half-extents of the zone
        Json Properties;            // Zone-specific properties
    };

    // Represents the full design intent parsed from a prompt
    struct DesignIntent {
        std::string RawPrompt;
        std::string SceneType;      // "forest", "dungeon", "city", etc.
        std::vector<LevelElement> Elements;
        std::vector<LevelZone> Zones;
        Json GlobalSettings;        // Lighting, weather, time of day
        std::vector<std::string> ParseWarnings;
        bool IsValid = false;
    };

    // Result of an MCP tool operation
    struct ToolCallResult {
        std::string ToolName;
        Json Arguments;
        Json Result;
        bool Success;
        std::string Error;
        double ExecutionTimeMs;
    };

    // Result of the entire design process
    struct DesignResult {
        bool Success;
        std::string Summary;
        std::vector<ToolCallResult> ToolCalls;
        std::vector<uint32_t> CreatedEntityIds;
        double TotalTimeMs;
        std::vector<std::string> Warnings;
        std::vector<std::string> Errors;
    };

    // ============================================================================
    // Level Templates / Presets
    // ============================================================================

    struct ElementPreset {
        std::string Template;       // MCP template
        Json DefaultComponents;     // Default component configuration
        glm::vec3 DefaultScale{1.0f};
        std::vector<std::string> Variations;  // Mesh path variations
    };

    struct ScenePreset {
        std::string Name;
        std::string Description;
        Json DefaultLighting;
        std::vector<std::string> SuggestedElements;
        glm::vec3 DefaultBounds{100.0f, 50.0f, 100.0f};
    };

    // ============================================================================
    // Prompt Parser
    // ============================================================================

    class PromptParser {
    public:
        PromptParser() {
            InitializePatterns();
        }

        DesignIntent Parse(const std::string& prompt) {
            DesignIntent intent;
            intent.RawPrompt = prompt;

            // Convert to lowercase for easier parsing
            std::string lowerPrompt = ToLower(prompt);

            // Extract scene type
            intent.SceneType = ExtractSceneType(lowerPrompt);

            // Extract quantity patterns (e.g., "10 trees", "a few lights")
            ExtractQuantityPatterns(lowerPrompt, intent);

            // Extract spatial patterns (e.g., "in a circle", "randomly scattered")
            ExtractSpatialPatterns(lowerPrompt, intent);

            // Extract lighting/atmosphere
            ExtractAtmosphere(lowerPrompt, intent);

            // Validate the intent
            intent.IsValid = ValidateIntent(intent);

            return intent;
        }

    private:
        std::unordered_map<std::string, std::string> m_SceneTypeKeywords;
        std::unordered_map<std::string, std::string> m_ElementKeywords;
        std::unordered_map<std::string, int> m_QuantityWords;

        void InitializePatterns() {
            // Scene type keywords
            m_SceneTypeKeywords = {
                {"forest", "forest"}, {"woods", "forest"}, {"jungle", "forest"},
                {"dungeon", "dungeon"}, {"cave", "dungeon"}, {"underground", "dungeon"},
                {"city", "urban"}, {"town", "urban"}, {"village", "urban"},
                {"desert", "desert"}, {"wasteland", "desert"},
                {"space", "space"}, {"sci-fi", "space"}, {"futuristic", "space"},
                {"medieval", "medieval"}, {"castle", "medieval"},
                {"beach", "coastal"}, {"ocean", "coastal"}, {"island", "coastal"},
                {"mountain", "mountain"}, {"hills", "mountain"},
                {"indoor", "interior"}, {"room", "interior"}, {"building", "interior"}
            };

            // Element type keywords
            m_ElementKeywords = {
                {"tree", "tree"}, {"trees", "tree"}, {"oak", "tree"}, {"pine", "tree"},
                {"rock", "rock"}, {"rocks", "rock"}, {"boulder", "rock"}, {"stone", "rock"},
                {"light", "light"}, {"lights", "light"}, {"lamp", "light"}, {"torch", "light"},
                {"building", "building"}, {"house", "building"}, {"structure", "building"},
                {"enemy", "enemy"}, {"enemies", "enemy"}, {"monster", "enemy"},
                {"npc", "npc"}, {"character", "npc"}, {"person", "npc"},
                {"box", "box"}, {"crate", "box"}, {"cube", "box"},
                {"sphere", "sphere"}, {"ball", "sphere"},
                {"platform", "platform"}, {"floor", "platform"},
                {"wall", "wall"}, {"barrier", "wall"},
                {"water", "water"}, {"lake", "water"}, {"pond", "water"},
                {"trigger", "trigger"}, {"zone", "trigger"}, {"area", "trigger"}
            };

            // Quantity words
            m_QuantityWords = {
                {"a", 1}, {"an", 1}, {"one", 1},
                {"two", 2}, {"couple", 2}, {"pair", 2},
                {"three", 3}, {"few", 3},
                {"four", 4}, {"five", 5}, {"six", 6},
                {"seven", 7}, {"eight", 8}, {"nine", 9}, {"ten", 10},
                {"dozen", 12}, {"several", 5},
                {"many", 15}, {"lots", 20}, {"numerous", 25},
                {"hundred", 100}
            };
        }

        std::string ToLower(const std::string& str) {
            std::string result = str;
            std::transform(result.begin(), result.end(), result.begin(), ::tolower);
            return result;
        }

        std::string ExtractSceneType(const std::string& prompt) {
            for (const auto& [keyword, sceneType] : m_SceneTypeKeywords) {
                if (prompt.find(keyword) != std::string::npos) {
                    return sceneType;
                }
            }
            return "generic";
        }

        void ExtractQuantityPatterns(const std::string& prompt, DesignIntent& intent) {
            // Pattern: "<number/word> <element type>"
            std::regex numericPattern(R"((\d+)\s+(\w+))");
            std::regex wordPattern(R"((a|an|one|two|three|four|five|six|seven|eight|nine|ten|dozen|several|few|many|lots|numerous|hundred)\s+(\w+))");

            std::smatch match;
            std::string::const_iterator searchStart = prompt.cbegin();

            // Search for numeric patterns
            while (std::regex_search(searchStart, prompt.cend(), match, numericPattern)) {
                int count = std::stoi(match[1].str());
                std::string elementWord = match[2].str();

                auto elementIt = m_ElementKeywords.find(elementWord);
                if (elementIt != m_ElementKeywords.end()) {
                    AddElements(intent, elementIt->second, count);
                }

                searchStart = match.suffix().first;
            }

            // Search for word quantity patterns
            searchStart = prompt.cbegin();
            while (std::regex_search(searchStart, prompt.cend(), match, wordPattern)) {
                std::string quantityWord = match[1].str();
                std::string elementWord = match[2].str();

                auto quantityIt = m_QuantityWords.find(quantityWord);
                auto elementIt = m_ElementKeywords.find(elementWord);

                if (quantityIt != m_QuantityWords.end() && elementIt != m_ElementKeywords.end()) {
                    int count = quantityIt->second;
                    AddElements(intent, elementIt->second, count);
                }

                searchStart = match.suffix().first;
            }

            // Check for singular mentions without quantities
            for (const auto& [keyword, elementType] : m_ElementKeywords) {
                if (prompt.find(keyword) != std::string::npos) {
                    // Check if we already added elements of this type
                    bool alreadyAdded = false;
                    for (const auto& elem : intent.Elements) {
                        if (elem.Type == elementType) {
                            alreadyAdded = true;
                            break;
                        }
                    }
                    if (!alreadyAdded) {
                        AddElements(intent, elementType, 1);
                    }
                }
            }
        }

        void AddElements(DesignIntent& intent, const std::string& type, int count) {
            for (int i = 0; i < count; ++i) {
                LevelElement elem;
                elem.Type = type;
                elem.Name = type + "_" + std::to_string(i + 1);

                // Set template based on type
                if (type == "light") {
                    elem.Template = "light";
                } else if (type == "trigger" || type == "zone") {
                    elem.Template = "trigger";
                } else if (type == "box" || type == "crate") {
                    elem.Template = "physicsBox";
                } else if (type == "sphere" || type == "ball") {
                    elem.Template = "physicsSphere";
                } else {
                    elem.Template = "mesh";
                }

                intent.Elements.push_back(elem);
            }
        }

        void ExtractSpatialPatterns(const std::string& prompt, DesignIntent& intent) {
            // Check for spatial arrangement keywords
            if (prompt.find("circle") != std::string::npos || 
                prompt.find("ring") != std::string::npos) {
                intent.GlobalSettings["arrangement"] = "circle";
            } else if (prompt.find("grid") != std::string::npos ||
                       prompt.find("rows") != std::string::npos) {
                intent.GlobalSettings["arrangement"] = "grid";
            } else if (prompt.find("random") != std::string::npos ||
                       prompt.find("scattered") != std::string::npos) {
                intent.GlobalSettings["arrangement"] = "random";
            } else if (prompt.find("line") != std::string::npos ||
                       prompt.find("row") != std::string::npos) {
                intent.GlobalSettings["arrangement"] = "line";
            } else {
                intent.GlobalSettings["arrangement"] = "random";  // Default
            }

            // Extract bounds/area
            std::regex boundsPattern(R"((\d+)\s*(?:x|by)\s*(\d+)\s*(?:x|by)?\s*(\d+)?(?:\s*(?:units?|meters?|m)?)?)");
            std::smatch match;
            if (std::regex_search(prompt, match, boundsPattern)) {
                float x = std::stof(match[1].str());
                float y = match[3].matched ? std::stof(match[3].str()) : 10.0f;
                float z = match[2].matched ? std::stof(match[2].str()) : x;
                intent.GlobalSettings["bounds"] = {{"x", x}, {"y", y}, {"z", z}};
            }

            // Check for specific area mentions
            if (prompt.find("large") != std::string::npos) {
                intent.GlobalSettings["scale"] = "large";
            } else if (prompt.find("small") != std::string::npos) {
                intent.GlobalSettings["scale"] = "small";
            } else if (prompt.find("huge") != std::string::npos || 
                       prompt.find("massive") != std::string::npos) {
                intent.GlobalSettings["scale"] = "huge";
            }
        }

        void ExtractAtmosphere(const std::string& prompt, DesignIntent& intent) {
            // Time of day
            if (prompt.find("night") != std::string::npos || 
                prompt.find("dark") != std::string::npos) {
                intent.GlobalSettings["timeOfDay"] = "night";
                intent.GlobalSettings["ambientLight"] = 0.1f;
            } else if (prompt.find("sunset") != std::string::npos ||
                       prompt.find("dusk") != std::string::npos) {
                intent.GlobalSettings["timeOfDay"] = "sunset";
                intent.GlobalSettings["ambientLight"] = 0.4f;
            } else if (prompt.find("morning") != std::string::npos ||
                       prompt.find("dawn") != std::string::npos) {
                intent.GlobalSettings["timeOfDay"] = "morning";
                intent.GlobalSettings["ambientLight"] = 0.6f;
            } else {
                intent.GlobalSettings["timeOfDay"] = "day";
                intent.GlobalSettings["ambientLight"] = 1.0f;
            }

            // Weather/atmosphere
            if (prompt.find("foggy") != std::string::npos || 
                prompt.find("misty") != std::string::npos) {
                intent.GlobalSettings["fog"] = true;
                intent.GlobalSettings["fogDensity"] = 0.05f;
            }

            // Mood
            if (prompt.find("spooky") != std::string::npos ||
                prompt.find("scary") != std::string::npos ||
                prompt.find("horror") != std::string::npos) {
                intent.GlobalSettings["mood"] = "horror";
            } else if (prompt.find("peaceful") != std::string::npos ||
                       prompt.find("calm") != std::string::npos ||
                       prompt.find("serene") != std::string::npos) {
                intent.GlobalSettings["mood"] = "peaceful";
            } else if (prompt.find("action") != std::string::npos ||
                       prompt.find("combat") != std::string::npos ||
                       prompt.find("battle") != std::string::npos) {
                intent.GlobalSettings["mood"] = "action";
            }
        }

        bool ValidateIntent(DesignIntent& intent) {
            if (intent.Elements.empty() && intent.Zones.empty()) {
                intent.ParseWarnings.push_back("No elements or zones could be parsed from prompt");
                return false;
            }
            return true;
        }
    };

    // ============================================================================
    // Position Generator
    // ============================================================================

    class PositionGenerator {
    public:
        PositionGenerator() : m_RNG(std::random_device{}()) {}

        void SetSeed(uint32_t seed) {
            m_RNG.seed(seed);
        }

        // Generate positions based on arrangement type
        std::vector<glm::vec3> GeneratePositions(
            int count,
            const std::string& arrangement,
            const glm::vec3& bounds,
            const glm::vec3& center = glm::vec3(0.0f)) 
        {
            std::vector<glm::vec3> positions;

            if (arrangement == "circle") {
                positions = GenerateCirclePositions(count, bounds, center);
            } else if (arrangement == "grid") {
                positions = GenerateGridPositions(count, bounds, center);
            } else if (arrangement == "line") {
                positions = GenerateLinePositions(count, bounds, center);
            } else {
                positions = GenerateRandomPositions(count, bounds, center);
            }

            return positions;
        }

        glm::vec3 GenerateRandomPosition(const glm::vec3& bounds, const glm::vec3& center) {
            std::uniform_real_distribution<float> distX(-bounds.x, bounds.x);
            std::uniform_real_distribution<float> distZ(-bounds.z, bounds.z);
            return center + glm::vec3(distX(m_RNG), 0.0f, distZ(m_RNG));
        }

        float GenerateRandomRotationY() {
            std::uniform_real_distribution<float> dist(0.0f, 360.0f);
            return dist(m_RNG);
        }

        float GenerateRandomScale(float base, float variance) {
            std::uniform_real_distribution<float> dist(base - variance, base + variance);
            return std::max(0.1f, dist(m_RNG));
        }

    private:
        std::mt19937 m_RNG;

        std::vector<glm::vec3> GenerateCirclePositions(
            int count, const glm::vec3& bounds, const glm::vec3& center) 
        {
            std::vector<glm::vec3> positions;
            float radius = std::min(bounds.x, bounds.z) * 0.8f;
            float angleStep = 2.0f * 3.14159f / static_cast<float>(count);

            for (int i = 0; i < count; ++i) {
                float angle = angleStep * static_cast<float>(i);
                float x = center.x + radius * std::cos(angle);
                float z = center.z + radius * std::sin(angle);
                positions.push_back(glm::vec3(x, center.y, z));
            }
            return positions;
        }

        std::vector<glm::vec3> GenerateGridPositions(
            int count, const glm::vec3& bounds, const glm::vec3& center) 
        {
            std::vector<glm::vec3> positions;
            int gridSize = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count))));
            float spacingX = (bounds.x * 2.0f) / static_cast<float>(gridSize);
            float spacingZ = (bounds.z * 2.0f) / static_cast<float>(gridSize);

            int generated = 0;
            for (int row = 0; row < gridSize && generated < count; ++row) {
                for (int col = 0; col < gridSize && generated < count; ++col) {
                    float x = center.x - bounds.x + spacingX * (static_cast<float>(col) + 0.5f);
                    float z = center.z - bounds.z + spacingZ * (static_cast<float>(row) + 0.5f);
                    positions.push_back(glm::vec3(x, center.y, z));
                    ++generated;
                }
            }
            return positions;
        }

        std::vector<glm::vec3> GenerateLinePositions(
            int count, const glm::vec3& bounds, const glm::vec3& center) 
        {
            std::vector<glm::vec3> positions;
            float spacing = (bounds.x * 2.0f) / static_cast<float>(std::max(1, count - 1));

            for (int i = 0; i < count; ++i) {
                float x = center.x - bounds.x + spacing * static_cast<float>(i);
                positions.push_back(glm::vec3(x, center.y, center.z));
            }
            return positions;
        }

        std::vector<glm::vec3> GenerateRandomPositions(
            int count, const glm::vec3& bounds, const glm::vec3& center) 
        {
            std::vector<glm::vec3> positions;
            std::uniform_real_distribution<float> distX(-bounds.x, bounds.x);
            std::uniform_real_distribution<float> distZ(-bounds.z, bounds.z);

            for (int i = 0; i < count; ++i) {
                float x = center.x + distX(m_RNG);
                float z = center.z + distZ(m_RNG);
                positions.push_back(glm::vec3(x, center.y, z));
            }
            return positions;
        }
    };

    // ============================================================================
    // Element Factory
    // ============================================================================

    class ElementFactory {
    public:
        ElementFactory() {
            InitializePresets();
        }

        // Get MCP SpawnEntity arguments for an element
        Json CreateSpawnArguments(const LevelElement& element) {
            Json args;
            args["name"] = element.Name;
            args["template"] = element.Template;

            // Transform
            args["transform"] = {
                {"position", {{"x", element.Position.x}, {"y", element.Position.y}, {"z", element.Position.z}}},
                {"rotation", {{"pitch", element.Rotation.x}, {"yaw", element.Rotation.y}, {"roll", element.Rotation.z}}},
                {"scale", {{"x", element.Scale.x}, {"y", element.Scale.y}, {"z", element.Scale.z}}}
            };

            // Apply preset-based components
            auto presetIt = m_ElementPresets.find(element.Type);
            if (presetIt != m_ElementPresets.end()) {
                args["components"] = presetIt->second.DefaultComponents;
            }

            // Merge custom properties
            if (!element.CustomProperties.is_null()) {
                if (args.contains("components")) {
                    for (auto& [key, value] : element.CustomProperties.items()) {
                        args["components"][key] = value;
                    }
                } else {
                    args["components"] = element.CustomProperties;
                }
            }

            return args;
        }

        // Get default scale for element type
        glm::vec3 GetDefaultScale(const std::string& type) {
            auto it = m_ElementPresets.find(type);
            if (it != m_ElementPresets.end()) {
                return it->second.DefaultScale;
            }
            return glm::vec3(1.0f);
        }

        // Check if a type has a preset
        bool HasPreset(const std::string& type) const {
            return m_ElementPresets.find(type) != m_ElementPresets.end();
        }

    private:
        std::unordered_map<std::string, ElementPreset> m_ElementPresets;

        void InitializePresets() {
            // Tree preset
            m_ElementPresets["tree"] = {
                "mesh",
                {{"mesh", {{"path", "assets/meshes/tree.gltf"}, {"visible", true}, {"castShadows", true}}}},
                glm::vec3(1.0f, 2.0f, 1.0f),
                {"assets/meshes/tree_oak.gltf", "assets/meshes/tree_pine.gltf", "assets/meshes/tree_birch.gltf"}
            };

            // Rock preset
            m_ElementPresets["rock"] = {
                "mesh",
                {{"mesh", {{"path", "assets/meshes/rock.gltf"}, {"visible", true}, {"castShadows", true}}}},
                glm::vec3(1.0f, 1.0f, 1.0f),
                {"assets/meshes/rock_small.gltf", "assets/meshes/rock_large.gltf", "assets/meshes/boulder.gltf"}
            };

            // Light preset
            m_ElementPresets["light"] = {
                "light",
                {{"light", {{"type", "point"}, {"color", {{"r", 1.0f}, {"g", 0.9f}, {"b", 0.8f}}}, 
                           {"intensity", 5.0f}, {"radius", 20.0f}}}},
                glm::vec3(1.0f),
                {}
            };

            // Box/Crate preset
            m_ElementPresets["box"] = {
                "physicsBox",
                {{"mesh", {{"path", "assets/meshes/crate.gltf"}, {"visible", true}}},
                 {"rigidBody", {{"motionType", "dynamic"}, {"mass", 10.0f}}}},
                glm::vec3(1.0f),
                {}
            };

            // Platform preset
            m_ElementPresets["platform"] = {
                "physicsBox",
                {{"mesh", {{"path", "assets/meshes/platform.gltf"}, {"visible", true}}},
                 {"rigidBody", {{"motionType", "static"}}}},
                glm::vec3(5.0f, 0.5f, 5.0f),
                {}
            };

            // Wall preset
            m_ElementPresets["wall"] = {
                "physicsBox",
                {{"mesh", {{"path", "assets/meshes/wall.gltf"}, {"visible", true}}},
                 {"rigidBody", {{"motionType", "static"}}}},
                glm::vec3(0.5f, 3.0f, 5.0f),
                {}
            };

            // Enemy preset
            m_ElementPresets["enemy"] = {
                "mesh",
                {{"mesh", {{"path", "assets/meshes/enemy.gltf"}, {"visible", true}}},
                 {"rigidBody", {{"motionType", "kinematic"}, {"mass", 50.0f}}}},
                glm::vec3(1.0f),
                {}
            };

            // NPC preset
            m_ElementPresets["npc"] = {
                "mesh",
                {{"mesh", {{"path", "assets/meshes/npc.gltf"}, {"visible", true}}},
                 {"rigidBody", {{"motionType", "kinematic"}}}},
                glm::vec3(1.0f),
                {}
            };

            // Trigger zone preset
            m_ElementPresets["trigger"] = {
                "trigger",
                {{"collider", {{"type", "box"}, {"isSensor", true}}}},
                glm::vec3(5.0f),
                {}
            };

            // Sphere preset
            m_ElementPresets["sphere"] = {
                "physicsSphere",
                {{"rigidBody", {{"motionType", "dynamic"}, {"mass", 5.0f}}}},
                glm::vec3(1.0f),
                {}
            };

            // Building preset
            m_ElementPresets["building"] = {
                "mesh",
                {{"mesh", {{"path", "assets/meshes/building.gltf"}, {"visible", true}, {"castShadows", true}}},
                 {"rigidBody", {{"motionType", "static"}}}},
                glm::vec3(10.0f, 15.0f, 10.0f),
                {}
            };

            // Water preset
            m_ElementPresets["water"] = {
                "mesh",
                {{"mesh", {{"path", "assets/meshes/water_plane.gltf"}, {"visible", true}}}},
                glm::vec3(20.0f, 0.1f, 20.0f),
                {}
            };
        }
    };

    // ============================================================================
    // Auto-Level Designer Tool
    // ============================================================================

    class AutoLevelDesignerTool : public MCPTool {
    public:
        AutoLevelDesignerTool()
            : MCPTool("AutoLevelDesigner",
                      "Automatically design and generate game levels from natural language "
                      "prompts. Translates high-level descriptions like 'create a forest with "
                      "10 trees and some lights' into actual spawned entities.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"prompt", {
                    {"type", "string"},
                    {"description", "Natural language description of the level to create. "
                                   "Examples: 'Create a forest scene with 10 trees', "
                                   "'Make a dark dungeon with scattered lights', "
                                   "'Build a city block with buildings arranged in a grid'"}
                }},
                {"bounds", {
                    {"type", "object"},
                    {"description", "Level bounds in world units (optional, auto-calculated if not provided)"},
                    {"properties", {
                        {"x", {{"type", "number"}, {"default", 50.0}}},
                        {"y", {{"type", "number"}, {"default", 20.0}}},
                        {"z", {{"type", "number"}, {"default", 50.0}}}
                    }}
                }},
                {"center", {
                    {"type", "object"},
                    {"description", "Center point for level generation"},
                    {"properties", {
                        {"x", {{"type", "number"}, {"default", 0.0}}},
                        {"y", {{"type", "number"}, {"default", 0.0}}},
                        {"z", {{"type", "number"}, {"default", 0.0}}}
                    }}
                }},
                {"seed", {
                    {"type", "integer"},
                    {"description", "Random seed for reproducible generation (optional)"}
                }},
                {"dryRun", {
                    {"type", "boolean"},
                    {"description", "If true, returns the plan without executing it"},
                    {"default", false}
                }},
                {"maxEntities", {
                    {"type", "integer"},
                    {"description", "Maximum entities to spawn (safety limit)"},
                    {"default", 100},
                    {"minimum", 1},
                    {"maximum", 1000}
                }},
                {"scaleVariation", {
                    {"type", "number"},
                    {"description", "Random scale variation factor (0.0-1.0)"},
                    {"default", 0.2}
                }},
                {"rotationVariation", {
                    {"type", "boolean"},
                    {"description", "Apply random Y rotation to elements"},
                    {"default", true}
                }}
            };
            schema.Required = {"prompt"};
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            auto startTime = std::chrono::high_resolution_clock::now();

            if (!scene) {
                return ToolResult::Error("No active scene available");
            }

            // Validate input
            auto& validator = GetGlobalValidator();
            
            // Extract parameters
            std::string prompt = arguments.value("prompt", "");
            if (prompt.empty()) {
                return ToolResult::Error("Prompt is required");
            }

            // Validate prompt length
            if (prompt.length() > validator.GetLimits().MaxStringLength) {
                return ToolResult::Error("Prompt exceeds maximum length");
            }

            bool dryRun = arguments.value("dryRun", false);
            int maxEntities = arguments.value("maxEntities", 100);
            float scaleVariation = arguments.value("scaleVariation", 0.2f);
            bool rotationVariation = arguments.value("rotationVariation", true);

            // Parse bounds
            glm::vec3 bounds(50.0f, 20.0f, 50.0f);
            if (arguments.contains("bounds")) {
                bounds.x = arguments["bounds"].value("x", 50.0f);
                bounds.y = arguments["bounds"].value("y", 20.0f);
                bounds.z = arguments["bounds"].value("z", 50.0f);
            }

            // Parse center
            glm::vec3 center(0.0f);
            if (arguments.contains("center")) {
                center.x = arguments["center"].value("x", 0.0f);
                center.y = arguments["center"].value("y", 0.0f);
                center.z = arguments["center"].value("z", 0.0f);
            }

            // Set random seed if provided
            if (arguments.contains("seed")) {
                m_PositionGenerator.SetSeed(arguments["seed"].get<uint32_t>());
            }

            // Parse the prompt
            DesignIntent intent = m_Parser.Parse(prompt);

            if (!intent.IsValid) {
                Json result;
                result["success"] = false;
                result["error"] = "Failed to parse prompt";
                result["warnings"] = intent.ParseWarnings;
                result["parsedElements"] = static_cast<int>(intent.Elements.size());
                return ToolResult::SuccessJson(result);
            }

            // Apply bounds from settings if specified
            if (intent.GlobalSettings.contains("bounds")) {
                bounds.x = intent.GlobalSettings["bounds"].value("x", bounds.x);
                bounds.y = intent.GlobalSettings["bounds"].value("y", bounds.y);
                bounds.z = intent.GlobalSettings["bounds"].value("z", bounds.z);
            }

            // Apply scale factor
            if (intent.GlobalSettings.contains("scale")) {
                std::string scaleName = intent.GlobalSettings["scale"].get<std::string>();
                if (scaleName == "small") {
                    bounds *= 0.5f;
                } else if (scaleName == "large") {
                    bounds *= 2.0f;
                } else if (scaleName == "huge") {
                    bounds *= 4.0f;
                }
            }

            // Limit number of elements
            if (static_cast<int>(intent.Elements.size()) > maxEntities) {
                intent.Elements.resize(static_cast<size_t>(maxEntities));
                intent.ParseWarnings.push_back("Element count limited to " + std::to_string(maxEntities));
            }

            // Generate positions for elements
            std::string arrangement = intent.GlobalSettings.value("arrangement", "random");
            auto positions = m_PositionGenerator.GeneratePositions(
                static_cast<int>(intent.Elements.size()), arrangement, bounds, center);

            // Assign positions and variations to elements
            for (size_t i = 0; i < intent.Elements.size(); ++i) {
                auto& elem = intent.Elements[i];
                elem.Position = positions[i];

                // Apply rotation variation
                if (rotationVariation) {
                    elem.Rotation.y = m_PositionGenerator.GenerateRandomRotationY();
                }

                // Apply scale with variation
                glm::vec3 baseScale = m_Factory.GetDefaultScale(elem.Type);
                if (scaleVariation > 0.0f) {
                    float scaleFactor = m_PositionGenerator.GenerateRandomScale(1.0f, scaleVariation);
                    elem.Scale = baseScale * scaleFactor;
                } else {
                    elem.Scale = baseScale;
                }
            }

            // Build the design plan
            Json plan = BuildDesignPlan(intent, bounds, center);

            if (dryRun) {
                auto endTime = std::chrono::high_resolution_clock::now();
                double durationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

                Json result;
                result["success"] = true;
                result["dryRun"] = true;
                result["plan"] = plan;
                result["executionTimeMs"] = durationMs;
                return ToolResult::SuccessJson(result);
            }

            // Execute the design
            DesignResult designResult = ExecuteDesign(intent, scene);

            auto endTime = std::chrono::high_resolution_clock::now();
            designResult.TotalTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

            // Build response
            Json result;
            result["success"] = designResult.Success;
            result["summary"] = designResult.Summary;
            result["createdEntities"] = designResult.CreatedEntityIds;
            result["entityCount"] = static_cast<int>(designResult.CreatedEntityIds.size());
            result["toolCallCount"] = static_cast<int>(designResult.ToolCalls.size());
            result["executionTimeMs"] = designResult.TotalTimeMs;
            result["sceneType"] = intent.SceneType;
            result["arrangement"] = arrangement;

            if (!designResult.Warnings.empty()) {
                result["warnings"] = designResult.Warnings;
            }
            if (!designResult.Errors.empty()) {
                result["errors"] = designResult.Errors;
            }

            // Include tool call details
            Json toolCallsJson = Json::array();
            for (const auto& call : designResult.ToolCalls) {
                toolCallsJson.push_back({
                    {"tool", call.ToolName},
                    {"success", call.Success},
                    {"executionTimeMs", call.ExecutionTimeMs}
                });
            }
            result["toolCalls"] = toolCallsJson;

            ENGINE_CORE_INFO("MCP AutoLevelDesigner: Created {} entities in {:.2f}ms",
                            designResult.CreatedEntityIds.size(), designResult.TotalTimeMs);

            return ToolResult::SuccessJson(result);
        }

    private:
        PromptParser m_Parser;
        PositionGenerator m_PositionGenerator;
        ElementFactory m_Factory;

        // Helper to convert degrees to radians
        static glm::quat EulerToQuat(const glm::vec3& eulerDegrees) {
            glm::vec3 radians = glm::radians(eulerDegrees);
            return glm::quat(radians);
        }

        Json BuildDesignPlan(const DesignIntent& intent, const glm::vec3& bounds, const glm::vec3& center) {
            Json plan;
            plan["sceneType"] = intent.SceneType;
            plan["elementCount"] = static_cast<int>(intent.Elements.size());
            plan["bounds"] = {{"x", bounds.x}, {"y", bounds.y}, {"z", bounds.z}};
            plan["center"] = {{"x", center.x}, {"y", center.y}, {"z", center.z}};
            plan["globalSettings"] = intent.GlobalSettings;

            Json elementsJson = Json::array();
            for (const auto& elem : intent.Elements) {
                elementsJson.push_back({
                    {"name", elem.Name},
                    {"type", elem.Type},
                    {"template", elem.Template},
                    {"position", {{"x", elem.Position.x}, {"y", elem.Position.y}, {"z", elem.Position.z}}},
                    {"rotation", {{"x", elem.Rotation.x}, {"y", elem.Rotation.y}, {"z", elem.Rotation.z}}},
                    {"scale", {{"x", elem.Scale.x}, {"y", elem.Scale.y}, {"z", elem.Scale.z}}}
                });
            }
            plan["elements"] = elementsJson;

            if (!intent.ParseWarnings.empty()) {
                plan["parseWarnings"] = intent.ParseWarnings;
            }

            return plan;
        }

        // Spawn a single entity directly into the scene
        std::pair<uint32_t, bool> SpawnElementDirect(const LevelElement& elem, ECS::Scene* scene) {
            auto& registry = scene->GetRegistry();

            // Create entity
            ECS::Entity entity = scene->CreateEntity(elem.Name);
            entt::entity handle = entity.GetHandle();

            // Add NameComponent
            registry.emplace<NameComponent>(handle, NameComponent{elem.Name});

            // Add TransformComponent
            ECS::TransformComponent transform;
            transform.Position = elem.Position;
            transform.Rotation = EulerToQuat(elem.Rotation);
            transform.Scale = elem.Scale;
            registry.emplace<ECS::TransformComponent>(handle, transform);

            // Apply template-based components
            ApplyTemplateComponents(registry, handle, elem);

            return {static_cast<uint32_t>(handle), true};
        }

        void ApplyTemplateComponents(entt::registry& registry, entt::entity handle, const LevelElement& elem) {
            // Apply components based on template type
            if (elem.Template == "light") {
                ECS::LightComponent light;
                light.Type = ECS::LightType::Point;
                light.Color = glm::vec3(1.0f, 0.9f, 0.8f);
                light.Intensity = 5.0f;
                light.Radius = 20.0f;

                // Override with custom properties
                if (elem.CustomProperties.contains("light")) {
                    const auto& lp = elem.CustomProperties["light"];
                    if (lp.contains("type")) {
                        std::string typeStr = lp["type"].get<std::string>();
                        if (typeStr == "directional") light.Type = ECS::LightType::Directional;
                        else if (typeStr == "spot") light.Type = ECS::LightType::Spot;
                    }
                    if (lp.contains("color")) {
                        light.Color.r = lp["color"].value("r", 1.0f);
                        light.Color.g = lp["color"].value("g", 1.0f);
                        light.Color.b = lp["color"].value("b", 1.0f);
                    }
                    if (lp.contains("intensity")) light.Intensity = lp["intensity"].get<float>();
                    if (lp.contains("radius")) light.Radius = lp["radius"].get<float>();
                }

                registry.emplace<ECS::LightComponent>(handle, light);
            }
            else if (elem.Template == "mesh") {
                ECS::MeshComponent mesh;
                mesh.MeshPath = "assets/meshes/default.gltf";
                mesh.Visible = true;
                mesh.CastShadows = true;

                if (elem.CustomProperties.contains("mesh")) {
                    const auto& mp = elem.CustomProperties["mesh"];
                    if (mp.contains("path")) mesh.MeshPath = mp["path"].get<std::string>();
                    if (mp.contains("visible")) mesh.Visible = mp["visible"].get<bool>();
                    if (mp.contains("castShadows")) mesh.CastShadows = mp["castShadows"].get<bool>();
                }

                registry.emplace<ECS::MeshComponent>(handle, mesh);
            }
            else if (elem.Template == "physicsBox" || elem.Template == "physicsSphere") {
                // Add collider
                ECS::ColliderComponent collider;
                if (elem.Template == "physicsBox") {
                    collider.Shape = ECS::ColliderShape::Box;
                    collider.HalfExtents = elem.Scale * 0.5f;
                } else {
                    collider.Shape = ECS::ColliderShape::Sphere;
                    collider.Radius = elem.Scale.x * 0.5f;
                }
                registry.emplace<ECS::ColliderComponent>(handle, collider);

                // Add rigid body
                ECS::RigidBodyComponent rigidBody;
                rigidBody.MotionType = ECS::MotionType::Dynamic;
                rigidBody.Mass = 1.0f;

                if (elem.CustomProperties.contains("rigidBody")) {
                    const auto& rb = elem.CustomProperties["rigidBody"];
                    if (rb.contains("motionType")) {
                        std::string mt = rb["motionType"].get<std::string>();
                        if (mt == "static") rigidBody.MotionType = ECS::MotionType::Static;
                        else if (mt == "kinematic") rigidBody.MotionType = ECS::MotionType::Kinematic;
                    }
                    if (rb.contains("mass")) rigidBody.Mass = rb["mass"].get<float>();
                }

                registry.emplace<ECS::RigidBodyComponent>(handle, rigidBody);

                // Optionally add mesh
                if (elem.CustomProperties.contains("mesh")) {
                    ECS::MeshComponent mesh;
                    const auto& mp = elem.CustomProperties["mesh"];
                    mesh.MeshPath = mp.value("path", "assets/meshes/default.gltf");
                    mesh.Visible = mp.value("visible", true);
                    registry.emplace<ECS::MeshComponent>(handle, mesh);
                }
            }
            else if (elem.Template == "camera") {
                ECS::CameraComponent camera;
                camera.ProjectionType = ECS::ProjectionType::Perspective;
                camera.FieldOfView = 60.0f;
                camera.NearPlane = 0.1f;
                camera.FarPlane = 1000.0f;
                camera.IsActive = false;
                registry.emplace<ECS::CameraComponent>(handle, camera);
            }
            else if (elem.Template == "trigger") {
                ECS::ColliderComponent collider;
                collider.Shape = ECS::ColliderShape::Box;
                collider.HalfExtents = elem.Scale * 0.5f;
                collider.IsSensor = true;
                registry.emplace<ECS::ColliderComponent>(handle, collider);
            }
        }

        DesignResult ExecuteDesign(const DesignIntent& intent, ECS::Scene* scene) {
            DesignResult result;
            result.Success = true;

            // Create each element directly
            for (const auto& elem : intent.Elements) {
                auto callStart = std::chrono::high_resolution_clock::now();

                auto [entityId, success] = SpawnElementDirect(elem, scene);

                auto callEnd = std::chrono::high_resolution_clock::now();
                double callDurationMs = std::chrono::duration<double, std::milli>(callEnd - callStart).count();

                ToolCallResult callResult;
                callResult.ToolName = "DirectSpawn";
                callResult.Arguments = m_Factory.CreateSpawnArguments(elem);
                callResult.ExecutionTimeMs = callDurationMs;

                if (success) {
                    callResult.Success = true;
                    callResult.Result = {{"entityId", entityId}, {"name", elem.Name}};
                    result.CreatedEntityIds.push_back(entityId);
                } else {
                    callResult.Success = false;
                    callResult.Error = "Failed to spawn entity";
                    result.Warnings.push_back("Failed to spawn " + elem.Name);
                }

                result.ToolCalls.push_back(callResult);
            }

            // Create scene lighting based on atmosphere settings
            if (intent.GlobalSettings.contains("timeOfDay")) {
                CreateAtmosphereLighting(intent, scene, result);
            }

            // Build summary
            result.Summary = "Created " + std::to_string(result.CreatedEntityIds.size()) + 
                            " entities for " + intent.SceneType + " scene";

            return result;
        }

        void CreateAtmosphereLighting(const DesignIntent& intent, ECS::Scene* scene, DesignResult& result) {
            std::string timeOfDay = intent.GlobalSettings.value("timeOfDay", "day");
            
            // Create directional light based on time of day
            LevelElement sunLight;
            sunLight.Name = "SunLight";
            sunLight.Type = "light";
            sunLight.Template = "light";
            sunLight.Position = glm::vec3(0.0f, 100.0f, 0.0f);

            // Set light properties based on time
            Json lightProps;
            lightProps["light"] = Json::object();
            lightProps["light"]["type"] = "directional";

            if (timeOfDay == "night") {
                lightProps["light"]["color"] = {{"r", 0.2f}, {"g", 0.2f}, {"b", 0.4f}};
                lightProps["light"]["intensity"] = 0.3f;
                sunLight.Rotation = glm::vec3(-30.0f, 0.0f, 0.0f);
            } else if (timeOfDay == "sunset") {
                lightProps["light"]["color"] = {{"r", 1.0f}, {"g", 0.6f}, {"b", 0.3f}};
                lightProps["light"]["intensity"] = 0.8f;
                sunLight.Rotation = glm::vec3(-15.0f, 45.0f, 0.0f);
            } else if (timeOfDay == "morning") {
                lightProps["light"]["color"] = {{"r", 1.0f}, {"g", 0.9f}, {"b", 0.7f}};
                lightProps["light"]["intensity"] = 0.7f;
                sunLight.Rotation = glm::vec3(-25.0f, -45.0f, 0.0f);
            } else {
                lightProps["light"]["color"] = {{"r", 1.0f}, {"g", 1.0f}, {"b", 1.0f}};
                lightProps["light"]["intensity"] = 1.0f;
                sunLight.Rotation = glm::vec3(-45.0f, 0.0f, 0.0f);
            }

            sunLight.CustomProperties = lightProps;

            // Spawn the sun light
            auto callStart = std::chrono::high_resolution_clock::now();
            auto [entityId, success] = SpawnElementDirect(sunLight, scene);
            auto callEnd = std::chrono::high_resolution_clock::now();

            ToolCallResult callResult;
            callResult.ToolName = "DirectSpawn";
            callResult.Arguments = m_Factory.CreateSpawnArguments(sunLight);
            callResult.ExecutionTimeMs = std::chrono::duration<double, std::milli>(callEnd - callStart).count();
            callResult.Success = success;

            if (success) {
                result.CreatedEntityIds.push_back(entityId);
            }

            result.ToolCalls.push_back(callResult);
        }
    };

    // ============================================================================
    // Design Query Tool
    // ============================================================================

    class DesignQueryTool : public MCPTool {
    public:
        DesignQueryTool()
            : MCPTool("DesignQuery",
                      "Query what the Auto-Level Designer can understand from a prompt "
                      "without actually creating anything. Useful for validating prompts "
                      "and understanding how they will be interpreted.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"prompt", {
                    {"type", "string"},
                    {"description", "The prompt to analyze"}
                }}
            };
            schema.Required = {"prompt"};
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            (void)scene;  // Not needed for queries

            std::string prompt = arguments.value("prompt", "");
            if (prompt.empty()) {
                return ToolResult::Error("Prompt is required");
            }

            DesignIntent intent = m_Parser.Parse(prompt);

            Json result;
            result["parsed"] = intent.IsValid;
            result["sceneType"] = intent.SceneType;
            result["elementCount"] = static_cast<int>(intent.Elements.size());
            result["globalSettings"] = intent.GlobalSettings;

            // Element summary by type
            std::unordered_map<std::string, int> typeCounts;
            for (const auto& elem : intent.Elements) {
                typeCounts[elem.Type]++;
            }

            Json elementSummary = Json::object();
            for (const auto& [type, count] : typeCounts) {
                elementSummary[type] = count;
            }
            result["elementsByType"] = elementSummary;

            if (!intent.ParseWarnings.empty()) {
                result["warnings"] = intent.ParseWarnings;
            }

            // Suggestions for improving the prompt
            Json suggestions = Json::array();
            if (intent.Elements.empty()) {
                suggestions.push_back("Try specifying objects to create (e.g., 'trees', 'lights', 'rocks')");
            }
            if (intent.SceneType == "generic") {
                suggestions.push_back("Add scene type keywords (e.g., 'forest', 'dungeon', 'city')");
            }
            if (!intent.GlobalSettings.contains("arrangement")) {
                suggestions.push_back("Specify arrangement (e.g., 'in a circle', 'grid', 'randomly scattered')");
            }
            if (!suggestions.empty()) {
                result["suggestions"] = suggestions;
            }

            return ToolResult::SuccessJson(result);
        }

        bool RequiresScene() const override { return false; }

    private:
        PromptParser m_Parser;
    };

    // ============================================================================
    // Design Templates Tool
    // ============================================================================

    class DesignTemplatesTool : public MCPTool {
    public:
        DesignTemplatesTool()
            : MCPTool("DesignTemplates",
                      "List available element types, presets, and scene templates that "
                      "the Auto-Level Designer understands.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"category", {
                    {"type", "string"},
                    {"enum", Json::array({"all", "elements", "scenes", "arrangements", "atmosphere"})},
                    {"description", "Category of templates to list"},
                    {"default", "all"}
                }}
            };
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            (void)scene;

            std::string category = arguments.value("category", "all");

            Json result;

            if (category == "all" || category == "elements") {
                result["elementTypes"] = Json::array({
                    {{"type", "tree"}, {"template", "mesh"}, {"description", "Trees and vegetation"}},
                    {{"type", "rock"}, {"template", "mesh"}, {"description", "Rocks and boulders"}},
                    {{"type", "light"}, {"template", "light"}, {"description", "Light sources"}},
                    {{"type", "box"}, {"template", "physicsBox"}, {"description", "Physics-enabled boxes"}},
                    {{"type", "sphere"}, {"template", "physicsSphere"}, {"description", "Physics-enabled spheres"}},
                    {{"type", "platform"}, {"template", "physicsBox"}, {"description", "Static platforms"}},
                    {{"type", "wall"}, {"template", "physicsBox"}, {"description", "Static walls"}},
                    {{"type", "building"}, {"template", "mesh"}, {"description", "Buildings and structures"}},
                    {{"type", "enemy"}, {"template", "mesh"}, {"description", "Enemy entities"}},
                    {{"type", "npc"}, {"template", "mesh"}, {"description", "Non-player characters"}},
                    {{"type", "trigger"}, {"template", "trigger"}, {"description", "Trigger zones"}},
                    {{"type", "water"}, {"template", "mesh"}, {"description", "Water planes"}}
                });
            }

            if (category == "all" || category == "scenes") {
                result["sceneTypes"] = Json::array({
                    {{"type", "forest"}, {"keywords", Json::array({"forest", "woods", "jungle"})}, 
                     {"description", "Forest and woodland scenes"}},
                    {{"type", "dungeon"}, {"keywords", Json::array({"dungeon", "cave", "underground"})}, 
                     {"description", "Underground dungeons and caves"}},
                    {{"type", "urban"}, {"keywords", Json::array({"city", "town", "village"})}, 
                     {"description", "Urban environments"}},
                    {{"type", "desert"}, {"keywords", Json::array({"desert", "wasteland"})}, 
                     {"description", "Desert and arid environments"}},
                    {{"type", "space"}, {"keywords", Json::array({"space", "sci-fi", "futuristic"})}, 
                     {"description", "Space and sci-fi settings"}},
                    {{"type", "medieval"}, {"keywords", Json::array({"medieval", "castle"})}, 
                     {"description", "Medieval fantasy settings"}},
                    {{"type", "coastal"}, {"keywords", Json::array({"beach", "ocean", "island"})}, 
                     {"description", "Coastal and island settings"}},
                    {{"type", "mountain"}, {"keywords", Json::array({"mountain", "hills"})}, 
                     {"description", "Mountainous terrain"}},
                    {{"type", "interior"}, {"keywords", Json::array({"indoor", "room", "building"})}, 
                     {"description", "Interior spaces"}}
                });
            }

            if (category == "all" || category == "arrangements") {
                result["arrangements"] = Json::array({
                    {{"type", "circle"}, {"keywords", Json::array({"circle", "ring"})}, 
                     {"description", "Elements arranged in a circle"}},
                    {{"type", "grid"}, {"keywords", Json::array({"grid", "rows"})}, 
                     {"description", "Elements in a regular grid pattern"}},
                    {{"type", "line"}, {"keywords", Json::array({"line", "row"})}, 
                     {"description", "Elements in a straight line"}},
                    {{"type", "random"}, {"keywords", Json::array({"random", "scattered"})}, 
                     {"description", "Randomly distributed elements"}}
                });
            }

            if (category == "all" || category == "atmosphere") {
                result["atmosphere"] = {
                    {"timeOfDay", Json::array({"day", "night", "sunset", "morning"})},
                    {"mood", Json::array({"peaceful", "action", "horror"})},
                    {"scale", Json::array({"small", "normal", "large", "huge"})},
                    {"effects", Json::array({"fog", "mist"})}
                };
            }

            result["examplePrompts"] = Json::array({
                "Create a forest scene with 10 trees arranged in a circle",
                "Build a dark dungeon with scattered lights",
                "Make a peaceful village with several buildings and a few lights",
                "Design a spooky graveyard at night with rocks and fog",
                "Create a large combat arena with walls arranged in a grid"
            });

            return ToolResult::SuccessJson(result);
        }

        bool RequiresScene() const override { return false; }
    };

    // ============================================================================
    // Factory function to create Auto-Level Designer tools
    // ============================================================================

    inline std::vector<MCPToolPtr> CreateAutoLevelDesignerTools() {
        return {
            std::make_shared<AutoLevelDesignerTool>(),
            std::make_shared<DesignQueryTool>(),
            std::make_shared<DesignTemplatesTool>()
        };
    }

} // namespace MCP
} // namespace Core
