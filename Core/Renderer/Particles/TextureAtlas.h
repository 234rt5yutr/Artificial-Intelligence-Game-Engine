#pragma once

/**
 * @file TextureAtlas.h
 * @brief Texture atlas (sprite sheet) support for animated particle VFX
 * 
 * Provides functionality to manage texture atlas metadata and calculate UV
 * coordinates for individual frames. Supports multiple animation modes for
 * various visual effects including explosions, fire, smoke, and magic spells.
 * 
 * @section Usage
 * @code
 * // Create an atlas for a 4x4 sprite sheet with 12 animation frames
 * TextureAtlas atlas;
 * atlas.Columns = 4;
 * atlas.Rows = 4;
 * atlas.FrameCount = 12;  // Only first 12 cells are used
 * atlas.FrameRate = 24.0f;
 * atlas.AnimationMode = AtlasAnimationMode::OverLifetime;
 * 
 * // Get UV coordinates for frame 5
 * glm::vec4 uvRect = atlas.GetFrameUV(5);  // Returns (uMin, vMin, uMax, vMax)
 * 
 * // Get frame index from normalized particle age
 * uint32_t frame = atlas.GetFrameFromAge(0.5f);
 * @endcode
 * 
 * @section GPU Integration
 * The atlas parameters are designed to be efficiently passed to shaders:
 * - Grid dimensions (columns, rows) for UV offset calculation
 * - Frame count and current frame for animation
 * - Animation mode for compute shader logic
 */

#include "Core/Math/Math.h"
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace Core {
namespace Renderer {
namespace Particles {

    //=========================================================================
    // Animation Mode Enumeration
    //=========================================================================

    /**
     * @brief Defines how particles animate through texture atlas frames
     */
    enum class AtlasAnimationMode : uint32_t {
        /**
         * Frame advances from 0 to frameCount based on particle age.
         * At birth (age=0), frame=0. At death (age=lifetime), frame=frameCount-1.
         * Ideal for: explosions, dissolves, transformations
         */
        OverLifetime = 0,

        /**
         * Frame changes based on particle velocity magnitude.
         * Faster particles show higher frame indices.
         * Ideal for: motion blur, speed lines, streaks
         */
        BySpeed = 1,

        /**
         * Each particle randomly selects a frame at spawn time.
         * Frame remains constant throughout particle lifetime.
         * Ideal for: debris variation, leaf types, crystal variants
         */
        Random = 2,

        /**
         * All particles use the same fixed frame index.
         * Useful for static sprite selection or sub-atlas organization.
         * Ideal for: particle type selection, icon particles
         */
        Fixed = 3,

        /**
         * Frame advances based on real time (FPS-based animation).
         * All particles sharing an emitter show synchronized animation.
         * Ideal for: flickering flames, energy fields, portals
         */
        RealTime = 4,

        /**
         * Frame cycles through atlas in ping-pong fashion.
         * 0 -> frameCount-1 -> 0 -> frameCount-1 -> ...
         * Ideal for: breathing effects, pulsing, oscillations
         */
        PingPong = 5
    };

    //=========================================================================
    // Atlas Blending Mode
    //=========================================================================

    /**
     * @brief Defines how to blend between adjacent frames (optional interpolation)
     */
    enum class AtlasBlendMode : uint32_t {
        /**
         * No interpolation - snap to nearest frame.
         * Best performance, works well for high frame-rate animations.
         */
        None = 0,

        /**
         * Linear interpolation between current and next frame.
         * Smoother animation but requires two texture samples.
         */
        LinearBlend = 1
    };

    //=========================================================================
    // Texture Atlas Structure
    //=========================================================================

    /**
     * @brief Texture atlas metadata and UV calculation for animated particles
     * 
     * This structure manages all the information needed to sample from a
     * sprite sheet texture atlas and animate particles through frames.
     * 
     * @note Atlas layout assumes row-major ordering where frame 0 is at
     *       the top-left corner, and frames proceed left-to-right, top-to-bottom.
     * 
     * @verbatim
     * Example 4x2 atlas layout (8 frames):
     * +---+---+---+---+
     * | 0 | 1 | 2 | 3 |  Row 0
     * +---+---+---+---+
     * | 4 | 5 | 6 | 7 |  Row 1
     * +---+---+---+---+
     *  Col 0 1 2 3
     * @endverbatim
     */
    struct TextureAtlas {
        //---------------------------------------------------------------------
        // Grid Configuration
        //---------------------------------------------------------------------

        /** Number of columns in the atlas grid (horizontal divisions) */
        uint32_t Columns = 1;

        /** Number of rows in the atlas grid (vertical divisions) */
        uint32_t Rows = 1;

        /** 
         * Total number of animation frames.
         * May be less than Columns * Rows if the last row is not fully populated.
         */
        uint32_t FrameCount = 1;

        //---------------------------------------------------------------------
        // Animation Configuration
        //---------------------------------------------------------------------

        /** Frames per second for time-based animation (RealTime mode) */
        float FrameRate = 30.0f;

        /** Whether the animation loops after reaching the last frame */
        bool Loop = true;

        /** Animation mode determining how frame selection works */
        AtlasAnimationMode AnimationMode = AtlasAnimationMode::OverLifetime;

        /** Blending mode for inter-frame interpolation */
        AtlasBlendMode BlendMode = AtlasBlendMode::None;

        //---------------------------------------------------------------------
        // Speed-Based Animation Parameters (BySpeed mode)
        //---------------------------------------------------------------------

        /** Minimum speed for frame 0 (BySpeed mode) */
        float SpeedMin = 0.0f;

        /** Maximum speed for frame (frameCount-1) (BySpeed mode) */
        float SpeedMax = 10.0f;

        //---------------------------------------------------------------------
        // Fixed Mode Parameters
        //---------------------------------------------------------------------

        /** Fixed frame index when using Fixed animation mode */
        uint32_t FixedFrameIndex = 0;

        //---------------------------------------------------------------------
        // Random Mode Parameters
        //---------------------------------------------------------------------

        /** Start frame for random selection range (inclusive) */
        uint32_t RandomFrameMin = 0;

        /** End frame for random selection range (inclusive) */
        uint32_t RandomFrameMax = UINT32_MAX;  // Will be clamped to FrameCount-1

        //---------------------------------------------------------------------
        // Constructors
        //---------------------------------------------------------------------

        TextureAtlas() = default;

        /**
         * @brief Construct atlas with grid dimensions
         * @param columns Number of columns in atlas
         * @param rows Number of rows in atlas
         * @param frameCount Total frames (default = columns * rows)
         */
        TextureAtlas(uint32_t columns, uint32_t rows, uint32_t frameCount = 0)
            : Columns(std::max(1u, columns))
            , Rows(std::max(1u, rows))
            , FrameCount(frameCount > 0 ? frameCount : columns * rows)
        {}

        /**
         * @brief Construct atlas with full animation parameters
         */
        TextureAtlas(uint32_t columns, uint32_t rows, uint32_t frameCount,
                     float frameRate, bool loop, AtlasAnimationMode mode)
            : Columns(std::max(1u, columns))
            , Rows(std::max(1u, rows))
            , FrameCount(frameCount > 0 ? frameCount : columns * rows)
            , FrameRate(frameRate)
            , Loop(loop)
            , AnimationMode(mode)
        {}

        //---------------------------------------------------------------------
        // UV Coordinate Calculation
        //---------------------------------------------------------------------

        /**
         * @brief Calculate UV rectangle for a given frame index
         * 
         * Returns UV coordinates for the specified frame in the atlas.
         * The returned vec4 contains (uMin, vMin, uMax, vMax) where:
         * - uMin, vMin: bottom-left corner of the frame
         * - uMax, vMax: top-right corner of the frame
         * 
         * @param frameIndex Frame index (0 to FrameCount-1)
         * @return UV rectangle as (uMin, vMin, uMax, vMax)
         * 
         * @note Frame indices are clamped to valid range if out of bounds.
         *       UV coordinates assume standard OpenGL/Vulkan texture coordinate
         *       system where (0,0) is at the top-left of the texture.
         */
        [[nodiscard]] Math::Vec4 GetFrameUV(uint32_t frameIndex) const {
            // Clamp frame index to valid range
            frameIndex = std::min(frameIndex, std::max(1u, FrameCount) - 1);

            // Calculate cell dimensions in UV space
            const float cellWidth = 1.0f / static_cast<float>(std::max(1u, Columns));
            const float cellHeight = 1.0f / static_cast<float>(std::max(1u, Rows));

            // Calculate column and row from frame index (row-major order)
            const uint32_t col = frameIndex % Columns;
            const uint32_t row = frameIndex / Columns;

            // Calculate UV bounds
            const float uMin = static_cast<float>(col) * cellWidth;
            const float vMin = static_cast<float>(row) * cellHeight;
            const float uMax = uMin + cellWidth;
            const float vMax = vMin + cellHeight;

            return Math::Vec4(uMin, vMin, uMax, vMax);
        }

        /**
         * @brief Calculate UV offset and scale for shader use
         * 
         * Returns offset (xy) and scale (zw) for transforming base UVs:
         * finalUV = baseUV * scale + offset
         * 
         * @param frameIndex Frame index (0 to FrameCount-1)
         * @return Vec4(offsetU, offsetV, scaleU, scaleV)
         */
        [[nodiscard]] Math::Vec4 GetFrameUVOffsetScale(uint32_t frameIndex) const {
            frameIndex = std::min(frameIndex, std::max(1u, FrameCount) - 1);

            const float scaleU = 1.0f / static_cast<float>(std::max(1u, Columns));
            const float scaleV = 1.0f / static_cast<float>(std::max(1u, Rows));

            const uint32_t col = frameIndex % Columns;
            const uint32_t row = frameIndex / Columns;

            const float offsetU = static_cast<float>(col) * scaleU;
            const float offsetV = static_cast<float>(row) * scaleV;

            return Math::Vec4(offsetU, offsetV, scaleU, scaleV);
        }

        //---------------------------------------------------------------------
        // Frame Selection Methods
        //---------------------------------------------------------------------

        /**
         * @brief Calculate frame index from normalized particle age (0-1)
         * 
         * Used for OverLifetime animation mode. Maps the particle's age ratio
         * to a frame index in the animation sequence.
         * 
         * @param normalizedAge Particle age as ratio (0.0 = birth, 1.0 = death)
         * @return Frame index for the given age
         */
        [[nodiscard]] uint32_t GetFrameFromAge(float normalizedAge) const {
            if (FrameCount <= 1) return 0;

            normalizedAge = std::clamp(normalizedAge, 0.0f, 1.0f);

            if (Loop) {
                // For looping, wrap around using modulo
                const float frameFloat = normalizedAge * static_cast<float>(FrameCount);
                return static_cast<uint32_t>(frameFloat) % FrameCount;
            } else {
                // For non-looping, clamp to last frame
                const float frameFloat = normalizedAge * static_cast<float>(FrameCount - 1);
                return std::min(static_cast<uint32_t>(frameFloat), FrameCount - 1);
            }
        }

        /**
         * @brief Calculate frame index from particle speed (BySpeed mode)
         * 
         * @param speed Current particle velocity magnitude
         * @return Frame index based on speed range mapping
         */
        [[nodiscard]] uint32_t GetFrameFromSpeed(float speed) const {
            if (FrameCount <= 1) return 0;
            if (SpeedMax <= SpeedMin) return 0;

            const float normalized = std::clamp(
                (speed - SpeedMin) / (SpeedMax - SpeedMin),
                0.0f, 1.0f
            );

            return std::min(
                static_cast<uint32_t>(normalized * static_cast<float>(FrameCount - 1)),
                FrameCount - 1
            );
        }

        /**
         * @brief Calculate frame index from elapsed time (RealTime mode)
         * 
         * @param elapsedTime Total elapsed time in seconds
         * @return Frame index based on time and frame rate
         */
        [[nodiscard]] uint32_t GetFrameFromTime(float elapsedTime) const {
            if (FrameCount <= 1 || FrameRate <= 0.0f) return 0;

            const float totalFrames = elapsedTime * FrameRate;

            if (Loop) {
                return static_cast<uint32_t>(std::fmod(totalFrames, static_cast<float>(FrameCount)));
            } else {
                return std::min(static_cast<uint32_t>(totalFrames), FrameCount - 1);
            }
        }

        /**
         * @brief Calculate frame index for ping-pong animation
         * 
         * @param normalizedAge Particle age as ratio (0.0 = birth, 1.0 = death)
         * @return Frame index oscillating between 0 and FrameCount-1
         */
        [[nodiscard]] uint32_t GetFrameFromAgePingPong(float normalizedAge) const {
            if (FrameCount <= 1) return 0;

            normalizedAge = std::clamp(normalizedAge, 0.0f, 1.0f);

            // Map age to a position in the ping-pong cycle (0 to 2)
            // 0-1: forward pass, 1-2: backward pass
            const float cycleCount = Loop ? 2.0f : 1.0f;
            const float cyclePos = std::fmod(normalizedAge * cycleCount, 2.0f);

            float frameProgress;
            if (cyclePos < 1.0f) {
                // Forward pass: 0 -> frameCount-1
                frameProgress = cyclePos;
            } else {
                // Backward pass: frameCount-1 -> 0
                frameProgress = 2.0f - cyclePos;
            }

            return std::min(
                static_cast<uint32_t>(frameProgress * static_cast<float>(FrameCount - 1)),
                FrameCount - 1
            );
        }

        /**
         * @brief Get a random frame index within configured range
         * 
         * @param randomValue Random value in range [0, 1]
         * @return Random frame index
         */
        [[nodiscard]] uint32_t GetRandomFrame(float randomValue) const {
            if (FrameCount <= 1) return 0;

            const uint32_t rangeMin = std::min(RandomFrameMin, FrameCount - 1);
            const uint32_t rangeMax = std::min(
                RandomFrameMax == UINT32_MAX ? FrameCount - 1 : RandomFrameMax,
                FrameCount - 1
            );

            if (rangeMax <= rangeMin) return rangeMin;

            const uint32_t range = rangeMax - rangeMin + 1;
            return rangeMin + static_cast<uint32_t>(randomValue * static_cast<float>(range)) % range;
        }

        //---------------------------------------------------------------------
        // Frame Blending (for LinearBlend mode)
        //---------------------------------------------------------------------

        /**
         * @brief Get current and next frame indices with blend factor
         * 
         * Used when BlendMode is LinearBlend to smoothly interpolate between frames.
         * 
         * @param normalizedAge Particle age ratio (0.0 = birth, 1.0 = death)
         * @param[out] frame0 Current frame index
         * @param[out] frame1 Next frame index
         * @param[out] blendFactor Interpolation factor (0 = frame0, 1 = frame1)
         */
        void GetBlendedFrames(float normalizedAge, 
                              uint32_t& frame0, 
                              uint32_t& frame1, 
                              float& blendFactor) const {
            if (FrameCount <= 1) {
                frame0 = 0;
                frame1 = 0;
                blendFactor = 0.0f;
                return;
            }

            normalizedAge = std::clamp(normalizedAge, 0.0f, 1.0f);

            const float frameFloat = normalizedAge * static_cast<float>(FrameCount - 1);
            frame0 = static_cast<uint32_t>(frameFloat);
            blendFactor = frameFloat - static_cast<float>(frame0);

            if (Loop) {
                frame1 = (frame0 + 1) % FrameCount;
            } else {
                frame1 = std::min(frame0 + 1, FrameCount - 1);
            }

            frame0 = std::min(frame0, FrameCount - 1);
        }

        //---------------------------------------------------------------------
        // Validation and Utility
        //---------------------------------------------------------------------

        /**
         * @brief Check if atlas configuration is valid
         */
        [[nodiscard]] bool IsValid() const {
            return Columns > 0 && Rows > 0 && FrameCount > 0 &&
                   FrameCount <= Columns * Rows;
        }

        /**
         * @brief Check if this is effectively a single-frame (non-animated) atlas
         */
        [[nodiscard]] bool IsSingleFrame() const {
            return FrameCount <= 1;
        }

        /**
         * @brief Get the total number of cells in the grid
         */
        [[nodiscard]] uint32_t GetTotalCells() const {
            return Columns * Rows;
        }

        /**
         * @brief Get animation duration in seconds (for OverLifetime mode reference)
         * @param particleLifetime Particle lifetime in seconds
         * @return Duration of one animation cycle
         */
        [[nodiscard]] float GetAnimationDuration(float particleLifetime) const {
            if (AnimationMode == AtlasAnimationMode::RealTime && FrameRate > 0.0f) {
                return static_cast<float>(FrameCount) / FrameRate;
            }
            return particleLifetime;
        }

        /**
         * @brief Reset to default single-frame state
         */
        void Reset() {
            Columns = 1;
            Rows = 1;
            FrameCount = 1;
            FrameRate = 30.0f;
            Loop = true;
            AnimationMode = AtlasAnimationMode::OverLifetime;
            BlendMode = AtlasBlendMode::None;
            SpeedMin = 0.0f;
            SpeedMax = 10.0f;
            FixedFrameIndex = 0;
            RandomFrameMin = 0;
            RandomFrameMax = UINT32_MAX;
        }

        //---------------------------------------------------------------------
        // Serialization Helpers (for GPU upload)
        //---------------------------------------------------------------------

        /**
         * @brief Pack atlas dimensions for GPU uniform
         * @return Packed value: (columns | (rows << 16))
         */
        [[nodiscard]] uint32_t PackDimensions() const {
            return (Columns & 0xFFFF) | ((Rows & 0xFFFF) << 16);
        }

        /**
         * @brief Pack animation parameters for GPU uniform
         * @return Packed value containing mode and flags
         */
        [[nodiscard]] uint32_t PackAnimationParams() const {
            uint32_t packed = static_cast<uint32_t>(AnimationMode) & 0xFF;
            packed |= (static_cast<uint32_t>(BlendMode) & 0xFF) << 8;
            packed |= (Loop ? 1u : 0u) << 16;
            return packed;
        }
    };

    //=========================================================================
    // GPU-Aligned Atlas Data (for shader upload)
    //=========================================================================

    /**
     * @brief GPU-aligned atlas parameters for uniform buffer upload
     * 
     * This structure is designed to be uploaded directly to a GPU uniform
     * or storage buffer for use in particle shaders.
     */
    struct alignas(16) AtlasGPUData {
        /** Inverse of columns (1.0 / columns) for UV calculation */
        float InvColumns;

        /** Inverse of rows (1.0 / rows) for UV calculation */
        float InvRows;

        /** Total frame count */
        uint32_t FrameCount;

        /** Animation mode (AtlasAnimationMode enum) */
        uint32_t AnimationMode;

        /** Frame rate for RealTime mode */
        float FrameRate;

        /** Speed minimum for BySpeed mode */
        float SpeedMin;

        /** Speed maximum for BySpeed mode */
        float SpeedMax;

        /** Loop flag (1 = loop, 0 = clamp) */
        uint32_t Loop;

        /**
         * @brief Initialize from TextureAtlas
         */
        static AtlasGPUData FromAtlas(const TextureAtlas& atlas) {
            AtlasGPUData data{};
            data.InvColumns = 1.0f / static_cast<float>(std::max(1u, atlas.Columns));
            data.InvRows = 1.0f / static_cast<float>(std::max(1u, atlas.Rows));
            data.FrameCount = atlas.FrameCount;
            data.AnimationMode = static_cast<uint32_t>(atlas.AnimationMode);
            data.FrameRate = atlas.FrameRate;
            data.SpeedMin = atlas.SpeedMin;
            data.SpeedMax = atlas.SpeedMax;
            data.Loop = atlas.Loop ? 1u : 0u;
            return data;
        }
    };
    static_assert(sizeof(AtlasGPUData) == 32, "AtlasGPUData must be 32 bytes");

    //=========================================================================
    // Preset Atlas Configurations
    //=========================================================================

    namespace AtlasPresets {

        /**
         * @brief Create atlas for typical explosion animation
         */
        inline TextureAtlas CreateExplosion(uint32_t columns = 4, uint32_t rows = 4) {
            TextureAtlas atlas(columns, rows);
            atlas.FrameRate = 30.0f;
            atlas.Loop = false;
            atlas.AnimationMode = AtlasAnimationMode::OverLifetime;
            return atlas;
        }

        /**
         * @brief Create atlas for looping fire animation
         */
        inline TextureAtlas CreateFire(uint32_t columns = 4, uint32_t rows = 4) {
            TextureAtlas atlas(columns, rows);
            atlas.FrameRate = 24.0f;
            atlas.Loop = true;
            atlas.AnimationMode = AtlasAnimationMode::RealTime;
            return atlas;
        }

        /**
         * @brief Create atlas for smoke with random variation
         */
        inline TextureAtlas CreateSmoke(uint32_t columns = 4, uint32_t rows = 2) {
            TextureAtlas atlas(columns, rows);
            atlas.AnimationMode = AtlasAnimationMode::Random;
            return atlas;
        }

        /**
         * @brief Create atlas for magic sparkles with ping-pong
         */
        inline TextureAtlas CreateMagic(uint32_t columns = 4, uint32_t rows = 1) {
            TextureAtlas atlas(columns, rows);
            atlas.FrameRate = 15.0f;
            atlas.Loop = true;
            atlas.AnimationMode = AtlasAnimationMode::PingPong;
            return atlas;
        }

        /**
         * @brief Create atlas for speed-based motion blur
         */
        inline TextureAtlas CreateMotionBlur(uint32_t columns = 4, uint32_t rows = 1,
                                              float minSpeed = 0.0f, float maxSpeed = 20.0f) {
            TextureAtlas atlas(columns, rows);
            atlas.AnimationMode = AtlasAnimationMode::BySpeed;
            atlas.SpeedMin = minSpeed;
            atlas.SpeedMax = maxSpeed;
            return atlas;
        }

        /**
         * @brief Create atlas for debris/rocks with random selection
         */
        inline TextureAtlas CreateDebris(uint32_t variants = 8) {
            // Arrange variants in a single row or optimal grid
            uint32_t cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(variants))));
            uint32_t rows = (variants + cols - 1) / cols;
            
            TextureAtlas atlas(cols, rows, variants);
            atlas.AnimationMode = AtlasAnimationMode::Random;
            return atlas;
        }

    } // namespace AtlasPresets

} // namespace Particles
} // namespace Renderer
} // namespace Core
