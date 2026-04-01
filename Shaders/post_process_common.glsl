#ifndef POST_PROCESS_COMMON_GLSL
#define POST_PROCESS_COMMON_GLSL

// ============================================================================
// Constants
// ============================================================================

const float PI = 3.14159265359;
const float EPSILON = 0.0001;

// Luminance coefficients (ITU-R BT.709)
const vec3 LUMINANCE_WEIGHTS = vec3(0.2126, 0.7152, 0.0722);

// ============================================================================
// Depth Utilities
// ============================================================================

// Linearize depth from Vulkan depth buffer [0, 1] -> linear view-space depth
float LinearizeDepth(float depth, float nearPlane, float farPlane) {
    // Reversed-Z projection: depth = (far * near) / (far - z * (far - near))
    // Solving for z: z = (far * near) / (depth * (far - near) + near)
    // For standard projection: z = near * far / (far - depth * (far - near))
    return nearPlane * farPlane / (farPlane - depth * (farPlane - nearPlane));
}

// Convert linear depth to normalized depth [0, 1]
float NormalizeDepth(float linearDepth, float nearPlane, float farPlane) {
    return (linearDepth - nearPlane) / (farPlane - nearPlane);
}

// ============================================================================
// Position Reconstruction
// ============================================================================

// Reconstruct view-space position from depth and UV
vec3 ReconstructViewPosition(vec2 uv, float depth, mat4 invProjection) {
    // Convert UV to clip space [-1, 1]
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);

    // Transform to view space
    vec4 viewPos = invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

// Reconstruct world-space position from depth and UV
vec3 ReconstructWorldPosition(vec2 uv, float depth, mat4 invViewProjection) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = invViewProjection * clipPos;
    return worldPos.xyz / worldPos.w;
}

// ============================================================================
// Color Utilities
// ============================================================================

// Calculate luminance of a color
float Luminance(vec3 color) {
    return dot(color, LUMINANCE_WEIGHTS);
}

// Convert RGB to HSV
vec3 RGBtoHSV(vec3 rgb) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(rgb.bg, K.wz), vec4(rgb.gb, K.xy), step(rgb.b, rgb.g));
    vec4 q = mix(vec4(p.xyw, rgb.r), vec4(rgb.r, p.yzx), step(p.x, rgb.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// Convert HSV to RGB
vec3 HSVtoRGB(vec3 hsv) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(hsv.xxx + K.xyz) * 6.0 - K.www);
    return hsv.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), hsv.y);
}

// ============================================================================
// Bloom Utilities
// ============================================================================

// Soft threshold for bloom extraction
vec3 SoftThreshold(vec3 color, float threshold, float knee) {
    float brightness = Luminance(color);

    // Soft knee curve
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + EPSILON);

    float contribution = max(soft, brightness - threshold);
    contribution /= max(brightness, EPSILON);

    return color * contribution;
}

// Karis average for bloom downsampling (reduces fireflies)
float KarisAverage(vec3 color) {
    return 1.0 / (1.0 + Luminance(color));
}

// ============================================================================
// Noise Functions
// ============================================================================

// Interleaved gradient noise (good for temporal dithering)
float InterleavedGradientNoise(vec2 screenPos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

// Blue noise approximation
float BlueNoise(vec2 uv, float seed) {
    return fract(sin(dot(uv + seed, vec2(12.9898, 78.233))) * 43758.5453);
}

// Hash function for random sampling
float Hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// ============================================================================
// Blur Weights
// ============================================================================

// Gaussian weight calculation
float GaussianWeight(float x, float sigma) {
    float sigmaSq = sigma * sigma;
    return exp(-x * x / (2.0 * sigmaSq)) / (sqrt(2.0 * PI) * sigma);
}

// Bilateral weight (edge-preserving blur)
float BilateralWeight(float centerDepth, float sampleDepth, float sigma) {
    float diff = abs(centerDepth - sampleDepth);
    return exp(-diff * diff / (2.0 * sigma * sigma));
}

// Combined bilateral + spatial weight
float CombinedBilateralWeight(float spatialDist, float depthDiff,
                               float spatialSigma, float depthSigma) {
    float spatial = exp(-spatialDist * spatialDist / (2.0 * spatialSigma * spatialSigma));
    float depth = exp(-depthDiff * depthDiff / (2.0 * depthSigma * depthSigma));
    return spatial * depth;
}

// ============================================================================
// Sampling Patterns
// ============================================================================

// Poisson disk samples for SSAO (16 samples)
const vec2 POISSON_DISK_16[16] = vec2[](
    vec2(-0.94201624, -0.39906216),
    vec2(0.94558609, -0.76890725),
    vec2(-0.094184101, -0.92938870),
    vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845),
    vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554),
    vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507),
    vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367),
    vec2(0.14383161, -0.14100790)
);

// Vogel disk point generation (for variable sample counts)
vec2 VogelDiskSample(int sampleIndex, int sampleCount, float rotation) {
    float goldenAngle = 2.4;
    float r = sqrt(float(sampleIndex) + 0.5) / sqrt(float(sampleCount));
    float theta = float(sampleIndex) * goldenAngle + rotation;
    return vec2(r * cos(theta), r * sin(theta));
}

// ============================================================================
// Tonemapping Operators
// ============================================================================

// ACES Filmic Tonemapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Reinhard Tonemapping
vec3 Reinhard(vec3 x) {
    return x / (1.0 + x);
}

// Extended Reinhard with white point
vec3 ReinhardExtended(vec3 x, float whitePoint) {
    vec3 numerator = x * (1.0 + x / (whitePoint * whitePoint));
    return numerator / (1.0 + x);
}

// Uncharted 2 Tonemapping
vec3 Uncharted2Tonemap(vec3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 Uncharted2(vec3 color, float exposureBias) {
    float W = 11.2;
    color *= exposureBias;
    vec3 curr = Uncharted2Tonemap(color);
    vec3 whiteScale = 1.0 / Uncharted2Tonemap(vec3(W));
    return curr * whiteScale;
}

// ============================================================================
// Color Grading
// ============================================================================

// Apply exposure
vec3 ApplyExposure(vec3 color, float exposure) {
    return color * pow(2.0, exposure);
}

// Apply contrast
vec3 ApplyContrast(vec3 color, float contrast) {
    return (color - 0.5) * contrast + 0.5;
}

// Apply saturation
vec3 ApplySaturation(vec3 color, float saturation) {
    float luma = Luminance(color);
    return mix(vec3(luma), color, saturation);
}

// Apply temperature shift (Kelvin-like)
vec3 ApplyTemperature(vec3 color, float temperature) {
    // Simplified temperature: positive = warm (red), negative = cool (blue)
    vec3 warm = vec3(1.0, 0.9, 0.8);
    vec3 cool = vec3(0.8, 0.9, 1.0);
    vec3 tint = mix(cool, warm, temperature * 0.5 + 0.5);
    return color * tint;
}

// Apply tint (green-magenta axis)
vec3 ApplyTint(vec3 color, float tint) {
    // Positive = magenta, negative = green
    vec3 magenta = vec3(1.0, 0.9, 1.0);
    vec3 green = vec3(0.9, 1.0, 0.9);
    vec3 shift = mix(green, magenta, tint * 0.5 + 0.5);
    return color * shift;
}

// ============================================================================
// Vignette
// ============================================================================

float Vignette(vec2 uv, float intensity, float smoothness) {
    vec2 centered = uv - 0.5;
    float dist = length(centered);
    return 1.0 - smoothstep(1.0 - smoothness - intensity, 1.0 - smoothness, dist * 2.0);
}

#endif // POST_PROCESS_COMMON_GLSL
