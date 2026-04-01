#ifndef ACES_TONEMAPPING_GLSL
#define ACES_TONEMAPPING_GLSL

// ACES (Academy Color Encoding System) Tonemapping Functions
// Industry-standard filmic tonemapping used in film and games

// sRGB to linear conversion
vec3 SRGBToLinear(vec3 color) {
    return pow(color, vec3(2.2));
}

// Linear to sRGB conversion
vec3 LinearToSRGB(vec3 color) {
    return pow(color, vec3(1.0 / 2.2));
}

// ACES Filmic Tonemapping (Stephen Hill approximation)
// Fast and accurate approximation of the ACES RRT/ODT
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Full ACES with input transform
// More accurate but slightly more expensive
mat3 ACESInputMat = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777
);

mat3 ACESOutputMat = mat3(
    1.60475, -0.10208, -0.00327,
    -0.53108, 1.10813, -0.07276,
    -0.07367, -0.00605, 1.07602
);

vec3 RRTAndODTFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 ACESFitted(vec3 color) {
    color = ACESInputMat * color;
    color = RRTAndODTFit(color);
    color = ACESOutputMat * color;
    return clamp(color, 0.0, 1.0);
}

// Reinhard Tonemapping
// Simple and predictable, good for real-time
vec3 Reinhard(vec3 color) {
    return color / (color + 1.0);
}

// Extended Reinhard with white point
vec3 ReinhardExtended(vec3 color, float whitePoint) {
    vec3 numerator = color * (1.0 + color / (whitePoint * whitePoint));
    return numerator / (1.0 + color);
}

// Uncharted 2 Tonemapping (John Hable)
// Good balance between contrast and detail preservation
vec3 Uncharted2Partial(vec3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 Uncharted2(vec3 color) {
    float exposureBias = 2.0;
    vec3 curr = Uncharted2Partial(color * exposureBias);
    
    vec3 whiteScale = 1.0 / Uncharted2Partial(vec3(11.2));
    return curr * whiteScale;
}

// Neutral tonemapping (AgX-inspired)
// Modern tonemapping with good color preservation
vec3 NeutralTonemap(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    
    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

// Apply selected tonemapping operator
vec3 ApplyTonemap(vec3 color, int mode) {
    switch (mode) {
        case 0: return ACESFilm(color);
        case 1: return Reinhard(color);
        case 2: return Uncharted2(color);
        case 3: return NeutralTonemap(color);
        default: return ACESFilm(color);
    }
}

#endif // ACES_TONEMAPPING_GLSL
