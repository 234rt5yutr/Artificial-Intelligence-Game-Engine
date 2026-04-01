#version 450

// Color Grading with 3D LUT support and professional color correction
// Final post-processing pass for cinematic look

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler3D lutTexture;

layout(push_constant) uniform PushConstants {
    float exposure;
    float contrast;
    float saturation;
    float temperature;
    float tint;
    float gamma;
    float lift;
    float gain;
    int tonemapMode;
    int useLUT;
    float lutSize;
    float vignetteIntensity;
    float vignetteSmoothness;
    float padding[3];
} pc;

#include "aces_tonemapping.glsl"
#include "post_process_common.glsl"

// White balance using temperature and tint
vec3 WhiteBalance(vec3 color, float temperature, float tint) {
    // Temperature: blue (-1) to yellow (+1)
    // Tint: green (-1) to magenta (+1)
    
    // Simple approximation of Planckian locus
    vec3 warm = vec3(1.0, 0.9, 0.8);
    vec3 cool = vec3(0.8, 0.9, 1.0);
    vec3 tempBalance = mix(cool, warm, temperature * 0.5 + 0.5);
    
    // Tint adjustment
    vec3 greenTint = vec3(0.9, 1.0, 0.9);
    vec3 magentaTint = vec3(1.0, 0.9, 1.0);
    vec3 tintBalance = mix(greenTint, magentaTint, tint * 0.5 + 0.5);
    
    return color * tempBalance * tintBalance;
}

// Lift-Gamma-Gain color correction
vec3 LiftGammaGain(vec3 color, float lift, float gamma, float gain) {
    // Lift: affects shadows
    color = color + lift * (1.0 - color);
    
    // Gain: affects highlights
    color = color * gain;
    
    // Gamma: overall curve
    color = pow(max(color, vec3(0.0)), vec3(1.0 / gamma));
    
    return color;
}

// Contrast adjustment around middle gray
vec3 ApplyContrast(vec3 color, float contrast) {
    const float midGray = 0.18;
    return (color - midGray) * contrast + midGray;
}

// Saturation adjustment
vec3 ApplySaturation(vec3 color, float saturation) {
    float luminance = Luminance(color);
    return mix(vec3(luminance), color, saturation);
}

// Sample 3D LUT with proper coordinate mapping
vec3 SampleLUT(vec3 color, float lutSize) {
    // Clamp input to valid range
    color = clamp(color, 0.0, 1.0);
    
    // Calculate LUT coordinates
    // Account for half-texel offset for proper interpolation
    float scale = (lutSize - 1.0) / lutSize;
    float offset = 0.5 / lutSize;
    vec3 lutCoord = color * scale + offset;
    
    return texture(lutTexture, lutCoord).rgb;
}

// Vignette effect
float Vignette(vec2 uv, float intensity, float smoothness) {
    vec2 center = uv - 0.5;
    float dist = length(center);
    float vignette = 1.0 - smoothstep(0.5 - smoothness, 0.5, dist * intensity);
    return vignette;
}

void main() {
    vec3 color = texture(sceneColor, fragUV).rgb;
    
    // 1. Exposure adjustment
    color *= pow(2.0, pc.exposure);
    
    // 2. White balance
    if (pc.temperature != 0.0 || pc.tint != 0.0) {
        color = WhiteBalance(color, pc.temperature, pc.tint);
    }
    
    // 3. Contrast
    color = ApplyContrast(color, pc.contrast);
    
    // 4. Lift-Gamma-Gain
    color = LiftGammaGain(color, pc.lift, pc.gamma, pc.gain);
    
    // 5. Saturation
    color = ApplySaturation(color, pc.saturation);
    
    // 6. 3D LUT color grading
    if (pc.useLUT > 0) {
        color = SampleLUT(color, pc.lutSize);
    }
    
    // 7. Tonemapping (HDR to SDR)
    color = ApplyTonemap(color, pc.tonemapMode);
    
    // 8. Vignette
    if (pc.vignetteIntensity > 0.0) {
        float vignette = Vignette(fragUV, pc.vignetteIntensity, pc.vignetteSmoothness);
        color *= vignette;
    }
    
    // 9. Convert to sRGB
    color = LinearToSRGB(color);
    
    // Clamp final output
    color = clamp(color, 0.0, 1.0);
    
    outColor = vec4(color, 1.0);
}
