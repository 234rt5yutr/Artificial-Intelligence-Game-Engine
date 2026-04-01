#pragma once

#include "Core/Math/Math.h"
#include <entt/entt.hpp>

namespace Core {
namespace ECS {

    struct TimeOfDayComponent {
        // Current time in 24-hour format (0.0 - 24.0)
        float CurrentTime = 12.0f;

        // Time progression speed (1.0 = real-time, 60.0 = 1 minute = 1 hour)
        float TimeSpeed = 1.0f;

        // Use system clock for real-time sync
        bool IsRealTime = false;

        // Dawn/Dusk timing (hours)
        float SunriseTime = 6.0f;
        float SunsetTime = 18.0f;

        // Moon properties
        float MoonPhase = 0.0f;  // 0.0 = new moon, 0.5 = full moon, 1.0 = new moon
        Math::Vec3 MoonColor{ 0.8f, 0.85f, 1.0f };
        float MoonIntensity = 0.15f;

        // Night sky
        float StarIntensity = 1.0f;  // Multiplier for star brightness
        float StarDensity = 0.5f;    // Controls how many stars are visible

        // Transition parameters
        float TransitionDuration = 0.5f;  // Hours for dawn/dusk color transitions

        // Linked directional light entity for sun/moon
        entt::entity LinkedDirectionalLight{ entt::null };

        // Sky color overrides for different times of day
        Math::Vec3 NoonZenithColor{ 0.15f, 0.35f, 0.65f };
        Math::Vec3 NoonHorizonColor{ 0.4f, 0.55f, 0.7f };
        Math::Vec3 SunsetZenithColor{ 0.15f, 0.15f, 0.3f };
        Math::Vec3 SunsetHorizonColor{ 1.0f, 0.5f, 0.2f };
        Math::Vec3 NightZenithColor{ 0.01f, 0.01f, 0.03f };
        Math::Vec3 NightHorizonColor{ 0.02f, 0.02f, 0.05f };

        // Ambient light multipliers for different times
        float DayAmbientIntensity = 0.3f;
        float NightAmbientIntensity = 0.05f;

        // Latitude for sun angle calculation (default: ~45 degrees)
        float Latitude = 45.0f;

        TimeOfDayComponent() = default;

        // Factory method for standard day/night cycle
        static TimeOfDayComponent CreateStandard(float startTime = 12.0f, float speed = 60.0f)
        {
            TimeOfDayComponent tod;
            tod.CurrentTime = startTime;
            tod.TimeSpeed = speed;
            return tod;
        }

        // Factory method for real-time sync
        static TimeOfDayComponent CreateRealTime()
        {
            TimeOfDayComponent tod;
            tod.IsRealTime = true;
            tod.TimeSpeed = 1.0f;
            return tod;
        }

        // Helper to check if it's currently daytime
        bool IsDaytime() const
        {
            return CurrentTime >= SunriseTime && CurrentTime <= SunsetTime;
        }

        // Helper to check if it's dawn (sunrise transition)
        bool IsDawn() const
        {
            return CurrentTime >= (SunriseTime - TransitionDuration) &&
                   CurrentTime <= (SunriseTime + TransitionDuration);
        }

        // Helper to check if it's dusk (sunset transition)
        bool IsDusk() const
        {
            return CurrentTime >= (SunsetTime - TransitionDuration) &&
                   CurrentTime <= (SunsetTime + TransitionDuration);
        }

        // Helper to get normalized time of day (0.0 = midnight, 0.5 = noon, 1.0 = midnight)
        float GetNormalizedTime() const
        {
            return CurrentTime / 24.0f;
        }

        // Helper to get sun elevation factor (0.0 = horizon, 1.0 = zenith)
        float GetSunElevationFactor() const
        {
            if (!IsDaytime()) return 0.0f;

            float dayLength = SunsetTime - SunriseTime;
            float dayProgress = (CurrentTime - SunriseTime) / dayLength;
            // Sine curve for sun arc
            return glm::sin(dayProgress * 3.14159265f);
        }
    };

} // namespace ECS
} // namespace Core
