#version 450
#extension GL_ARB_separate_shader_objects : enable

// MSDF Text Fragment Shader
// Multi-channel Signed Distance Field rendering with anti-aliasing, outline, and shadow support

layout(push_constant) uniform PushConstants {
    mat4 projectionMatrix;    // Orthographic projection (screen-space to clip-space)
    vec4 textColor;           // Base text color (RGBA)
    vec4 outlineColor;        // Outline color (RGBA)
    vec4 shadowColor;         // Drop shadow color (RGBA)
    float outlineWidth;       // Outline width in pixels (0 = disabled)
    float shadowOffset;       // Shadow offset in pixels (0 = disabled)
    float distanceRange;      // MSDF distance range (typically 4.0)
    float fontSize;           // Font size in pixels
} pc;

layout(set = 0, binding = 0) uniform sampler2D fontAtlas;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragDistanceRange;

layout(location = 0) out vec4 outColor;

// Median of three values - core MSDF operation
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

// Screen-space pixel distance for anti-aliasing
float screenPxDistance(float signedDist) {
    // Convert to screen pixels using the scaled distance range
    return signedDist * fragDistanceRange * pc.fontSize;
}

// Calculate glyph opacity at a given signed distance
float getOpacity(float signedDist) {
    float pxDist = screenPxDistance(signedDist);
    
    // Use fwidth for screen-space derivatives (anti-aliasing)
    float pxRange = length(vec2(dFdx(pxDist), dFdy(pxDist)));
    float edgeWidth = max(pxRange, 1.0);
    
    // Smooth step from edge (0.5 in MSDF space means the edge)
    return clamp(pxDist / edgeWidth + 0.5, 0.0, 1.0);
}

void main() {
    // Sample MSDF texture
    vec3 msdf = texture(fontAtlas, fragTexCoord).rgb;
    
    // Calculate signed distance from the three channels
    float signedDist = median(msdf.r, msdf.g, msdf.b) - 0.5;
    
    // Calculate main text opacity
    float textOpacity = getOpacity(signedDist);
    
    // Initialize output with main text
    vec4 result = vec4(fragColor.rgb, fragColor.a * textOpacity);
    
    // Apply outline if enabled
    if (pc.outlineWidth > 0.0) {
        // Outline extends outward from the glyph edge
        float outlineSignedDist = signedDist + (pc.outlineWidth / pc.fontSize / fragDistanceRange);
        float outlineOpacity = getOpacity(outlineSignedDist);
        
        // Blend outline behind text
        vec4 outlineResult = vec4(pc.outlineColor.rgb, pc.outlineColor.a * outlineOpacity);
        result = mix(outlineResult, result, textOpacity);
    }
    
    // Apply drop shadow if enabled
    if (pc.shadowOffset > 0.0) {
        // Sample MSDF at offset position for shadow
        vec2 shadowUV = fragTexCoord - vec2(pc.shadowOffset / pc.fontSize * 0.01, -pc.shadowOffset / pc.fontSize * 0.01);
        vec3 shadowMsdf = texture(fontAtlas, shadowUV).rgb;
        float shadowSignedDist = median(shadowMsdf.r, shadowMsdf.g, shadowMsdf.b) - 0.5;
        float shadowOpacity = getOpacity(shadowSignedDist);
        
        // Blend shadow behind everything
        vec4 shadowResult = vec4(pc.shadowColor.rgb, pc.shadowColor.a * shadowOpacity);
        
        // Shadow goes behind outline and text
        float combinedOpacity = max(textOpacity, pc.outlineWidth > 0.0 ? getOpacity(signedDist + (pc.outlineWidth / pc.fontSize / fragDistanceRange)) : 0.0);
        result = mix(shadowResult, result, combinedOpacity);
    }
    
    // Premultiplied alpha output
    outColor = result;
    
    // Discard fully transparent pixels for early-z benefits
    if (outColor.a < 0.01) {
        discard;
    }
}
