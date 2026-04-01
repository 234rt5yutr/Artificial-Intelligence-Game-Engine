#version 450

// Atmospheric scattering fragment shader
// Implements physically-based Rayleigh and Mie scattering for volumetric sky rendering

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inViewRay;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform AtmosphereUBO {
    vec3 sunDirection;
    float sunIntensity;
    
    vec3 rayleighCoefficient;
    float mieCoefficient;
    
    vec3 sunColor;
    float mieDirectionalG;
    
    vec3 cameraPosition;
    float atmosphereHeight;
    
    vec3 groundAlbedo;
    float planetRadius;
    
    float turbidity;
    float exposureMultiplier;
    float sunAngularDiameter;
    float timeOfDay;
    
    float starIntensity;
    float moonPhase;
    float _pad0;
    float _pad1;
} atmosphere;

const float PI = 3.14159265359;
const float INV_4PI = 0.07957747154;  // 1 / (4 * PI)
const int NUM_SCATTERING_SAMPLES = 16;
const int NUM_OPTICAL_DEPTH_SAMPLES = 8;

// Hash function for star generation
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Value noise for stars
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Generate procedural stars
float stars(vec3 direction) {
    // Use direction as seed for procedural star field
    vec2 sphereUV = vec2(
        atan(direction.z, direction.x) / (2.0 * PI) + 0.5,
        asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5
    );
    
    // Multiple octaves for variety
    float starField = 0.0;
    float scale = 800.0;
    
    for (int i = 0; i < 3; i++) {
        vec2 uv = sphereUV * scale;
        vec2 gv = fract(uv) - 0.5;
        vec2 id = floor(uv);
        
        float n = hash(id);
        float size = 0.02 + n * 0.03;
        
        // Only show ~10% of cells as stars
        if (n > 0.9) {
            float star = 1.0 - smoothstep(0.0, size, length(gv));
            star *= n;
            starField = max(starField, star);
        }
        
        scale *= 0.5;
    }
    
    // Twinkling effect based on position
    float twinkle = 0.7 + 0.3 * sin(atmosphere.timeOfDay * 2.0 + hash(sphereUV * 100.0) * 10.0);
    
    return starField * twinkle;
}

// Rayleigh phase function
float rayleighPhase(float cosTheta) {
    return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

// Mie phase function (Henyey-Greenstein approximation)
float miePhase(float cosTheta, float g) {
    float g2 = g * g;
    float num = (1.0 - g2);
    float denom = pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return INV_4PI * num / denom;
}

// Calculate ray-sphere intersection
// Returns distance to first intersection, or -1 if no intersection
vec2 raySphereIntersect(vec3 rayOrigin, vec3 rayDir, vec3 sphereCenter, float sphereRadius) {
    vec3 oc = rayOrigin - sphereCenter;
    float b = dot(oc, rayDir);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float h = b * b - c;
    
    if (h < 0.0) return vec2(-1.0);
    
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

// Calculate atmospheric density at a given height
float atmosphereDensity(float height, float scaleHeight) {
    return exp(-height / scaleHeight);
}

// Calculate optical depth along a ray through the atmosphere
vec2 computeOpticalDepth(vec3 rayOrigin, vec3 rayDir, float rayLength) {
    float rayleighScaleHeight = 8500.0;  // 8.5km for Rayleigh
    float mieScaleHeight = 1200.0;       // 1.2km for Mie
    
    float stepSize = rayLength / float(NUM_OPTICAL_DEPTH_SAMPLES);
    vec3 samplePoint = rayOrigin + rayDir * stepSize * 0.5;
    
    float rayleighOpticalDepth = 0.0;
    float mieOpticalDepth = 0.0;
    
    for (int i = 0; i < NUM_OPTICAL_DEPTH_SAMPLES; i++) {
        float height = length(samplePoint) - atmosphere.planetRadius;
        height = max(height, 0.0);
        
        rayleighOpticalDepth += atmosphereDensity(height, rayleighScaleHeight) * stepSize;
        mieOpticalDepth += atmosphereDensity(height, mieScaleHeight) * stepSize;
        
        samplePoint += rayDir * stepSize;
    }
    
    return vec2(rayleighOpticalDepth, mieOpticalDepth);
}

// Main atmospheric scattering calculation
vec3 computeAtmosphericScattering(vec3 rayDir, vec3 sunDir) {
    vec3 planetCenter = vec3(0.0, -atmosphere.planetRadius, 0.0);
    vec3 rayOrigin = atmosphere.cameraPosition + vec3(0.0, atmosphere.planetRadius, 0.0);
    
    float atmosphereRadius = atmosphere.planetRadius + atmosphere.atmosphereHeight;
    
    // Intersect view ray with atmosphere
    vec2 atmosphereIntersect = raySphereIntersect(rayOrigin, rayDir, planetCenter, atmosphereRadius);
    
    if (atmosphereIntersect.y < 0.0) {
        return vec3(0.0);  // No intersection with atmosphere
    }
    
    // Check for ground intersection
    vec2 groundIntersect = raySphereIntersect(rayOrigin, rayDir, planetCenter, atmosphere.planetRadius);
    float rayLength = (groundIntersect.x > 0.0) ? groundIntersect.x : atmosphereIntersect.y;
    rayLength = min(rayLength, atmosphereIntersect.y);
    
    float stepSize = rayLength / float(NUM_SCATTERING_SAMPLES);
    vec3 samplePoint = rayOrigin + rayDir * stepSize * 0.5;
    
    vec3 rayleighScattering = vec3(0.0);
    vec3 mieScattering = vec3(0.0);
    
    float rayleighScaleHeight = 8500.0;
    float mieScaleHeight = 1200.0;
    
    // Adjust Mie coefficient based on turbidity
    float adjustedMieCoeff = atmosphere.mieCoefficient * atmosphere.turbidity;
    
    for (int i = 0; i < NUM_SCATTERING_SAMPLES; i++) {
        float height = length(samplePoint) - atmosphere.planetRadius;
        height = max(height, 0.0);
        
        // Local density
        float rayleighDensity = atmosphereDensity(height, rayleighScaleHeight);
        float mieDensity = atmosphereDensity(height, mieScaleHeight);
        
        // Calculate optical depth to sun from this sample point
        vec2 sunRayIntersect = raySphereIntersect(samplePoint, sunDir, planetCenter, atmosphereRadius);
        vec2 sunOpticalDepth = computeOpticalDepth(samplePoint, sunDir, sunRayIntersect.y);
        
        // Calculate optical depth from camera to this point
        vec2 viewOpticalDepth = computeOpticalDepth(rayOrigin, rayDir, float(i) * stepSize);
        
        // Total optical depth
        vec2 totalOpticalDepth = sunOpticalDepth + viewOpticalDepth;
        
        // Calculate transmittance
        vec3 rayleighTransmittance = exp(-atmosphere.rayleighCoefficient * totalOpticalDepth.x);
        vec3 mieTransmittance = exp(-vec3(adjustedMieCoeff) * totalOpticalDepth.y);
        vec3 totalTransmittance = rayleighTransmittance * mieTransmittance;
        
        // Accumulate scattering
        rayleighScattering += rayleighDensity * totalTransmittance * stepSize;
        mieScattering += mieDensity * totalTransmittance * stepSize;
        
        samplePoint += rayDir * stepSize;
    }
    
    // Apply phase functions
    float cosTheta = dot(rayDir, sunDir);
    float rayleighPhaseFactor = rayleighPhase(cosTheta);
    float miePhaseFactor = miePhase(cosTheta, atmosphere.mieDirectionalG);
    
    // Final scattering
    vec3 rayleigh = rayleighScattering * atmosphere.rayleighCoefficient * rayleighPhaseFactor;
    vec3 mie = mieScattering * adjustedMieCoeff * miePhaseFactor;
    
    vec3 color = (rayleigh + mie) * atmosphere.sunColor * atmosphere.sunIntensity;
    
    // Ground contribution (if looking at horizon/below)
    if (groundIntersect.x > 0.0) {
        vec3 groundPoint = rayOrigin + rayDir * groundIntersect.x;
        vec3 groundNormal = normalize(groundPoint - planetCenter);
        float groundLight = max(dot(groundNormal, sunDir), 0.0);
        color += atmosphere.groundAlbedo * groundLight * atmosphere.sunColor * atmosphere.sunIntensity * 0.1;
    }
    
    return color;
}

// Render sun disc
vec3 renderSun(vec3 rayDir, vec3 sunDir) {
    float cosAngle = dot(rayDir, sunDir);
    float sunCosAngle = cos(atmosphere.sunAngularDiameter * 0.5);
    
    if (cosAngle > sunCosAngle) {
        // Inside sun disc
        float limb = 1.0 - pow(1.0 - (cosAngle - sunCosAngle) / (1.0 - sunCosAngle), 0.5);
        return atmosphere.sunColor * atmosphere.sunIntensity * 5.0 * limb;
    }
    
    // Sun glow/bloom
    float glow = pow(max(cosAngle, 0.0), 256.0);
    return atmosphere.sunColor * glow * atmosphere.sunIntensity * 0.5;
}

void main() {
    vec3 viewDir = normalize(inViewRay - atmosphere.cameraPosition);
    vec3 sunDir = normalize(atmosphere.sunDirection);
    
    // Calculate atmospheric scattering
    vec3 skyColor = computeAtmosphericScattering(viewDir, sunDir);
    
    // Add sun disc
    skyColor += renderSun(viewDir, sunDir);
    
    // Night sky (stars) - fade in when sun is below horizon
    float sunHeight = sunDir.y;
    float nightFactor = smoothstep(0.1, -0.2, sunHeight);
    
    if (nightFactor > 0.01 && viewDir.y > 0.0) {
        float starBrightness = stars(viewDir) * atmosphere.starIntensity * nightFactor;
        skyColor += vec3(starBrightness);
        
        // Add subtle Milky Way
        float milkyWay = pow(noise(viewDir.xz * 5.0 + viewDir.y * 3.0), 3.0) * 0.3;
        skyColor += vec3(0.6, 0.7, 1.0) * milkyWay * nightFactor * atmosphere.starIntensity;
    }
    
    // Apply exposure
    skyColor *= atmosphere.exposureMultiplier;
    
    // Tone mapping (ACES approximation)
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    skyColor = clamp((skyColor * (a * skyColor + b)) / (skyColor * (c * skyColor + d) + e), 0.0, 1.0);
    
    outColor = vec4(skyColor, 1.0);
}
