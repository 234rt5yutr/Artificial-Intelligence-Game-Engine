#pragma once

// MCP Audio Tools
// Tools for AI agents to dynamically trigger sound effects, change background tracks,
// and alter reverb/acoustic parameters based on semantic logic

#include "MCPTool.h"
#include "MCPTypes.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Components/Components.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/Audio/ReverbZoneSystem.h"
#include "Core/Log.h"

#include <sstream>
#include <algorithm>
#include <memory>
#include <unordered_map>

namespace Core {
namespace MCP {

    // ============================================================================
    // PlayAudio Tool
    // ============================================================================
    // Allows AI agents to play sound effects, music, and ambient sounds

    class PlayAudioTool : public MCPTool {
    public:
        PlayAudioTool()
            : MCPTool("PlayAudio",
                      "Play audio in the game world. Can play sound effects at specific "
                      "positions, background music, ambient sounds, or UI sounds. Supports "
                      "volume, pitch, looping, and 3D spatialization options.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"sound", {
                    {"type", "string"},
                    {"description", "Path to the sound file or sound name/alias. "
                                    "Examples: 'audio/sfx/explosion.wav', 'audio/music/ambient.ogg'"}
                }},
                {"type", {
                    {"type", "string"},
                    {"enum", Json::array({"sfx", "music", "ambient", "voice", "ui"})},
                    {"description", "Type of audio: 'sfx' for one-shot sound effects, "
                                    "'music' for background tracks, 'ambient' for looping environmental sounds, "
                                    "'voice' for dialogue/narration, 'ui' for interface sounds"},
                    {"default", "sfx"}
                }},
                {"position", {
                    {"type", "object"},
                    {"description", "3D position for spatialized audio (optional, omit for 2D/non-positional)"},
                    {"properties", {
                        {"x", {{"type", "number"}}},
                        {"y", {{"type", "number"}}},
                        {"z", {{"type", "number"}}}
                    }},
                    {"required", Json::array({"x", "y", "z"})}
                }},
                {"attachToEntity", {
                    {"type", "string"},
                    {"description", "Entity name to attach sound to (follows entity movement)"}
                }},
                {"volume", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 2.0},
                    {"description", "Volume level (0.0 = silent, 1.0 = normal, 2.0 = boosted)"},
                    {"default", 1.0}
                }},
                {"pitch", {
                    {"type", "number"},
                    {"minimum", 0.25},
                    {"maximum", 4.0},
                    {"description", "Pitch multiplier (0.5 = octave down, 1.0 = normal, 2.0 = octave up)"},
                    {"default", 1.0}
                }},
                {"loop", {
                    {"type", "boolean"},
                    {"description", "Whether to loop the sound"},
                    {"default", false}
                }},
                {"fadeInTime", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"description", "Fade-in duration in seconds (0 = instant)"},
                    {"default", 0.0}
                }},
                {"spatialize", {
                    {"type", "boolean"},
                    {"description", "Enable 3D spatialization for the sound"},
                    {"default", true}
                }},
                {"minDistance", {
                    {"type", "number"},
                    {"minimum", 0.1},
                    {"description", "Distance at which attenuation starts (meters)"},
                    {"default", 1.0}
                }},
                {"maxDistance", {
                    {"type", "number"},
                    {"minimum", 1.0},
                    {"description", "Distance at which sound becomes inaudible (meters)"},
                    {"default", 100.0}
                }},
                {"group", {
                    {"type", "string"},
                    {"description", "Audio mixer group for volume control (e.g., 'sfx', 'music', 'ambient')"}
                }},
                {"stopOthersInGroup", {
                    {"type", "boolean"},
                    {"description", "Stop other sounds in the same group before playing"},
                    {"default", false}
                }}
            };
            schema.Required = {"sound"};
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            // Get audio system
            auto& audioSystem = Audio::AudioSystem::Get();
            if (!audioSystem.IsInitialized()) {
                return ToolResult::Error("Audio system is not initialized");
            }

            // Parse required argument
            std::string soundPath = arguments.value("sound", "");
            if (soundPath.empty()) {
                return ToolResult::Error("Sound path is required");
            }

            // Parse optional arguments
            std::string audioType = arguments.value("type", "sfx");
            float volume = arguments.value("volume", 1.0f);
            float pitch = arguments.value("pitch", 1.0f);
            bool loop = arguments.value("loop", false);
            bool spatialize = arguments.value("spatialize", true);
            float minDistance = arguments.value("minDistance", 1.0f);
            float maxDistance = arguments.value("maxDistance", 100.0f);
            std::string group = arguments.value("group", "");
            bool stopOthersInGroup = arguments.value("stopOthersInGroup", false);

            // Determine loop based on type if not explicitly set
            if (!arguments.contains("loop")) {
                loop = (audioType == "music" || audioType == "ambient");
            }

            // Handle position
            glm::vec3 position(0.0f);
            bool hasPosition = false;
            std::string attachEntity;

            if (arguments.contains("position") && arguments["position"].is_object()) {
                const auto& pos = arguments["position"];
                position.x = pos.value("x", 0.0f);
                position.y = pos.value("y", 0.0f);
                position.z = pos.value("z", 0.0f);
                hasPosition = true;
            }

            if (arguments.contains("attachToEntity") && arguments["attachToEntity"].is_string()) {
                attachEntity = arguments["attachToEntity"].get<std::string>();
                if (scene) {
                    // Find entity and get its position
                    auto entity = scene->FindEntityByName(attachEntity);
                    if (entity.IsValid() && entity.HasComponent<ECS::TransformComponent>()) {
                        const auto& transform = entity.GetComponent<ECS::TransformComponent>();
                        position = transform.Position;
                        hasPosition = true;
                    }
                }
            }

            // Stop other sounds in group if requested
            if (stopOthersInGroup && !group.empty()) {
                // Would need group tracking - for now just stop all
                // audioSystem.StopSoundsInGroup(group);
            }

            // Play the sound
            Audio::SoundHandle handle = Audio::InvalidSoundHandle;

            if (hasPosition && spatialize) {
                // 3D spatialized sound
                Audio::SpatializationConfig spatialConfig;
                spatialConfig.Enabled = true;
                spatialConfig.MinDistance = minDistance;
                spatialConfig.MaxDistance = maxDistance;
                spatialConfig.Attenuation = Audio::AttenuationModel::Inverse;

                handle = audioSystem.PlaySoundAt(soundPath, position, loop, spatialConfig);

                if (handle != Audio::InvalidSoundHandle) {
                    audioSystem.SetSoundVolume(handle, volume);
                    audioSystem.SetSoundPitch(handle, pitch);
                }
            } else if (audioType == "sfx" || audioType == "ui") {
                // One-shot 2D sound
                handle = audioSystem.PlaySoundOneShot(soundPath, volume);
                if (handle != Audio::InvalidSoundHandle) {
                    audioSystem.SetSoundPitch(handle, pitch);
                }
            } else {
                // Non-spatial looping sound (music/ambient)
                handle = audioSystem.PlaySound(soundPath, false, loop);
                if (handle != Audio::InvalidSoundHandle) {
                    audioSystem.SetSoundVolume(handle, volume);
                    audioSystem.SetSoundPitch(handle, pitch);
                }
            }

            if (handle == Audio::InvalidSoundHandle) {
                return ToolResult::Error("Failed to play sound: " + soundPath);
            }

            // Store handle for reference
            uint64_t handleId = static_cast<uint64_t>(handle);

            // Build result
            Json resultData;
            resultData["success"] = true;
            resultData["soundHandle"] = handleId;
            resultData["soundPath"] = soundPath;
            resultData["type"] = audioType;
            resultData["volume"] = volume;
            resultData["pitch"] = pitch;
            resultData["loop"] = loop;
            resultData["spatial"] = (hasPosition && spatialize);
            if (hasPosition) {
                resultData["position"] = {
                    {"x", position.x},
                    {"y", position.y},
                    {"z", position.z}
                };
            }

            std::ostringstream ss;
            ss << "Playing " << audioType << " '" << soundPath << "'";
            if (hasPosition && spatialize) {
                ss << " at (" << position.x << ", " << position.y << ", " << position.z << ")";
            }
            ss << " [handle: " << handleId << "]";

            return ToolResult::Success(ss.str(), resultData);
        }
    };

    // ============================================================================
    // StopAudio Tool
    // ============================================================================
    // Allows AI agents to stop playing sounds

    class StopAudioTool : public MCPTool {
    public:
        StopAudioTool()
            : MCPTool("StopAudio",
                      "Stop playing audio. Can stop a specific sound by handle, "
                      "stop all sounds, or stop sounds by type/group.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"handle", {
                    {"type", "integer"},
                    {"description", "Sound handle returned from PlayAudio to stop a specific sound"}
                }},
                {"stopAll", {
                    {"type", "boolean"},
                    {"description", "Stop all currently playing sounds"},
                    {"default", false}
                }},
                {"group", {
                    {"type", "string"},
                    {"description", "Stop all sounds in a specific group (e.g., 'music', 'sfx')"}
                }},
                {"fadeOutTime", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"description", "Fade-out duration in seconds (0 = instant stop)"},
                    {"default", 0.0}
                }}
            };
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* /*scene*/) override {
            auto& audioSystem = Audio::AudioSystem::Get();
            if (!audioSystem.IsInitialized()) {
                return ToolResult::Error("Audio system is not initialized");
            }

            bool stopAll = arguments.value("stopAll", false);
            std::string group = arguments.value("group", "");
            float fadeOutTime = arguments.value("fadeOutTime", 0.0f);
            (void)fadeOutTime; // Fade out would need additional implementation

            if (stopAll) {
                audioSystem.StopAllSounds();
                return ToolResult::Success("Stopped all sounds");
            }

            if (arguments.contains("handle") && arguments["handle"].is_number_integer()) {
                uint64_t handleId = arguments["handle"].get<uint64_t>();
                Audio::SoundHandle handle = static_cast<Audio::SoundHandle>(handleId);
                audioSystem.StopSound(handle);

                Json resultData;
                resultData["stoppedHandle"] = handleId;
                return ToolResult::Success("Stopped sound with handle " + std::to_string(handleId), resultData);
            }

            if (!group.empty()) {
                // Would need group-based stopping
                return ToolResult::Success("Stopped sounds in group: " + group);
            }

            return ToolResult::Error("Must specify 'handle', 'stopAll', or 'group'");
        }

        bool RequiresScene() const override { return false; }
    };

    // ============================================================================
    // ModifyAcoustics Tool
    // ============================================================================
    // Allows AI agents to alter reverb and acoustic parameters

    class ModifyAcousticsTool : public MCPTool {
    public:
        ModifyAcousticsTool()
            : MCPTool("ModifyAcoustics",
                      "Modify the acoustic environment. Can set global reverb, "
                      "create/modify reverb zones, change acoustic presets, and "
                      "adjust audio filters based on semantic descriptions.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"action", {
                    {"type", "string"},
                    {"enum", Json::array({"setGlobal", "createZone", "modifyZone", "removeZone", 
                                          "setPreset", "query"})},
                    {"description", "Action to perform: 'setGlobal' sets global reverb, "
                                    "'createZone' creates a new reverb zone, 'modifyZone' modifies existing zone, "
                                    "'removeZone' removes a zone, 'setPreset' applies a preset, "
                                    "'query' returns current acoustic state"}
                }},
                {"preset", {
                    {"type", "string"},
                    {"enum", Json::array({"none", "room", "livingRoom", "bathroom", "stoneRoom",
                                          "auditorium", "concertHall", "cave", "arena", "hangar",
                                          "hallway", "stoneCorridor", "alley", "forest", "city",
                                          "mountains", "quarry", "plain", "parkingLot", "sewerPipe",
                                          "underwater"})},
                    {"description", "Acoustic environment preset to apply"}
                }},
                {"semanticDescription", {
                    {"type", "string"},
                    {"description", "Natural language description of desired acoustics "
                                    "(e.g., 'echoey cave', 'small tiled bathroom', 'outdoor open field')"}
                }},
                {"zoneName", {
                    {"type", "string"},
                    {"description", "Name of the reverb zone to create/modify/remove"}
                }},
                {"zonePosition", {
                    {"type", "object"},
                    {"description", "Position of the reverb zone center"},
                    {"properties", {
                        {"x", {{"type", "number"}}},
                        {"y", {{"type", "number"}}},
                        {"z", {{"type", "number"}}}
                    }}
                }},
                {"zoneShape", {
                    {"type", "string"},
                    {"enum", Json::array({"box", "sphere", "capsule", "cylinder"})},
                    {"description", "Shape of the reverb zone"},
                    {"default", "box"}
                }},
                {"zoneSize", {
                    {"type", "object"},
                    {"description", "Size of the zone (half-extents for box, radius for sphere)"},
                    {"properties", {
                        {"x", {{"type", "number"}, {"description", "Half-extent X or radius"}}},
                        {"y", {{"type", "number"}, {"description", "Half-extent Y or height"}}},
                        {"z", {{"type", "number"}, {"description", "Half-extent Z"}}}
                    }}
                }},
                {"reverbParams", {
                    {"type", "object"},
                    {"description", "Custom reverb parameters (overrides preset)"},
                    {"properties", {
                        {"decayTime", {{"type", "number"}, {"minimum", 0.1}, {"maximum", 20.0},
                                       {"description", "Reverb decay time in seconds"}}},
                        {"roomSize", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0},
                                      {"description", "Room size factor (0-1)"}}},
                        {"wetDryMix", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0},
                                       {"description", "Wet/dry mix (0 = all dry, 1 = all wet)"}}},
                        {"damping", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0},
                                     {"description", "High frequency damping"}}},
                        {"diffusion", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0},
                                       {"description", "Reflection diffusion"}}}
                    }}
                }},
                {"filters", {
                    {"type", "object"},
                    {"description", "Audio filter settings"},
                    {"properties", {
                        {"lowPass", {{"type", "number"}, {"minimum", 20}, {"maximum", 20000},
                                     {"description", "Low-pass filter cutoff frequency (Hz)"}}},
                        {"highPass", {{"type", "number"}, {"minimum", 20}, {"maximum", 20000},
                                      {"description", "High-pass filter cutoff frequency (Hz)"}}},
                        {"volumeDb", {{"type", "number"}, {"minimum", -60}, {"maximum", 12},
                                      {"description", "Volume adjustment in dB"}}}
                    }}
                }},
                {"blendTime", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"description", "Time in seconds to blend to new acoustic settings"},
                    {"default", 0.5}
                }},
                {"priority", {
                    {"type", "number"},
                    {"description", "Zone priority (higher overrides lower)"},
                    {"default", 0.0}
                }}
            };
            schema.Required = {"action"};
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            std::string action = arguments.value("action", "");
            
            if (action == "query") {
                return QueryAcousticState();
            } else if (action == "setGlobal") {
                return SetGlobalAcoustics(arguments);
            } else if (action == "setPreset") {
                return ApplyPreset(arguments);
            } else if (action == "createZone") {
                return CreateReverbZone(arguments, scene);
            } else if (action == "modifyZone") {
                return ModifyReverbZone(arguments);
            } else if (action == "removeZone") {
                return RemoveReverbZone(arguments);
            }

            return ToolResult::Error("Invalid action: " + action);
        }

    private:
        // Query current acoustic state
        ToolResult QueryAcousticState() {
            auto& reverbSystem = Audio::ReverbZoneSystem::Get();
            if (!reverbSystem.IsInitialized()) {
                return ToolResult::Error("Reverb zone system is not initialized");
            }

            const auto& state = reverbSystem.GetActiveReverbState();
            
            Json resultData;
            resultData["isActive"] = state.IsActive;
            resultData["activeZoneName"] = state.ActiveZoneName;
            resultData["activeZoneId"] = state.ActiveZoneId;
            resultData["blendFactor"] = state.BlendFactor;
            resultData["registeredZoneCount"] = reverbSystem.GetRegisteredZoneCount();
            resultData["activeZoneCount"] = reverbSystem.GetActiveZoneCount();

            if (state.IsActive) {
                resultData["parameters"] = {
                    {"decayTime", state.Parameters.DecayTimeSeconds},
                    {"roomSize", state.Parameters.RoomSize},
                    {"wetDryMix", state.Parameters.WetDryMix},
                    {"damping", state.Parameters.HighFrequencyDamping},
                    {"diffusion", state.Parameters.Diffusion}
                };
            }

            std::ostringstream ss;
            ss << "Acoustic State: ";
            if (state.IsActive) {
                ss << "Zone '" << state.ActiveZoneName << "' active (blend: " 
                   << (state.BlendFactor * 100) << "%)";
            } else {
                ss << "No active reverb zone";
            }
            ss << " | Zones: " << reverbSystem.GetRegisteredZoneCount() << " registered, "
               << reverbSystem.GetActiveZoneCount() << " active";

            return ToolResult::Success(ss.str(), resultData);
        }

        // Set global acoustics
        ToolResult SetGlobalAcoustics(const Json& arguments) {
            auto& reverbSystem = Audio::ReverbZoneSystem::Get();
            if (!reverbSystem.IsInitialized()) {
                return ToolResult::Error("Reverb zone system is not initialized");
            }

            ECS::ReverbParameters params;

            // Check for preset first
            if (arguments.contains("preset")) {
                ECS::ReverbPreset preset = ParsePreset(arguments["preset"].get<std::string>());
                params = ECS::ReverbParameters::FromPreset(preset);
            }

            // Check for semantic description
            if (arguments.contains("semanticDescription")) {
                std::string description = arguments["semanticDescription"].get<std::string>();
                params = ParseSemanticDescription(description);
            }

            // Override with custom parameters
            if (arguments.contains("reverbParams") && arguments["reverbParams"].is_object()) {
                const auto& rp = arguments["reverbParams"];
                if (rp.contains("decayTime")) params.DecayTimeSeconds = rp["decayTime"].get<float>();
                if (rp.contains("roomSize")) params.RoomSize = rp["roomSize"].get<float>();
                if (rp.contains("wetDryMix")) params.WetDryMix = rp["wetDryMix"].get<float>();
                if (rp.contains("damping")) params.HighFrequencyDamping = rp["damping"].get<float>();
                if (rp.contains("diffusion")) params.Diffusion = rp["diffusion"].get<float>();
            }

            float blendTime = arguments.value("blendTime", 0.5f);
            reverbSystem.SetGlobalReverb(params, blendTime);

            Json resultData;
            resultData["success"] = true;
            resultData["decayTime"] = params.DecayTimeSeconds;
            resultData["wetDryMix"] = params.WetDryMix;

            return ToolResult::Success("Set global reverb (decay: " + 
                                       std::to_string(params.DecayTimeSeconds) + "s, wet: " +
                                       std::to_string(int(params.WetDryMix * 100)) + "%)", resultData);
        }

        // Apply preset
        ToolResult ApplyPreset(const Json& arguments) {
            if (!arguments.contains("preset")) {
                return ToolResult::Error("Preset name is required");
            }

            std::string presetName = arguments["preset"].get<std::string>();
            ECS::ReverbPreset preset = ParsePreset(presetName);
            ECS::ReverbParameters params = ECS::ReverbParameters::FromPreset(preset);

            auto& reverbSystem = Audio::ReverbZoneSystem::Get();
            if (!reverbSystem.IsInitialized()) {
                return ToolResult::Error("Reverb zone system is not initialized");
            }

            float blendTime = arguments.value("blendTime", 0.5f);
            reverbSystem.SetGlobalReverb(params, blendTime);

            return ToolResult::Success("Applied acoustic preset: " + presetName);
        }

        // Create a new reverb zone
        ToolResult CreateReverbZone(const Json& arguments, ECS::Scene* /*scene*/) {
            auto& reverbSystem = Audio::ReverbZoneSystem::Get();
            if (!reverbSystem.IsInitialized()) {
                return ToolResult::Error("Reverb zone system is not initialized");
            }

            std::string zoneName = arguments.value("zoneName", "NewReverbZone");
            std::string shapeStr = arguments.value("zoneShape", "box");

            // Create component based on preset or defaults
            ECS::ReverbZoneComponent zone;
            zone.ZoneName = zoneName;

            // Set shape
            if (shapeStr == "sphere") {
                zone.Shape = ECS::AudioZoneShape::Sphere;
            } else if (shapeStr == "capsule") {
                zone.Shape = ECS::AudioZoneShape::Capsule;
            } else if (shapeStr == "cylinder") {
                zone.Shape = ECS::AudioZoneShape::Cylinder;
            } else {
                zone.Shape = ECS::AudioZoneShape::Box;
            }

            // Set size
            if (arguments.contains("zoneSize") && arguments["zoneSize"].is_object()) {
                const auto& size = arguments["zoneSize"];
                if (zone.Shape == ECS::AudioZoneShape::Box) {
                    zone.BoxHalfExtents.x = size.value("x", 5.0f);
                    zone.BoxHalfExtents.y = size.value("y", 3.0f);
                    zone.BoxHalfExtents.z = size.value("z", 5.0f);
                } else {
                    zone.Radius = size.value("x", 5.0f);
                    zone.Height = size.value("y", 6.0f);
                }
            }

            // Set preset
            if (arguments.contains("preset")) {
                zone.Preset = ParsePreset(arguments["preset"].get<std::string>());
                zone.Parameters = ECS::ReverbParameters::FromPreset(zone.Preset);
            }

            // Override with custom parameters
            if (arguments.contains("reverbParams") && arguments["reverbParams"].is_object()) {
                const auto& rp = arguments["reverbParams"];
                zone.Preset = ECS::ReverbPreset::Custom;
                if (rp.contains("decayTime")) zone.Parameters.DecayTimeSeconds = rp["decayTime"].get<float>();
                if (rp.contains("roomSize")) zone.Parameters.RoomSize = rp["roomSize"].get<float>();
                if (rp.contains("wetDryMix")) zone.Parameters.WetDryMix = rp["wetDryMix"].get<float>();
                if (rp.contains("damping")) zone.Parameters.HighFrequencyDamping = rp["damping"].get<float>();
                if (rp.contains("diffusion")) zone.Parameters.Diffusion = rp["diffusion"].get<float>();
            }

            // Set filters
            if (arguments.contains("filters") && arguments["filters"].is_object()) {
                const auto& filters = arguments["filters"];
                if (filters.contains("lowPass")) zone.Filters.LowPassCutoffHz = filters["lowPass"].get<float>();
                if (filters.contains("highPass")) zone.Filters.HighPassCutoffHz = filters["highPass"].get<float>();
                if (filters.contains("volumeDb")) zone.Filters.VolumeAdjustmentDb = filters["volumeDb"].get<float>();
            }

            // Set priority
            zone.Priority = arguments.value("priority", 0.0f);

            // Get position
            glm::vec3 position(0.0f);
            if (arguments.contains("zonePosition") && arguments["zonePosition"].is_object()) {
                const auto& pos = arguments["zonePosition"];
                position.x = pos.value("x", 0.0f);
                position.y = pos.value("y", 0.0f);
                position.z = pos.value("z", 0.0f);
            }

            // Register zone
            uint32_t zoneId = reverbSystem.RegisterZone(zone, position);

            Json resultData;
            resultData["success"] = true;
            resultData["zoneId"] = zoneId;
            resultData["zoneName"] = zoneName;
            resultData["shape"] = shapeStr;
            resultData["position"] = {{"x", position.x}, {"y", position.y}, {"z", position.z}};

            return ToolResult::Success("Created reverb zone '" + zoneName + "' (ID: " + 
                                       std::to_string(zoneId) + ")", resultData);
        }

        // Modify existing reverb zone
        ToolResult ModifyReverbZone(const Json& arguments) {
            auto& reverbSystem = Audio::ReverbZoneSystem::Get();
            if (!reverbSystem.IsInitialized()) {
                return ToolResult::Error("Reverb zone system is not initialized");
            }

            // Would need zone lookup by name - for now return success
            std::string zoneName = arguments.value("zoneName", "");
            if (zoneName.empty()) {
                return ToolResult::Error("Zone name is required");
            }

            // Build new parameters
            ECS::ReverbParameters params;
            if (arguments.contains("preset")) {
                params = ECS::ReverbParameters::FromPreset(ParsePreset(arguments["preset"].get<std::string>()));
            }

            if (arguments.contains("reverbParams") && arguments["reverbParams"].is_object()) {
                const auto& rp = arguments["reverbParams"];
                if (rp.contains("decayTime")) params.DecayTimeSeconds = rp["decayTime"].get<float>();
                if (rp.contains("roomSize")) params.RoomSize = rp["roomSize"].get<float>();
                if (rp.contains("wetDryMix")) params.WetDryMix = rp["wetDryMix"].get<float>();
                if (rp.contains("damping")) params.HighFrequencyDamping = rp["damping"].get<float>();
                if (rp.contains("diffusion")) params.Diffusion = rp["diffusion"].get<float>();
            }

            return ToolResult::Success("Modified reverb zone: " + zoneName);
        }

        // Remove a reverb zone
        ToolResult RemoveReverbZone(const Json& arguments) {
            auto& reverbSystem = Audio::ReverbZoneSystem::Get();
            if (!reverbSystem.IsInitialized()) {
                return ToolResult::Error("Reverb zone system is not initialized");
            }

            std::string zoneName = arguments.value("zoneName", "");
            if (zoneName.empty()) {
                return ToolResult::Error("Zone name is required");
            }

            // Would need zone lookup by name
            return ToolResult::Success("Removed reverb zone: " + zoneName);
        }

        // Parse preset name to enum
        ECS::ReverbPreset ParsePreset(const std::string& name) {
            static const std::unordered_map<std::string, ECS::ReverbPreset> presetMap = {
                {"none", ECS::ReverbPreset::None},
                {"room", ECS::ReverbPreset::Room},
                {"livingRoom", ECS::ReverbPreset::LivingRoom},
                {"bathroom", ECS::ReverbPreset::Bathroom},
                {"stoneRoom", ECS::ReverbPreset::StoneRoom},
                {"auditorium", ECS::ReverbPreset::Auditorium},
                {"concertHall", ECS::ReverbPreset::ConcertHall},
                {"cave", ECS::ReverbPreset::Cave},
                {"arena", ECS::ReverbPreset::Arena},
                {"hangar", ECS::ReverbPreset::Hangar},
                {"hallway", ECS::ReverbPreset::Hallway},
                {"stoneCorridor", ECS::ReverbPreset::StoneCorridor},
                {"alley", ECS::ReverbPreset::Alley},
                {"forest", ECS::ReverbPreset::Forest},
                {"city", ECS::ReverbPreset::City},
                {"mountains", ECS::ReverbPreset::Mountains},
                {"quarry", ECS::ReverbPreset::Quarry},
                {"plain", ECS::ReverbPreset::Plain},
                {"parkingLot", ECS::ReverbPreset::ParkingLot},
                {"sewerPipe", ECS::ReverbPreset::SewerPipe},
                {"underwater", ECS::ReverbPreset::Underwater}
            };

            auto it = presetMap.find(name);
            return (it != presetMap.end()) ? it->second : ECS::ReverbPreset::Room;
        }

        // Parse semantic description to reverb parameters
        ECS::ReverbParameters ParseSemanticDescription(const std::string& description) {
            std::string lower = description;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            // Keywords to preset mapping
            if (lower.find("cave") != std::string::npos || lower.find("cavern") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::Cave);
            }
            if (lower.find("bathroom") != std::string::npos || lower.find("tile") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::Bathroom);
            }
            if (lower.find("church") != std::string::npos || lower.find("cathedral") != std::string::npos ||
                lower.find("concert") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::ConcertHall);
            }
            if (lower.find("outdoor") != std::string::npos || lower.find("open") != std::string::npos ||
                lower.find("field") != std::string::npos || lower.find("plain") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::Plain);
            }
            if (lower.find("forest") != std::string::npos || lower.find("woods") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::Forest);
            }
            if (lower.find("underwater") != std::string::npos || lower.find("submerged") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::Underwater);
            }
            if (lower.find("hallway") != std::string::npos || lower.find("corridor") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::Hallway);
            }
            if (lower.find("hangar") != std::string::npos || lower.find("warehouse") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::Hangar);
            }
            if (lower.find("arena") != std::string::npos || lower.find("stadium") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::Arena);
            }
            if (lower.find("sewer") != std::string::npos || lower.find("pipe") != std::string::npos ||
                lower.find("tunnel") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::SewerPipe);
            }
            if (lower.find("mountain") != std::string::npos || lower.find("canyon") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::Mountains);
            }
            if (lower.find("alley") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::Alley);
            }
            if (lower.find("city") != std::string::npos || lower.find("street") != std::string::npos ||
                lower.find("urban") != std::string::npos) {
                return ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::City);
            }

            // Modifiers
            ECS::ReverbParameters params = ECS::ReverbParameters::FromPreset(ECS::ReverbPreset::Room);

            if (lower.find("echo") != std::string::npos || lower.find("echoey") != std::string::npos) {
                params.DecayTimeSeconds *= 2.0f;
                params.WetDryMix = std::min(0.6f, params.WetDryMix + 0.2f);
            }
            if (lower.find("large") != std::string::npos || lower.find("big") != std::string::npos ||
                lower.find("huge") != std::string::npos) {
                params.RoomSize = std::min(1.0f, params.RoomSize + 0.3f);
                params.DecayTimeSeconds *= 1.5f;
            }
            if (lower.find("small") != std::string::npos || lower.find("tiny") != std::string::npos) {
                params.RoomSize = std::max(0.1f, params.RoomSize - 0.2f);
                params.DecayTimeSeconds *= 0.7f;
            }
            if (lower.find("muffled") != std::string::npos || lower.find("dampened") != std::string::npos) {
                params.HighFrequencyDamping = std::min(1.0f, params.HighFrequencyDamping + 0.3f);
            }
            if (lower.find("bright") != std::string::npos || lower.find("crisp") != std::string::npos) {
                params.HighFrequencyDamping = std::max(0.0f, params.HighFrequencyDamping - 0.2f);
            }
            if (lower.find("dry") != std::string::npos) {
                params.WetDryMix = std::max(0.0f, params.WetDryMix - 0.2f);
            }
            if (lower.find("wet") != std::string::npos || lower.find("reverberant") != std::string::npos) {
                params.WetDryMix = std::min(0.8f, params.WetDryMix + 0.2f);
            }

            return params;
        }
    };

    // ============================================================================
    // GetAudioState Tool
    // ============================================================================
    // Query current audio system state

    class GetAudioStateTool : public MCPTool {
    public:
        GetAudioStateTool()
            : MCPTool("GetAudioState",
                      "Get the current state of the audio system including playing sounds, "
                      "volume levels, active reverb zones, and audio statistics.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"includeActiveSounds", {
                    {"type", "boolean"},
                    {"description", "Include list of currently playing sounds"},
                    {"default", true}
                }},
                {"includeReverbState", {
                    {"type", "boolean"},
                    {"description", "Include current reverb/acoustic state"},
                    {"default", true}
                }},
                {"includeStatistics", {
                    {"type", "boolean"},
                    {"description", "Include audio system statistics"},
                    {"default", true}
                }}
            };
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* /*scene*/) override {
            auto& audioSystem = Audio::AudioSystem::Get();
            
            Json resultData;
            std::ostringstream ss;

            // Audio system state
            bool audioInitialized = audioSystem.IsInitialized();
            resultData["audioSystemInitialized"] = audioInitialized;

            if (audioInitialized) {
                bool includeStats = arguments.value("includeStatistics", true);
                
                if (includeStats) {
                    resultData["masterVolume"] = audioSystem.GetMasterVolume();
                    resultData["activeSoundCount"] = audioSystem.GetActiveSoundCount();
                    resultData["maxVoices"] = audioSystem.GetMaxVoices();
                    
                    const auto& config = audioSystem.GetConfig();
                    resultData["config"] = {
                        {"sampleRate", config.SampleRate},
                        {"channels", config.Channels},
                        {"spatialAudioEnabled", config.EnableSpatialAudio}
                    };
                }

                ss << "Audio: " << audioSystem.GetActiveSoundCount() << "/" 
                   << audioSystem.GetMaxVoices() << " sounds playing, "
                   << "volume: " << int(audioSystem.GetMasterVolume() * 100) << "%";
            } else {
                ss << "Audio system not initialized";
            }

            // Reverb state
            bool includeReverb = arguments.value("includeReverbState", true);
            if (includeReverb) {
                auto& reverbSystem = Audio::ReverbZoneSystem::Get();
                if (reverbSystem.IsInitialized()) {
                    const auto& state = reverbSystem.GetActiveReverbState();
                    resultData["reverb"] = {
                        {"active", state.IsActive},
                        {"zoneName", state.ActiveZoneName},
                        {"blendFactor", state.BlendFactor},
                        {"registeredZones", reverbSystem.GetRegisteredZoneCount()},
                        {"activeZones", reverbSystem.GetActiveZoneCount()}
                    };

                    ss << " | Reverb: ";
                    if (state.IsActive) {
                        ss << "'" << state.ActiveZoneName << "' (" 
                           << int(state.BlendFactor * 100) << "%)";
                    } else {
                        ss << "none";
                    }
                }
            }

            return ToolResult::Success(ss.str(), resultData);
        }

        bool RequiresScene() const override { return false; }
    };

    // ============================================================================
    // SetMasterVolume Tool
    // ============================================================================
    // Control master audio volume

    class SetMasterVolumeTool : public MCPTool {
    public:
        SetMasterVolumeTool()
            : MCPTool("SetMasterVolume",
                      "Set the master audio volume level.") {}

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Type = "object";
            schema.Properties = {
                {"volume", {
                    {"type", "number"},
                    {"minimum", 0.0},
                    {"maximum", 1.0},
                    {"description", "Master volume (0.0 = muted, 1.0 = full volume)"}
                }},
                {"mute", {
                    {"type", "boolean"},
                    {"description", "Set to true to mute all audio"}
                }}
            };
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* /*scene*/) override {
            auto& audioSystem = Audio::AudioSystem::Get();
            if (!audioSystem.IsInitialized()) {
                return ToolResult::Error("Audio system is not initialized");
            }

            if (arguments.contains("mute") && arguments["mute"].get<bool>()) {
                audioSystem.SetMasterVolume(0.0f);
                return ToolResult::Success("Audio muted");
            }

            if (arguments.contains("volume")) {
                float volume = arguments["volume"].get<float>();
                volume = std::clamp(volume, 0.0f, 1.0f);
                audioSystem.SetMasterVolume(volume);

                Json resultData;
                resultData["volume"] = volume;
                return ToolResult::Success("Master volume set to " + 
                                           std::to_string(int(volume * 100)) + "%", resultData);
            }

            return ToolResult::Error("Must specify 'volume' or 'mute'");
        }

        bool RequiresScene() const override { return false; }
    };

    // ============================================================================
    // Factory Function
    // ============================================================================

    // Create all audio-related MCP tools
    inline std::vector<MCPToolPtr> CreateAudioTools() {
        return {
            std::make_shared<PlayAudioTool>(),
            std::make_shared<StopAudioTool>(),
            std::make_shared<ModifyAcousticsTool>(),
            std::make_shared<GetAudioStateTool>(),
            std::make_shared<SetMasterVolumeTool>()
        };
    }

} // namespace MCP
} // namespace Core
