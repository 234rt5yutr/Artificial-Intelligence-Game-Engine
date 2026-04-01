#version 450

/**
 * @file particle.vert
 * @brief GPU particle rendering vertex shader with texture atlas support
 * 
 * Renders particles as camera-facing billboards using instanced drawing.
 * Each instance represents one particle from the sorted particle buffer.
 * Supports texture atlas (sprite sheet) animation with multiple modes.
 * Outputs view-space depth for soft particle calculations.
 */

//=============================================================================
// Vertex Inputs (quad vertices)
//=============================================================================

// Pre-defined quad vertices (4 vertices, no vertex buffer needed)
// Generated procedurally from gl_VertexIndex
const vec2 quadVertices[4] = vec2[4](
    vec2(-0.5, -0.5),
    vec2( 0.5, -0.5),
    vec2(-0.5,  0.5),
    vec2( 0.5,  0.5)
);

const vec2 quadUVs[4] = vec2[4](
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(0.0, 0.0),
    vec2(1.0, 0.0)
);

//=============================================================================
// Constants - Atlas Animation Modes
//=============================================================================

#define ATLAS_MODE_OVER_LIFETIME 0
#define ATLAS_MODE_BY_SPEED      1
#define ATLAS_MODE_RANDOM        2
#define ATLAS_MODE_FIXED         3
#define ATLAS_MODE_REAL_TIME     4
#define ATLAS_MODE_PING_PONG     5

//=============================================================================
// Structures
//=============================================================================

struct Particle {
    vec3 position;
    float lifetime;
    vec3 velocity;
    float age;
    vec4 color;
    vec2 size;
    float rotation;
    uint flags;
};

//=============================================================================
// Uniform Buffer
//=============================================================================

layout (set = 0, binding = 0, std140) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec3 cameraPosition;
    float time;
    vec3 cameraRight;
    float padding0;
    vec3 cameraUp;
    float padding1;
    vec2 screenSize;           // Screen dimensions
    float nearPlane;           // Camera near plane
    float farPlane;            // Camera far plane
} camera;

//=============================================================================
// Storage Buffers
//=============================================================================

// Particle data
layout (set = 0, binding = 1, std430) readonly buffer ParticleBuffer {
    Particle particles[];
};

// Sorted indices (if sorting enabled, otherwise use alive list)
layout (set = 0, binding = 2, std430) readonly buffer SortedIndexBuffer {
    uint sortedIndices[];
};

// Alive list (used if sorting disabled)
layout (set = 0, binding = 3, std430) readonly buffer AliveListBuffer {
    uint aliveList[];
};

//=============================================================================
// Push Constants
//=============================================================================

layout (push_constant) uniform PushConstants {
    uint useSortedIndices;      // 1 = use sorted, 0 = use alive list
    uint softParticles;         // Enable soft particle edges
    float softParticleScale;    // Soft particle scale
    uint useTexture;            // Use texture sampling
    uint blendMode;             // Current blend mode
    float depthBias;            // Depth bias for layering
    // Atlas parameters
    uint atlasColumns;          // Number of columns in atlas (0 = no atlas)
    uint atlasRows;             // Number of rows in atlas
    uint atlasFrameCount;       // Total animation frames
    uint atlasAnimMode;         // Animation mode (AtlasAnimationMode enum)
    float atlasFrameRate;       // Frames per second for RealTime mode
    float atlasSpeedMin;        // Min speed for BySpeed mode
    float atlasSpeedMax;        // Max speed for BySpeed mode
    uint atlasLoop;             // Loop animation flag
} pc;

//=============================================================================
// Outputs to Fragment Shader
//=============================================================================

layout (location = 0) out vec2 fragUV;
layout (location = 1) out vec4 fragColor;
layout (location = 2) out float fragAge;
layout (location = 3) out float fragLifetime;
layout (location = 4) out float fragViewDepth;  // View-space depth for soft particles
// Atlas outputs
layout (location = 5) out vec2 fragAtlasUVOffset;   // UV offset for current frame
layout (location = 6) out vec2 fragAtlasUVScale;    // UV scale (1/cols, 1/rows)
layout (location = 7) out float fragAtlasBlendFactor; // Blend factor to next frame (for interpolation)
layout (location = 8) out vec2 fragAtlasNextUVOffset; // UV offset for next frame (for blending)

//=============================================================================
// Helper Functions
//=============================================================================

/**
 * @brief Create 2D rotation matrix
 */
mat2 rotation2D(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, -s, s, c);
}

/**
 * @brief Calculate view-space depth for a world position
 */
float getViewSpaceDepth(vec3 worldPos) {
    vec4 viewPos = camera.view * vec4(worldPos, 1.0);
    return viewPos.z; // Negative in view space (looking down -Z)
}

/**
 * @brief Calculate animation frame from particle age (OverLifetime mode)
 */
uint getFrameFromAge(float normalizedAge, uint frameCount, bool loop) {
    if (frameCount <= 1u) return 0u;
    
    normalizedAge = clamp(normalizedAge, 0.0, 1.0);
    
    if (loop) {
        float frameFloat = normalizedAge * float(frameCount);
        return uint(frameFloat) % frameCount;
    } else {
        float frameFloat = normalizedAge * float(frameCount - 1u);
        return min(uint(frameFloat), frameCount - 1u);
    }
}

/**
 * @brief Calculate animation frame from particle speed (BySpeed mode)
 */
uint getFrameFromSpeed(float speed, uint frameCount, float speedMin, float speedMax) {
    if (frameCount <= 1u) return 0u;
    if (speedMax <= speedMin) return 0u;
    
    float normalized = clamp((speed - speedMin) / (speedMax - speedMin), 0.0, 1.0);
    return min(uint(normalized * float(frameCount - 1u)), frameCount - 1u);
}

/**
 * @brief Calculate animation frame from elapsed time (RealTime mode)
 */
uint getFrameFromTime(float elapsedTime, uint frameCount, float frameRate, bool loop) {
    if (frameCount <= 1u || frameRate <= 0.0) return 0u;
    
    float totalFrames = elapsedTime * frameRate;
    
    if (loop) {
        return uint(mod(totalFrames, float(frameCount)));
    } else {
        return min(uint(totalFrames), frameCount - 1u);
    }
}

/**
 * @brief Calculate animation frame for ping-pong mode
 */
uint getFrameFromAgePingPong(float normalizedAge, uint frameCount, bool loop) {
    if (frameCount <= 1u) return 0u;
    
    normalizedAge = clamp(normalizedAge, 0.0, 1.0);
    
    // Map age to a position in the ping-pong cycle (0 to 2)
    float cycleCount = loop ? 2.0 : 1.0;
    float cyclePos = mod(normalizedAge * cycleCount, 2.0);
    
    float frameProgress;
    if (cyclePos < 1.0) {
        // Forward pass: 0 -> frameCount-1
        frameProgress = cyclePos;
    } else {
        // Backward pass: frameCount-1 -> 0
        frameProgress = 2.0 - cyclePos;
    }
    
    return min(uint(frameProgress * float(frameCount - 1u)), frameCount - 1u);
}

/**
 * @brief Get a pseudo-random frame based on particle index
 */
uint getRandomFrame(uint particleIdx, uint frameCount) {
    if (frameCount <= 1u) return 0u;
    
    // Simple hash function for randomization
    uint hash = particleIdx * 747796405u + 2891336453u;
    hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
    hash = (hash >> 22u) ^ hash;
    
    return hash % frameCount;
}

/**
 * @brief Calculate UV offset for a given frame
 */
vec2 getFrameUVOffset(uint frameIndex, uint columns, uint rows) {
    float invCols = 1.0 / float(columns);
    float invRows = 1.0 / float(rows);
    
    uint col = frameIndex % columns;
    uint row = frameIndex / columns;
    
    return vec2(float(col) * invCols, float(row) * invRows);
}

/**
 * @brief Calculate current frame and blend factor for smooth animation
 */
void getBlendedFrames(float normalizedAge, uint frameCount, bool loop,
                       out uint frame0, out uint frame1, out float blendFactor) {
    if (frameCount <= 1u) {
        frame0 = 0u;
        frame1 = 0u;
        blendFactor = 0.0;
        return;
    }
    
    normalizedAge = clamp(normalizedAge, 0.0, 1.0);
    
    float frameFloat = normalizedAge * float(frameCount - 1u);
    frame0 = uint(frameFloat);
    blendFactor = fract(frameFloat);
    
    if (loop) {
        frame1 = (frame0 + 1u) % frameCount;
    } else {
        frame1 = min(frame0 + 1u, frameCount - 1u);
    }
    
    frame0 = min(frame0, frameCount - 1u);
}

//=============================================================================
// Main
//=============================================================================

void main() {
    // Get vertex index within quad (0-3)
    uint vertexId = gl_VertexIndex;
    
    // Get particle index from instance
    uint particleIdx;
    if (pc.useSortedIndices != 0u) {
        particleIdx = sortedIndices[gl_InstanceIndex];
    } else {
        particleIdx = aliveList[gl_InstanceIndex];
    }
    
    // Read particle data
    Particle p = particles[particleIdx];
    
    // Get quad vertex position and UV
    vec2 quadPos = quadVertices[vertexId];
    vec2 uv = quadUVs[vertexId];
    
    // Apply particle rotation
    quadPos = rotation2D(p.rotation) * quadPos;
    
    // Apply particle size
    quadPos *= p.size;
    
    // Billboard: transform to world space using camera orientation
    vec3 worldOffset = camera.cameraRight * quadPos.x + camera.cameraUp * quadPos.y;
    vec3 worldPos = p.position + worldOffset;
    
    // Calculate view-space depth for soft particles (at particle center)
    fragViewDepth = getViewSpaceDepth(p.position);
    
    // Transform to clip space
    gl_Position = camera.viewProjection * vec4(worldPos, 1.0);
    
    // Calculate normalized age for animation
    float normalizedAge = p.age / max(p.lifetime, 0.001);
    float speed = length(p.velocity);
    
    // Atlas UV calculation
    if (pc.atlasColumns > 0u && pc.atlasRows > 0u && pc.atlasFrameCount > 1u) {
        uint frameCount = pc.atlasFrameCount;
        bool loop = pc.atlasLoop != 0u;
        uint currentFrame = 0u;
        uint nextFrame = 0u;
        float blendFactor = 0.0;
        
        // Calculate frame based on animation mode
        switch (pc.atlasAnimMode) {
            case ATLAS_MODE_OVER_LIFETIME:
                getBlendedFrames(normalizedAge, frameCount, loop, currentFrame, nextFrame, blendFactor);
                break;
                
            case ATLAS_MODE_BY_SPEED:
                currentFrame = getFrameFromSpeed(speed, frameCount, pc.atlasSpeedMin, pc.atlasSpeedMax);
                nextFrame = min(currentFrame + 1u, frameCount - 1u);
                blendFactor = 0.0;
                break;
                
            case ATLAS_MODE_RANDOM:
                currentFrame = getRandomFrame(particleIdx, frameCount);
                nextFrame = currentFrame;
                blendFactor = 0.0;
                break;
                
            case ATLAS_MODE_FIXED:
                // Fixed frame comes from emitter config, use frame 0 as default
                currentFrame = 0u;
                nextFrame = 0u;
                blendFactor = 0.0;
                break;
                
            case ATLAS_MODE_REAL_TIME:
                currentFrame = getFrameFromTime(camera.time, frameCount, pc.atlasFrameRate, loop);
                nextFrame = loop ? (currentFrame + 1u) % frameCount : min(currentFrame + 1u, frameCount - 1u);
                // Calculate blend factor for smooth real-time animation
                float frameFloat = camera.time * pc.atlasFrameRate;
                blendFactor = fract(frameFloat);
                break;
                
            case ATLAS_MODE_PING_PONG:
                currentFrame = getFrameFromAgePingPong(normalizedAge, frameCount, loop);
                // For ping-pong, next frame depends on direction
                nextFrame = currentFrame;
                blendFactor = 0.0;
                break;
                
            default:
                currentFrame = 0u;
                nextFrame = 0u;
                blendFactor = 0.0;
                break;
        }
        
        // Calculate UV scale (same for all frames)
        fragAtlasUVScale = vec2(1.0 / float(pc.atlasColumns), 1.0 / float(pc.atlasRows));
        
        // Calculate UV offsets for current and next frame
        fragAtlasUVOffset = getFrameUVOffset(currentFrame, pc.atlasColumns, pc.atlasRows);
        fragAtlasNextUVOffset = getFrameUVOffset(nextFrame, pc.atlasColumns, pc.atlasRows);
        fragAtlasBlendFactor = blendFactor;
    } else {
        // No atlas - use full texture
        fragAtlasUVScale = vec2(1.0, 1.0);
        fragAtlasUVOffset = vec2(0.0, 0.0);
        fragAtlasNextUVOffset = vec2(0.0, 0.0);
        fragAtlasBlendFactor = 0.0;
    }
    
    // Pass outputs to fragment shader
    fragUV = uv;
    fragColor = p.color;
    fragAge = p.age;
    fragLifetime = p.lifetime;
}
