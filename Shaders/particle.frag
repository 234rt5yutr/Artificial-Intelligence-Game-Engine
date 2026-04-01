#version 450

/**
 * @file particle.frag
 * @brief GPU particle rendering fragment shader with texture atlas support
 * 
 * Renders particles with texture support, alpha blending, soft particles,
 * multiple blend mode computations, and texture atlas (sprite sheet) animation.
 * 
 * Supports:
 * - Procedural circular particles or textured particles
 * - Texture atlas (sprite sheet) animation with multiple modes
 * - Inter-frame blending for smooth animation
 * - Soft particles (fade near geometry using depth buffer)
 * - Multiple blend modes (Additive, AlphaBlend, Premultiplied, Multiply, SoftAdditive)
 * - Lifetime-based fade out
 * - Color interpolation
 */

//=============================================================================
// Blend Mode Constants
//=============================================================================

#define BLEND_MODE_ADDITIVE      0
#define BLEND_MODE_ALPHA_BLEND   1
#define BLEND_MODE_PREMULTIPLIED 2
#define BLEND_MODE_MULTIPLY      3
#define BLEND_MODE_SOFT_ADDITIVE 4

//=============================================================================
// Inputs from Vertex Shader
//=============================================================================

layout (location = 0) in vec2 fragUV;
layout (location = 1) in vec4 fragColor;
layout (location = 2) in float fragAge;
layout (location = 3) in float fragLifetime;
layout (location = 4) in float fragViewDepth;  // View-space depth for soft particles
// Atlas inputs
layout (location = 5) in vec2 fragAtlasUVOffset;    // UV offset for current frame
layout (location = 6) in vec2 fragAtlasUVScale;     // UV scale (1/cols, 1/rows)
layout (location = 7) in float fragAtlasBlendFactor; // Blend factor to next frame
layout (location = 8) in vec2 fragAtlasNextUVOffset; // UV offset for next frame

//=============================================================================
// Outputs
//=============================================================================

layout (location = 0) out vec4 outColor;

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
    vec2 screenSize;           // Screen dimensions for depth sampling
    float nearPlane;           // Camera near plane
    float farPlane;            // Camera far plane
} camera;

//=============================================================================
// Textures
//=============================================================================

// Particle texture (optional, defaults to circular falloff if not bound)
layout (set = 1, binding = 0) uniform sampler2D particleTexture;

// Scene depth buffer for soft particles
layout (set = 1, binding = 1) uniform sampler2D depthTexture;

//=============================================================================
// Push Constants
//=============================================================================

layout (push_constant) uniform PushConstants {
    uint useSortedIndices;      // Use sorted particle order
    uint softParticles;         // Enable soft particle edges
    float softParticleScale;    // Soft particle fade distance
    uint useTexture;            // 1 = sample texture, 0 = procedural
    uint blendMode;             // Current blend mode for shader adjustments
    float depthBias;            // Depth bias for layering
    // Atlas parameters
    uint atlasColumns;          // Number of columns in atlas (0 = no atlas)
    uint atlasRows;             // Number of rows in atlas
    uint atlasFrameCount;       // Total animation frames
    uint atlasAnimMode;         // Animation mode
    float atlasFrameRate;       // Frames per second
    float atlasSpeedMin;        // Min speed for BySpeed mode
    float atlasSpeedMax;        // Max speed for BySpeed mode
    uint atlasLoop;             // Loop animation flag
} pc;

//=============================================================================
// Helper Functions
//=============================================================================

/**
 * @brief Soft circular falloff for untextured particles
 * Creates a smooth circular gradient from center (1.0) to edge (0.0)
 */
float circularFalloff(vec2 uv) {
    vec2 centered = uv * 2.0 - 1.0;
    float distSq = dot(centered, centered);
    return 1.0 - smoothstep(0.0, 1.0, distSq);
}

/**
 * @brief Soft radial gradient with customizable falloff
 * @param uv UV coordinates
 * @param innerRadius Inner radius where alpha is 1.0
 * @param outerRadius Outer radius where alpha is 0.0
 */
float radialGradient(vec2 uv, float innerRadius, float outerRadius) {
    vec2 centered = uv * 2.0 - 1.0;
    float dist = length(centered);
    return 1.0 - smoothstep(innerRadius, outerRadius, dist);
}

/**
 * @brief Convert depth buffer value to linear depth
 * @param depth Non-linear depth buffer value [0, 1]
 * @return Linear depth in view space
 */
float linearizeDepth(float depth) {
    float z = depth * 2.0 - 1.0; // Convert to NDC [-1, 1]
    return (2.0 * camera.nearPlane * camera.farPlane) / 
           (camera.farPlane + camera.nearPlane - z * (camera.farPlane - camera.nearPlane));
}

/**
 * @brief Calculate soft particle fade factor
 * @param sceneDepth Linear scene depth
 * @param particleDepth Linear particle depth
 * @param scale Fade distance scale
 * @return Fade factor [0, 1]
 */
float calculateSoftFade(float sceneDepth, float particleDepth, float scale) {
    float depthDiff = sceneDepth - particleDepth;
    return smoothstep(0.0, scale, depthDiff);
}

/**
 * @brief Calculate lifetime-based fade
 * @param lifeRatio Current age / total lifetime [0, 1]
 * @return Fade multiplier [0, 1]
 */
float lifetimeFade(float lifeRatio) {
    // Fade in during first 5% of life
    float fadeIn = smoothstep(0.0, 0.05, lifeRatio);
    // Fade out during last 30% of life
    float fadeOut = 1.0 - smoothstep(0.7, 1.0, lifeRatio);
    return fadeIn * fadeOut;
}

/**
 * @brief Apply color adjustments based on blend mode
 * Some blend modes work better with specific color preprocessing
 */
vec4 adjustColorForBlendMode(vec4 color, float alpha, uint blendMode) {
    vec4 result = color;
    result.a = alpha;
    
    switch (blendMode) {
        case BLEND_MODE_ADDITIVE:
            // Additive: Multiply color by alpha for intensity control
            result.rgb *= alpha;
            break;
            
        case BLEND_MODE_ALPHA_BLEND:
            // Standard alpha blend: No special processing needed
            break;
            
        case BLEND_MODE_PREMULTIPLIED:
            // Premultiplied alpha: Color already multiplied by alpha
            result.rgb *= alpha;
            break;
            
        case BLEND_MODE_MULTIPLY:
            // Multiply: Invert alpha effect (darker = more visible)
            result.rgb = mix(vec3(1.0), result.rgb, alpha);
            result.a = 1.0; // Multiply doesn't use alpha in the traditional sense
            break;
            
        case BLEND_MODE_SOFT_ADDITIVE:
            // Soft additive: Attenuate based on brightness
            float brightness = dot(result.rgb, vec3(0.299, 0.587, 0.114));
            result.rgb *= alpha * (1.0 - brightness * 0.5);
            break;
            
        default:
            // Default to premultiplied
            result.rgb *= alpha;
            break;
    }
    
    return result;
}

/**
 * @brief Sample texture atlas at the given base UV
 * Transforms base UV (0-1 quad UV) to atlas UV for specific frame
 */
vec4 sampleAtlas(vec2 baseUV, vec2 uvOffset, vec2 uvScale) {
    vec2 atlasUV = uvOffset + baseUV * uvScale;
    return texture(particleTexture, atlasUV);
}

/**
 * @brief Sample texture atlas with bilinear interpolation between frames
 * Blends between current frame and next frame for smooth animation
 */
vec4 sampleAtlasBlended(vec2 baseUV, vec2 uvOffset0, vec2 uvOffset1, vec2 uvScale, float blendFactor) {
    vec4 sample0 = sampleAtlas(baseUV, uvOffset0, uvScale);
    vec4 sample1 = sampleAtlas(baseUV, uvOffset1, uvScale);
    return mix(sample0, sample1, blendFactor);
}

//=============================================================================
// Main
//=============================================================================

void main() {
    // Calculate life ratio for effects
    float lifeRatio = fragAge / max(fragLifetime, 0.001);
    
    // Base color from particle (interpolated from start to end color)
    vec4 color = fragColor;
    
    // Sample texture or use procedural falloff
    float alpha;
    vec3 texColor = vec3(1.0);
    
    if (pc.useTexture != 0u) {
        vec4 texSample;
        
        // Check if atlas is enabled (columns > 0 and more than 1 frame)
        bool useAtlas = pc.atlasColumns > 0u && pc.atlasRows > 0u && pc.atlasFrameCount > 1u;
        
        if (useAtlas) {
            // Check if blending between frames is enabled
            if (fragAtlasBlendFactor > 0.001) {
                // Sample and blend between current and next frame
                texSample = sampleAtlasBlended(
                    fragUV,
                    fragAtlasUVOffset,
                    fragAtlasNextUVOffset,
                    fragAtlasUVScale,
                    fragAtlasBlendFactor
                );
            } else {
                // Sample only current frame
                texSample = sampleAtlas(fragUV, fragAtlasUVOffset, fragAtlasUVScale);
            }
        } else {
            // No atlas, sample full texture
            texSample = texture(particleTexture, fragUV);
        }
        
        texColor = texSample.rgb;
        alpha = texSample.a;
    } else {
        // Procedural circular particle with soft edge
        alpha = circularFalloff(fragUV);
    }
    
    // Combine texture/procedural alpha with particle alpha
    alpha *= color.a;
    
    // Apply lifetime fade (fade in and fade out)
    alpha *= lifetimeFade(lifeRatio);
    
    // Early discard for fully transparent pixels
    if (alpha < 0.001) {
        discard;
    }
    
    // Soft particles - fade when near scene geometry
    if (pc.softParticles != 0u) {
        // Sample scene depth at fragment position
        vec2 screenUV = gl_FragCoord.xy / camera.screenSize;
        float sceneDepthNonLinear = texture(depthTexture, screenUV).r;
        float sceneDepthLinear = linearizeDepth(sceneDepthNonLinear);
        
        // Get particle's linear depth (passed from vertex shader)
        float particleDepthLinear = abs(fragViewDepth);
        
        // Apply depth bias
        particleDepthLinear += pc.depthBias;
        
        // Calculate soft fade factor
        float softFade = calculateSoftFade(sceneDepthLinear, particleDepthLinear, pc.softParticleScale);
        alpha *= softFade;
        
        // Discard if faded to nothing
        if (alpha < 0.001) {
            discard;
        }
    }
    
    // Apply texture color modulation
    color.rgb *= texColor;
    
    // Apply blend mode-specific color adjustments
    outColor = adjustColorForBlendMode(color, alpha, pc.blendMode);
}
