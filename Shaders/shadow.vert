#version 450

layout(location = 0) in vec3 inPosition;

// We assume there's a set = 0, binding = 0 for the light space matrix
layout(set = 0, binding = 0) uniform ViewProjUBO {
    mat4 lightSpaceMatrix;
} ubo;

// We assume there's push constants for the model matrix
layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
} push;

void main() {
    gl_Position = ubo.lightSpaceMatrix * push.modelMatrix * vec4(inPosition, 1.0);
}
