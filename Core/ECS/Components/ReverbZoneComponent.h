#pragma once

// ReverbZoneComponent
// Defines an acoustic volume that applies reverb and other audio effects
// when the listener enters the zone

#include "Core/Math/Math.h"
#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace Core {
namespace ECS {

    // ============================================================================
    // Reverb Presets
    // ============================================================================
    // Common acoustic environment presets

    enum class ReverbPreset : uint8_t {
        None = 0,           // No reverb (dry signal only)
        Room,               // Small room
        LivingRoom,         // Medium living room
        Bathroom,           // Tiled bathroom (bright reverb)
        StoneRoom,          // Stone-walled room
        Auditorium,         // Large auditorium
        ConcertHall,        // Concert hall
        Cave,               // Natural cave
        Arena,              // Sports arena
        Hangar,             // Aircraft hangar
        Hallway,            // Long hallway
        StoneCorridor,      // Stone corridor
        Alley,              // Urban alley
        Forest,             // Dense forest
        City,               // City street
        Mountains,          // Mountain range
        Quarry,             // Open quarry
        Plain,              // Open plain
        ParkingLot,         // Parking structure
        SewerPipe,          // Sewer or pipe
        Underwater,         // Underwater
        Custom              // User-defined parameters
    };

    // Convert preset to string for debugging
    inline const char* ReverbPresetToString(ReverbPreset preset) {
        switch (preset) {
            case ReverbPreset::None:          return "None";
            case ReverbPreset::Room:          return "Room";
            case ReverbPreset::LivingRoom:    return "LivingRoom";
            case ReverbPreset::Bathroom:      return "Bathroom";
            case ReverbPreset::StoneRoom:     return "StoneRoom";
            case ReverbPreset::Auditorium:    return "Auditorium";
            case ReverbPreset::ConcertHall:   return "ConcertHall";
            case ReverbPreset::Cave:          return "Cave";
            case ReverbPreset::Arena:         return "Arena";
            case ReverbPreset::Hangar:        return "Hangar";
            case ReverbPreset::Hallway:       return "Hallway";
            case ReverbPreset::StoneCorridor: return "StoneCorridor";
            case ReverbPreset::Alley:         return "Alley";
            case ReverbPreset::Forest:        return "Forest";
            case ReverbPreset::City:          return "City";
            case ReverbPreset::Mountains:     return "Mountains";
            case ReverbPreset::Quarry:        return "Quarry";
            case ReverbPreset::Plain:         return "Plain";
            case ReverbPreset::ParkingLot:    return "ParkingLot";
            case ReverbPreset::SewerPipe:     return "SewerPipe";
            case ReverbPreset::Underwater:    return "Underwater";
            case ReverbPreset::Custom:        return "Custom";
            default:                          return "Unknown";
        }
    }

    // ============================================================================
    // Zone Shape Types
    // ============================================================================

    enum class AudioZoneShape : uint8_t {
        Box,        // Axis-aligned bounding box
        Sphere,     // Spherical zone
        Capsule,    // Capsule shape (cylinder with hemispherical ends)
        Cylinder    // Upright cylinder
    };

    // ============================================================================
    // Reverb Parameters
    // ============================================================================
    // Detailed reverb configuration

    struct ReverbParameters {
        // Pre-delay: time before first reflection (0-100ms typical)
        float PreDelayMs = 10.0f;

        // Room size: affects reflection density and timing (0-1)
        float RoomSize = 0.5f;

        // Decay time: how long the reverb tail lasts (0.1-20 seconds)
        float DecayTimeSeconds = 1.5f;

        // High frequency damping: simulates air absorption (0-1, higher = more damping)
        float HighFrequencyDamping = 0.5f;

        // Diffusion: spread of reflections (0-1, higher = smoother)
        float Diffusion = 0.8f;

        // Early reflections level relative to dry (-60 to 0 dB)
        float EarlyReflectionsGainDb = -6.0f;

        // Late reverb level relative to dry (-60 to 0 dB)
        float LateReverbGainDb = -10.0f;

        // Wet/Dry mix (0 = all dry, 1 = all wet)
        float WetDryMix = 0.3f;

        // Density: reflection density (0-1)
        float Density = 0.7f;

        // Low frequency reverb time ratio (0.5-2, relative to main decay)
        float LowFrequencyRatio = 1.0f;

        // High frequency reverb time ratio (0.1-1, relative to main decay)
        float HighFrequencyRatio = 0.6f;

        // Modulation depth for chorus-like effect (0-1)
        float ModulationDepth = 0.0f;

        // Modulation rate in Hz (0-10)
        float ModulationRateHz = 0.5f;

        // Factory methods for presets
        static ReverbParameters FromPreset(ReverbPreset preset);
    };

    // ============================================================================
    // Audio Filter Parameters
    // ============================================================================
    // Additional audio processing applied in the zone

    struct AudioFilterParameters {
        // Low-pass filter cutoff frequency (20-20000 Hz, 0 = disabled)
        float LowPassCutoffHz = 0.0f;

        // High-pass filter cutoff frequency (20-20000 Hz, 0 = disabled)
        float HighPassCutoffHz = 0.0f;

        // Filter resonance/Q (0.1-10)
        float FilterResonance = 0.707f;

        // Volume adjustment in this zone (-60 to 12 dB)
        float VolumeAdjustmentDb = 0.0f;

        // Pitch shift factor (0.5-2, 1 = no shift)
        float PitchShift = 1.0f;

        // Echo/delay effect
        bool EnableEcho = false;
        float EchoDelayMs = 250.0f;
        float EchoDecay = 0.5f;
        float EchoWetMix = 0.2f;
    };

    // ============================================================================
    // Reverb Zone Component
    // ============================================================================

    struct ReverbZoneComponent {
        // Zone identification
        std::string ZoneName = "ReverbZone";
        uint32_t ZoneId = 0;

        // Zone shape and dimensions
        AudioZoneShape Shape = AudioZoneShape::Box;

        // Box dimensions (half-extents)
        glm::vec3 BoxHalfExtents{5.0f, 3.0f, 5.0f};

        // Sphere/Capsule radius
        float Radius = 5.0f;

        // Capsule/Cylinder height (for Capsule and Cylinder shapes)
        float Height = 6.0f;

        // Reverb settings
        ReverbPreset Preset = ReverbPreset::Room;
        ReverbParameters Parameters;

        // Additional audio filters
        AudioFilterParameters Filters;

        // Blend settings
        float BlendDistance = 2.0f;  // Distance over which to blend in/out
        float Priority = 0.0f;       // Higher priority zones override lower ones

        // State flags
        bool IsEnabled = true;
        bool IsGlobal = false;       // If true, affects all audio regardless of position
        bool AffectsListener = true; // Apply reverb based on listener position
        bool AffectsSources = false; // Apply reverb to sources inside zone

        // Runtime state (managed by system)
        bool ListenerInside = false;
        float CurrentBlendFactor = 0.0f;

        // ========================================================================
        // Factory Methods
        // ========================================================================

        static ReverbZoneComponent CreateRoom(const glm::vec3& halfExtents) {
            ReverbZoneComponent zone;
            zone.ZoneName = "Room";
            zone.Shape = AudioZoneShape::Box;
            zone.BoxHalfExtents = halfExtents;
            zone.Preset = ReverbPreset::Room;
            zone.Parameters = ReverbParameters::FromPreset(ReverbPreset::Room);
            return zone;
        }

        static ReverbZoneComponent CreateCave(float radius) {
            ReverbZoneComponent zone;
            zone.ZoneName = "Cave";
            zone.Shape = AudioZoneShape::Sphere;
            zone.Radius = radius;
            zone.Preset = ReverbPreset::Cave;
            zone.Parameters = ReverbParameters::FromPreset(ReverbPreset::Cave);
            zone.Parameters.DecayTimeSeconds = 4.0f;
            return zone;
        }

        static ReverbZoneComponent CreateHallway(const glm::vec3& halfExtents) {
            ReverbZoneComponent zone;
            zone.ZoneName = "Hallway";
            zone.Shape = AudioZoneShape::Box;
            zone.BoxHalfExtents = halfExtents;
            zone.Preset = ReverbPreset::Hallway;
            zone.Parameters = ReverbParameters::FromPreset(ReverbPreset::Hallway);
            return zone;
        }

        static ReverbZoneComponent CreateOutdoor(float radius) {
            ReverbZoneComponent zone;
            zone.ZoneName = "Outdoor";
            zone.Shape = AudioZoneShape::Sphere;
            zone.Radius = radius;
            zone.Preset = ReverbPreset::Plain;
            zone.Parameters = ReverbParameters::FromPreset(ReverbPreset::Plain);
            zone.Parameters.WetDryMix = 0.1f;  // Very little reverb outdoors
            return zone;
        }

        static ReverbZoneComponent CreateConcertHall(const glm::vec3& halfExtents) {
            ReverbZoneComponent zone;
            zone.ZoneName = "ConcertHall";
            zone.Shape = AudioZoneShape::Box;
            zone.BoxHalfExtents = halfExtents;
            zone.Preset = ReverbPreset::ConcertHall;
            zone.Parameters = ReverbParameters::FromPreset(ReverbPreset::ConcertHall);
            return zone;
        }

        static ReverbZoneComponent CreateBathroom(const glm::vec3& halfExtents) {
            ReverbZoneComponent zone;
            zone.ZoneName = "Bathroom";
            zone.Shape = AudioZoneShape::Box;
            zone.BoxHalfExtents = halfExtents;
            zone.Preset = ReverbPreset::Bathroom;
            zone.Parameters = ReverbParameters::FromPreset(ReverbPreset::Bathroom);
            return zone;
        }

        static ReverbZoneComponent CreateUnderwater(float radius) {
            ReverbZoneComponent zone;
            zone.ZoneName = "Underwater";
            zone.Shape = AudioZoneShape::Sphere;
            zone.Radius = radius;
            zone.Preset = ReverbPreset::Underwater;
            zone.Parameters = ReverbParameters::FromPreset(ReverbPreset::Underwater);
            // Add underwater filter effects
            zone.Filters.LowPassCutoffHz = 800.0f;  // Muffle high frequencies
            zone.Filters.VolumeAdjustmentDb = -6.0f;
            return zone;
        }

        static ReverbZoneComponent CreateForest(float radius) {
            ReverbZoneComponent zone;
            zone.ZoneName = "Forest";
            zone.Shape = AudioZoneShape::Sphere;
            zone.Radius = radius;
            zone.Preset = ReverbPreset::Forest;
            zone.Parameters = ReverbParameters::FromPreset(ReverbPreset::Forest);
            return zone;
        }

        static ReverbZoneComponent CreateSewer(float radius, float height) {
            ReverbZoneComponent zone;
            zone.ZoneName = "Sewer";
            zone.Shape = AudioZoneShape::Cylinder;
            zone.Radius = radius;
            zone.Height = height;
            zone.Preset = ReverbPreset::SewerPipe;
            zone.Parameters = ReverbParameters::FromPreset(ReverbPreset::SewerPipe);
            zone.Filters.EnableEcho = true;
            zone.Filters.EchoDelayMs = 150.0f;
            return zone;
        }

        static ReverbZoneComponent CreateGlobal(ReverbPreset preset) {
            ReverbZoneComponent zone;
            zone.ZoneName = "GlobalAmbience";
            zone.IsGlobal = true;
            zone.Priority = -100.0f;  // Lowest priority
            zone.Preset = preset;
            zone.Parameters = ReverbParameters::FromPreset(preset);
            return zone;
        }
    };

    // ============================================================================
    // Inline Implementation of ReverbParameters::FromPreset
    // ============================================================================

    inline ReverbParameters ReverbParameters::FromPreset(ReverbPreset preset) {
        ReverbParameters params;

        switch (preset) {
            case ReverbPreset::None:
                params.WetDryMix = 0.0f;
                params.DecayTimeSeconds = 0.0f;
                break;

            case ReverbPreset::Room:
                params.PreDelayMs = 5.0f;
                params.RoomSize = 0.3f;
                params.DecayTimeSeconds = 0.4f;
                params.HighFrequencyDamping = 0.6f;
                params.Diffusion = 0.8f;
                params.EarlyReflectionsGainDb = -4.0f;
                params.LateReverbGainDb = -8.0f;
                params.WetDryMix = 0.25f;
                break;

            case ReverbPreset::LivingRoom:
                params.PreDelayMs = 8.0f;
                params.RoomSize = 0.4f;
                params.DecayTimeSeconds = 0.5f;
                params.HighFrequencyDamping = 0.7f;
                params.Diffusion = 0.75f;
                params.EarlyReflectionsGainDb = -5.0f;
                params.LateReverbGainDb = -10.0f;
                params.WetDryMix = 0.2f;
                break;

            case ReverbPreset::Bathroom:
                params.PreDelayMs = 3.0f;
                params.RoomSize = 0.25f;
                params.DecayTimeSeconds = 1.2f;
                params.HighFrequencyDamping = 0.2f;  // Bright reflections
                params.Diffusion = 0.9f;
                params.EarlyReflectionsGainDb = -2.0f;
                params.LateReverbGainDb = -4.0f;
                params.WetDryMix = 0.4f;
                break;

            case ReverbPreset::StoneRoom:
                params.PreDelayMs = 10.0f;
                params.RoomSize = 0.5f;
                params.DecayTimeSeconds = 1.8f;
                params.HighFrequencyDamping = 0.3f;
                params.Diffusion = 0.7f;
                params.EarlyReflectionsGainDb = -3.0f;
                params.LateReverbGainDb = -6.0f;
                params.WetDryMix = 0.35f;
                break;

            case ReverbPreset::Auditorium:
                params.PreDelayMs = 20.0f;
                params.RoomSize = 0.75f;
                params.DecayTimeSeconds = 2.5f;
                params.HighFrequencyDamping = 0.5f;
                params.Diffusion = 0.85f;
                params.EarlyReflectionsGainDb = -6.0f;
                params.LateReverbGainDb = -8.0f;
                params.WetDryMix = 0.3f;
                break;

            case ReverbPreset::ConcertHall:
                params.PreDelayMs = 25.0f;
                params.RoomSize = 0.9f;
                params.DecayTimeSeconds = 3.0f;
                params.HighFrequencyDamping = 0.4f;
                params.Diffusion = 0.9f;
                params.EarlyReflectionsGainDb = -8.0f;
                params.LateReverbGainDb = -6.0f;
                params.WetDryMix = 0.35f;
                params.Density = 0.85f;
                break;

            case ReverbPreset::Cave:
                params.PreDelayMs = 30.0f;
                params.RoomSize = 0.8f;
                params.DecayTimeSeconds = 4.0f;
                params.HighFrequencyDamping = 0.6f;
                params.Diffusion = 0.6f;
                params.EarlyReflectionsGainDb = -4.0f;
                params.LateReverbGainDb = -5.0f;
                params.WetDryMix = 0.45f;
                params.LowFrequencyRatio = 1.2f;
                break;

            case ReverbPreset::Arena:
                params.PreDelayMs = 35.0f;
                params.RoomSize = 0.95f;
                params.DecayTimeSeconds = 5.0f;
                params.HighFrequencyDamping = 0.5f;
                params.Diffusion = 0.7f;
                params.EarlyReflectionsGainDb = -10.0f;
                params.LateReverbGainDb = -6.0f;
                params.WetDryMix = 0.4f;
                break;

            case ReverbPreset::Hangar:
                params.PreDelayMs = 40.0f;
                params.RoomSize = 1.0f;
                params.DecayTimeSeconds = 6.0f;
                params.HighFrequencyDamping = 0.4f;
                params.Diffusion = 0.5f;
                params.EarlyReflectionsGainDb = -6.0f;
                params.LateReverbGainDb = -4.0f;
                params.WetDryMix = 0.5f;
                break;

            case ReverbPreset::Hallway:
                params.PreDelayMs = 8.0f;
                params.RoomSize = 0.35f;
                params.DecayTimeSeconds = 1.5f;
                params.HighFrequencyDamping = 0.5f;
                params.Diffusion = 0.6f;
                params.EarlyReflectionsGainDb = -3.0f;
                params.LateReverbGainDb = -8.0f;
                params.WetDryMix = 0.3f;
                break;

            case ReverbPreset::StoneCorridor:
                params.PreDelayMs = 12.0f;
                params.RoomSize = 0.4f;
                params.DecayTimeSeconds = 2.0f;
                params.HighFrequencyDamping = 0.35f;
                params.Diffusion = 0.5f;
                params.EarlyReflectionsGainDb = -2.0f;
                params.LateReverbGainDb = -6.0f;
                params.WetDryMix = 0.35f;
                break;

            case ReverbPreset::Alley:
                params.PreDelayMs = 15.0f;
                params.RoomSize = 0.45f;
                params.DecayTimeSeconds = 1.0f;
                params.HighFrequencyDamping = 0.6f;
                params.Diffusion = 0.4f;
                params.EarlyReflectionsGainDb = -5.0f;
                params.LateReverbGainDb = -12.0f;
                params.WetDryMix = 0.2f;
                break;

            case ReverbPreset::Forest:
                params.PreDelayMs = 50.0f;
                params.RoomSize = 0.6f;
                params.DecayTimeSeconds = 0.8f;
                params.HighFrequencyDamping = 0.8f;
                params.Diffusion = 0.3f;
                params.EarlyReflectionsGainDb = -12.0f;
                params.LateReverbGainDb = -18.0f;
                params.WetDryMix = 0.1f;
                break;

            case ReverbPreset::City:
                params.PreDelayMs = 20.0f;
                params.RoomSize = 0.5f;
                params.DecayTimeSeconds = 0.7f;
                params.HighFrequencyDamping = 0.7f;
                params.Diffusion = 0.5f;
                params.EarlyReflectionsGainDb = -8.0f;
                params.LateReverbGainDb = -14.0f;
                params.WetDryMix = 0.15f;
                break;

            case ReverbPreset::Mountains:
                params.PreDelayMs = 100.0f;
                params.RoomSize = 0.7f;
                params.DecayTimeSeconds = 3.0f;
                params.HighFrequencyDamping = 0.5f;
                params.Diffusion = 0.2f;
                params.EarlyReflectionsGainDb = -15.0f;
                params.LateReverbGainDb = -20.0f;
                params.WetDryMix = 0.15f;
                break;

            case ReverbPreset::Quarry:
                params.PreDelayMs = 60.0f;
                params.RoomSize = 0.85f;
                params.DecayTimeSeconds = 2.5f;
                params.HighFrequencyDamping = 0.4f;
                params.Diffusion = 0.4f;
                params.EarlyReflectionsGainDb = -8.0f;
                params.LateReverbGainDb = -10.0f;
                params.WetDryMix = 0.25f;
                break;

            case ReverbPreset::Plain:
                params.PreDelayMs = 80.0f;
                params.RoomSize = 0.3f;
                params.DecayTimeSeconds = 0.5f;
                params.HighFrequencyDamping = 0.9f;
                params.Diffusion = 0.2f;
                params.EarlyReflectionsGainDb = -20.0f;
                params.LateReverbGainDb = -30.0f;
                params.WetDryMix = 0.05f;
                break;

            case ReverbPreset::ParkingLot:
                params.PreDelayMs = 15.0f;
                params.RoomSize = 0.65f;
                params.DecayTimeSeconds = 2.0f;
                params.HighFrequencyDamping = 0.45f;
                params.Diffusion = 0.55f;
                params.EarlyReflectionsGainDb = -4.0f;
                params.LateReverbGainDb = -8.0f;
                params.WetDryMix = 0.35f;
                break;

            case ReverbPreset::SewerPipe:
                params.PreDelayMs = 6.0f;
                params.RoomSize = 0.2f;
                params.DecayTimeSeconds = 2.5f;
                params.HighFrequencyDamping = 0.5f;
                params.Diffusion = 0.85f;
                params.EarlyReflectionsGainDb = -2.0f;
                params.LateReverbGainDb = -4.0f;
                params.WetDryMix = 0.5f;
                params.LowFrequencyRatio = 1.3f;
                break;

            case ReverbPreset::Underwater:
                params.PreDelayMs = 2.0f;
                params.RoomSize = 0.8f;
                params.DecayTimeSeconds = 1.5f;
                params.HighFrequencyDamping = 0.9f;  // Water absorbs high frequencies
                params.Diffusion = 0.95f;
                params.EarlyReflectionsGainDb = 0.0f;
                params.LateReverbGainDb = -2.0f;
                params.WetDryMix = 0.6f;
                params.LowFrequencyRatio = 1.5f;
                params.ModulationDepth = 0.3f;  // Underwater "wobble"
                params.ModulationRateHz = 2.0f;
                break;

            case ReverbPreset::Custom:
            default:
                // Use default values
                break;
        }

        return params;
    }

} // namespace ECS
} // namespace Core
