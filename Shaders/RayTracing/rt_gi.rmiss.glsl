// RT GI Miss Shader
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 giPayload;

void main() {
    // Sky contribution - simple ambient
    vec3 skyColor = vec3(0.1, 0.15, 0.2);
    giPayload = vec4(skyColor, -1.0);  // -1 indicates sky hit
}
