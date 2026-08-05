#include "SceneRenderer.h"

#include "Core/ECS/Systems/LightSystem.h"
#include "Core/ECS/Systems/RenderSystem.h"
#include "Core/Log.h"
#include "Core/RHI/ShaderCompiler.h"
#include "Core/RHI/Vulkan/VulkanBuffer.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Renderer/Material/MaterialGraph.h"
#include "Core/Renderer/Mesh.h"

#include <algorithm>
#include <cstring>

namespace Core {
namespace Renderer {

    namespace {

        constexpr uint32_t kMaxMaterialTextures = 8;
        constexpr uint32_t kResolveGroupSize = 8;

        // Shared declaration block. The material graph's generated code refers to
        // `uMaterial.CameraPosition` and `uMaterial.TimeSeconds`, so the block's
        // instance name and member names are part of the codegen contract.
        const char* kSceneUniformBlock = R"GLSL(
layout(set = 0, binding = 0) uniform SceneUniforms {
    mat4 ViewProjection;
    mat4 View;
    mat4 InverseViewProjection;
    vec4 CameraPosition;
    vec4 Resolution;
    vec4 AmbientColor;
    vec4 DirectionalDirection[4];
    vec4 DirectionalColor[4];
    vec4 PointPositionRadius[16];
    vec4 PointColorIntensity[16];
    uvec4 LightCounts;
    float TimeSeconds;
    float Pad0;
    float Pad1;
    float Pad2;
} uMaterial;
)GLSL";

        const char* kGeometryVertexShader = R"GLSL(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vTexCoord;
layout(location = 3) out vec4 vTangent;

%SCENE_UNIFORMS%

struct Instance {
    mat4 transform;
    vec4 boundsCenterRadius;
    uint clusterBase;
    uint clusterCount;
    uint materialIndex;
    uint flags;
};
layout(std430, set = 0, binding = 1) readonly buffer Instances { Instance instances[]; };

layout(push_constant) uniform Push {
    mat4 model;
    uint useInstanceBuffer;
} push;

void main() {
    // Indirect draws tag every command with its instance through firstInstance,
    // so gl_InstanceIndex is the lookup key. Direct fallback draws push a model
    // matrix instead because they never went through the GPU scene.
    mat4 model = push.useInstanceBuffer != 0u ? instances[gl_InstanceIndex].transform : push.model;

    vec4 world = model * vec4(inPosition, 1.0);
    vWorldPos = world.xyz;
    // Normal matrix from the upper 3x3 inverse transpose, so non-uniform scale
    // does not shear the normals.
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vWorldNormal = normalize(normalMatrix * inNormal);
    vTangent = vec4(normalize(mat3(model) * inTangent.xyz), inTangent.w);
    vTexCoord = inTexCoord;

    gl_Position = uMaterial.ViewProjection * world;
}
)GLSL";

        const char* kGeometryFragmentShader = R"GLSL(
#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outAlbedo;
layout(location = 2) out vec4 outNormal;

%SCENE_UNIFORMS%

layout(set = 1, binding = 0) uniform sampler2D uMaterialTextures[8];

struct MaterialSurface {
    vec3 BaseColor;
    float Metallic;
    float Roughness;
    vec3 Emissive;
    vec3 TangentNormal;
    bool HasTangentNormal;
    float Opacity;
};

const float PI = 3.14159265359;

float DistributionGGX(vec3 n, vec3 h, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = max(dot(n, h), 0.0);
    float denom = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-6);
}

float GeometrySchlickGGX(float ndotv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, 1e-6);
}

float GeometrySmith(vec3 n, vec3 v, vec3 l, float roughness) {
    return GeometrySchlickGGX(max(dot(n, v), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(n, l), 0.0), roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 ShadeLight(vec3 n, vec3 v, vec3 l, vec3 radiance, MaterialSurface surf, vec3 f0) {
    vec3 h = normalize(v + l);
    float ndotl = max(dot(n, l), 0.0);
    if (ndotl <= 0.0) {
        return vec3(0.0);
    }

    float ndf = DistributionGGX(n, h, surf.Roughness);
    float g = GeometrySmith(n, v, l, surf.Roughness);
    vec3 f = FresnelSchlick(max(dot(h, v), 0.0), f0);

    vec3 specular = (ndf * g * f) / max(4.0 * max(dot(n, v), 0.0) * ndotl, 1e-4);
    vec3 kd = (vec3(1.0) - f) * (1.0 - surf.Metallic);
    return (kd * surf.BaseColor / PI + specular) * radiance * ndotl;
}

void main() {
    MaterialSurface surf;
    surf.BaseColor = vec3(0.78, 0.78, 0.80);
    surf.Metallic = 0.0;
    surf.Roughness = 0.6;
    surf.Emissive = vec3(0.0);
    surf.TangentNormal = vec3(0.0, 0.0, 1.0);
    surf.HasTangentNormal = false;
    surf.Opacity = 1.0;

%MATERIAL_BODY%

    vec3 n = normalize(inWorldNormal);
    if (surf.HasTangentNormal) {
        vec3 t = normalize(inTangent.xyz - n * dot(n, inTangent.xyz));
        vec3 b = cross(n, t) * (inTangent.w < 0.0 ? -1.0 : 1.0);
        n = normalize(mat3(t, b, n) * surf.TangentNormal);
    }

    vec3 v = normalize(uMaterial.CameraPosition.xyz - inWorldPos);
    vec3 f0 = mix(vec3(0.04), surf.BaseColor, surf.Metallic);

    vec3 direct = vec3(0.0);
    for (uint i = 0u; i < min(uMaterial.LightCounts.x, 4u); ++i) {
        vec3 l = normalize(-uMaterial.DirectionalDirection[i].xyz);
        vec3 radiance = uMaterial.DirectionalColor[i].rgb * uMaterial.DirectionalColor[i].w;
        direct += ShadeLight(n, v, l, radiance, surf, f0);
    }
    for (uint i = 0u; i < min(uMaterial.LightCounts.y, 16u); ++i) {
        vec3 toLight = uMaterial.PointPositionRadius[i].xyz - inWorldPos;
        float distance = length(toLight);
        float radius = max(uMaterial.PointPositionRadius[i].w, 1e-3);
        if (distance > radius) {
            continue;
        }
        // Inverse-square with a smooth window at the radius, so a light never
        // pops when an object crosses its bound.
        float attenuation = 1.0 / max(distance * distance, 1e-4);
        float window = clamp(1.0 - pow(distance / radius, 4.0), 0.0, 1.0);
        vec3 radiance = uMaterial.PointColorIntensity[i].rgb *
                        uMaterial.PointColorIntensity[i].w * attenuation * window * window;
        direct += ShadeLight(n, v, toLight / max(distance, 1e-5), radiance, surf, f0);
    }

    // A small constant ambient keeps unlit scenes readable. Indirect light is
    // added in the resolve pass from the GI buffer, not here.
    vec3 ambient = uMaterial.AmbientColor.rgb * uMaterial.AmbientColor.w * surf.BaseColor;

    outColor = vec4(direct + ambient + surf.Emissive, surf.Opacity);
    outAlbedo = vec4(surf.BaseColor, surf.Roughness);
    outNormal = vec4(n * 0.5 + 0.5, surf.Metallic);
}
)GLSL";

        const char* kResolveShader = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D uSceneColor;
layout(binding = 1) uniform sampler2D uAlbedo;
layout(binding = 2) uniform sampler2D uGI;
layout(binding = 3, rgba16f) uniform writeonly image2D uResolved;
layout(binding = 4) uniform ResolveParams {
    vec4 sizeParams;   // xy = render size, zw = 1/size
    vec4 flags;        // x = GI enabled
} rp;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= int(rp.sizeParams.x) || pixel.y >= int(rp.sizeParams.y)) {
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) * rp.sizeParams.zw;
    vec3 direct = texture(uSceneColor, uv).rgb;

    // Indirect light is bounced radiance, so it has to be modulated by the
    // surface albedo here rather than in the GI pass, which never saw it.
    vec3 indirect = vec3(0.0);
    if (rp.flags.x > 0.5) {
        indirect = texture(uGI, uv).rgb * texture(uAlbedo, uv).rgb;
    }

    imageStore(uResolved, pixel, vec4(direct + indirect, 1.0));
}
)GLSL";

        const char* kCompositeVertexShader = R"GLSL(
#version 450

layout(location = 0) out vec2 vUV;

// Fullscreen triangle: three vertices, no vertex buffer, no index buffer.
void main() {
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

        const char* kCompositeFragmentShader = R"GLSL(
#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uSource;

layout(push_constant) uniform Push {
    vec4 params;   // x = exposure, y = apply gamma, z = unused, w = unused
} push;

// Narkowicz's ACES fit: one polynomial, no LUT, and close enough to the full
// curve that the difference does not survive an 8-bit swapchain.
vec3 ACESFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 color = texture(uSource, vUV).rgb * push.params.x;
    color = ACESFilm(color);
    if (push.params.y > 0.5) {
        color = pow(color, vec3(1.0 / 2.2));
    }
    outColor = vec4(color, 1.0);
}
)GLSL";

        std::string Substitute(const std::string& source, const std::string& token,
                               const std::string& replacement) {
            std::string result = source;
            std::size_t position = result.find(token);
            while (position != std::string::npos) {
                result.replace(position, token.size(), replacement);
                position = result.find(token, position + replacement.size());
            }
            return result;
        }

        bool IsSrgbFormat(VkFormat format) {
            switch (format) {
                case VK_FORMAT_R8G8B8A8_SRGB:
                case VK_FORMAT_B8G8R8A8_SRGB:
                case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
                    return true;
                default:
                    return false;
            }
        }

    } // namespace

    SceneRenderer::~SceneRenderer() {
        Shutdown();
    }

    bool SceneRenderer::Initialize(RHI::VulkanContext* context) {
        if (!context || context->GetDevice() == VK_NULL_HANDLE) {
            return false;
        }
        Shutdown();
        m_Context = context;

        // Slot 0 must exist before any draw command can reference a material.
        MaterialLibrary::Get().GetOrCreateDefault();

        if (!CreateSharedResources() || !CreateRenderPasses() ||
            !CreateResolvePipeline() || !CreateCompositePipeline()) {
            Shutdown();
            return false;
        }

        if (!m_GPUScene.Initialize(context)) {
            ENGINE_CORE_WARN("GPU scene unavailable; falling back to per-mesh direct draws");
            m_GPUDrivenEnabled = false;
        }
        if (m_GPUDrivenEnabled && !m_Culler.Initialize(context, m_GPUScene.GetLimits().MaxClusterSlots)) {
            ENGINE_CORE_WARN("GPU-driven culler unavailable; falling back to per-mesh direct draws");
            m_GPUDrivenEnabled = false;
        }
        if (!context->SupportsGPUDrivenDraw()) {
            m_GPUDrivenEnabled = false;
        }

        if (!m_GI.Initialize(context)) {
            ENGINE_CORE_WARN("Dynamic GI unavailable; the frame renders direct lighting only");
        }
        if (!m_FSR.Initialize(context)) {
            ENGINE_CORE_WARN("FSR unavailable; the frame presents at render resolution");
        }

        const VkExtent2D extent = context->GetSwapchainExtent();
        if (extent.width > 0 && extent.height > 0 && !OnDisplayResize(extent.width, extent.height)) {
            Shutdown();
            return false;
        }

        ENGINE_CORE_INFO("SceneRenderer initialized (GPU-driven: {}, GI: {}, FSR: {})",
                         m_GPUDrivenEnabled, m_GI.IsInitialized(), m_FSR.IsInitialized());
        return true;
    }

    bool SceneRenderer::CreateSharedResources() {
        VkDevice device = m_Context->GetDevice();

        m_LinearSampler = RHI::CreateClampedSampler(device, VK_FILTER_LINEAR);
        m_PointSampler = RHI::CreateClampedSampler(device, VK_FILTER_NEAREST);
        if (m_LinearSampler == VK_NULL_HANDLE || m_PointSampler == VK_NULL_HANDLE) {
            return false;
        }

        if (!RHI::CreateGpuBuffer(m_Context->GetAllocator(), sizeof(SceneUniforms),
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_SceneUniformBuffer) ||
            !RHI::CreateGpuBuffer(m_Context->GetAllocator(), sizeof(Math::Vec4) * 2,
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, m_ResolveUniformBuffer)) {
            return false;
        }

        // Set 0: scene uniforms + the instance transforms the indirect draws index.
        VkDescriptorSetLayoutBinding sceneBindings[2]{};
        sceneBindings[0].binding = 0;
        sceneBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sceneBindings[0].descriptorCount = 1;
        sceneBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        sceneBindings[1].binding = 1;
        sceneBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sceneBindings[1].descriptorCount = 1;
        sceneBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo sceneLayoutInfo{};
        sceneLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        sceneLayoutInfo.bindingCount = 2;
        sceneLayoutInfo.pBindings = sceneBindings;
        if (vkCreateDescriptorSetLayout(device, &sceneLayoutInfo, nullptr, &m_SceneSetLayout) != VK_SUCCESS) {
            return false;
        }

        // Set 1: the material texture array. Every slot is bound, unused ones to
        // a 1x1 white texture, so a graph that samples a texture the project has
        // not imported yet renders as a neutral multiply instead of failing to
        // build a pipeline.
        VkDescriptorSetLayoutBinding textureBinding{};
        textureBinding.binding = 0;
        textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureBinding.descriptorCount = kMaxMaterialTextures;
        textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo textureLayoutInfo{};
        textureLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        textureLayoutInfo.bindingCount = 1;
        textureLayoutInfo.pBindings = &textureBinding;
        if (vkCreateDescriptorSetLayout(device, &textureLayoutInfo, nullptr,
                                        &m_MaterialTextureSetLayout) != VK_SUCCESS) {
            return false;
        }

        m_ResolveSetLayout = RHI::CreateComputeSetLayout(device, {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        });

        VkDescriptorSetLayoutBinding compositeBinding{};
        compositeBinding.binding = 0;
        compositeBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        compositeBinding.descriptorCount = 1;
        compositeBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo compositeLayoutInfo{};
        compositeLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        compositeLayoutInfo.bindingCount = 1;
        compositeLayoutInfo.pBindings = &compositeBinding;
        if (m_ResolveSetLayout == VK_NULL_HANDLE ||
            vkCreateDescriptorSetLayout(device, &compositeLayoutInfo, nullptr,
                                        &m_CompositeSetLayout) != VK_SUCCESS) {
            return false;
        }

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxMaterialTextures + 8},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 8;
        poolInfo.poolSizeCount = 4;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            return false;
        }

        const VkDescriptorSetLayout layouts[] = {
            m_SceneSetLayout, m_MaterialTextureSetLayout, m_ResolveSetLayout, m_CompositeSetLayout
        };
        VkDescriptorSet sets[4]{};
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 4;
        allocInfo.pSetLayouts = layouts;
        if (vkAllocateDescriptorSets(device, &allocInfo, sets) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("SceneRenderer: descriptor set allocation failed");
            return false;
        }
        m_SceneSet = sets[0];
        m_MaterialTextureSet = sets[1];
        m_ResolveSet = sets[2];
        m_CompositeSet = sets[3];

        // Geometry pipeline layout: set 0 scene, set 1 material textures, plus a
        // model matrix and an instancing switch in push constants.
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(Math::Mat4) + sizeof(uint32_t);

        const VkDescriptorSetLayout geometrySets[] = {m_SceneSetLayout, m_MaterialTextureSetLayout};
        VkPipelineLayoutCreateInfo geometryLayoutInfo{};
        geometryLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        geometryLayoutInfo.setLayoutCount = 2;
        geometryLayoutInfo.pSetLayouts = geometrySets;
        geometryLayoutInfo.pushConstantRangeCount = 1;
        geometryLayoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device, &geometryLayoutInfo, nullptr, &m_GeometryLayout) != VK_SUCCESS) {
            return false;
        }

        return CreateDummyTexture();
    }

    bool SceneRenderer::CreateDummyTexture() {
        RHI::GpuImageDesc desc{};
        desc.Width = 1;
        desc.Height = 1;
        desc.Format = VK_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        desc.DebugName = "MaterialDummyWhite";
        if (!RHI::CreateGpuImage(m_Context->GetDevice(), m_Context->GetAllocator(), desc, m_DummyWhite)) {
            return false;
        }

        // Bind every material texture slot to it up front; slots a graph actually
        // uses are rebound when a real texture exists.
        std::vector<VkDescriptorImageInfo> imageInfos(kMaxMaterialTextures);
        for (auto& info : imageInfos) {
            info.sampler = m_LinearSampler;
            info.imageView = m_DummyWhite.View;
            info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_MaterialTextureSet;
        write.dstBinding = 0;
        write.descriptorCount = kMaxMaterialTextures;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = imageInfos.data();
        vkUpdateDescriptorSets(m_Context->GetDevice(), 1, &write, 0, nullptr);
        return true;
    }

    bool SceneRenderer::CreateRenderPasses() {
        VkDevice device = m_Context->GetDevice();

        // Three colour targets plus depth. Albedo and normal exist so the GI and
        // resolve passes have something to modulate against; without them
        // indirect light could only be added, never coloured by the surface.
        VkAttachmentDescription attachments[4]{};
        const VkFormat colorFormats[3] = {
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R16G16B16A16_SFLOAT,
        };
        for (uint32_t i = 0; i < 3; ++i) {
            attachments[i].format = colorFormats[i];
            attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        attachments[3].format = m_Context->GetDepthFormat();
        attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRefs[3]{};
        for (uint32_t i = 0; i < 3; ++i) {
            colorRefs[i].attachment = i;
            colorRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        VkAttachmentReference depthRef{};
        depthRef.attachment = 3;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 3;
        subpass.pColorAttachments = colorRefs;
        subpass.pDepthStencilAttachment = &depthRef;

        // Both directions: the HZB build reads depth right after the pass, and
        // the late pass writes into targets the compute work just read.
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        passInfo.attachmentCount = 4;
        passInfo.pAttachments = attachments;
        passInfo.subpassCount = 1;
        passInfo.pSubpasses = &subpass;
        passInfo.dependencyCount = 2;
        passInfo.pDependencies = dependencies;

        if (vkCreateRenderPass(device, &passInfo, nullptr, &m_ScenePassClear) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("SceneRenderer: scene render pass creation failed");
            return false;
        }

        // Identical apart from load ops, so pipelines built for one are valid
        // with the other. The late phase must not clear what the early phase drew.
        for (uint32_t i = 0; i < 4; ++i) {
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[i].initialLayout = i == 3 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                  : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        if (vkCreateRenderPass(device, &passInfo, nullptr, &m_ScenePassLoad) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("SceneRenderer: late-phase render pass creation failed");
            return false;
        }
        return true;
    }

    bool SceneRenderer::CreateTargets(uint32_t width, uint32_t height) {
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();

        RHI::GpuImageDesc desc{};
        desc.Width = width;
        desc.Height = height;

        desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        desc.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.DebugName = "SceneColor";
        if (!RHI::CreateGpuImage(device, allocator, desc, m_SceneColor)) {
            return false;
        }

        desc.Format = VK_FORMAT_R8G8B8A8_UNORM;
        desc.DebugName = "SceneAlbedo";
        if (!RHI::CreateGpuImage(device, allocator, desc, m_SceneAlbedo)) {
            return false;
        }

        desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        desc.DebugName = "SceneNormal";
        if (!RHI::CreateGpuImage(device, allocator, desc, m_SceneNormal)) {
            return false;
        }

        desc.Format = m_Context->GetDepthFormat();
        desc.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.Aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        desc.DebugName = "SceneDepth";
        if (!RHI::CreateGpuImage(device, allocator, desc, m_SceneDepth)) {
            return false;
        }

        desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        desc.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        desc.DebugName = "SceneResolved";
        if (!RHI::CreateGpuImage(device, allocator, desc, m_Resolved)) {
            return false;
        }

        const VkImageView views[4] = {
            m_SceneColor.View, m_SceneAlbedo.View, m_SceneNormal.View, m_SceneDepth.View
        };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_ScenePassClear;
        framebufferInfo.attachmentCount = 4;
        framebufferInfo.pAttachments = views;
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = 1;
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_SceneFramebuffer) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("SceneRenderer: scene framebuffer creation failed");
            return false;
        }

        m_RenderWidth = width;
        m_RenderHeight = height;
        m_Stats.RenderWidth = width;
        m_Stats.RenderHeight = height;
        return true;
    }

    void SceneRenderer::DestroyTargets() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        VmaAllocator allocator = m_Context->GetAllocator();

        if (m_SceneFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, m_SceneFramebuffer, nullptr);
            m_SceneFramebuffer = VK_NULL_HANDLE;
        }
        RHI::DestroyGpuImage(device, allocator, m_SceneColor);
        RHI::DestroyGpuImage(device, allocator, m_SceneAlbedo);
        RHI::DestroyGpuImage(device, allocator, m_SceneNormal);
        RHI::DestroyGpuImage(device, allocator, m_SceneDepth);
        RHI::DestroyGpuImage(device, allocator, m_Resolved);
        m_RenderWidth = 0;
        m_RenderHeight = 0;
    }

    bool SceneRenderer::OnDisplayResize(uint32_t width, uint32_t height) {
        if (!m_Context || width == 0 || height == 0) {
            return false;
        }
        m_DisplayWidth = width;
        m_DisplayHeight = height;
        m_Stats.DisplayWidth = width;
        m_Stats.DisplayHeight = height;

        // A swapchain recreate destroys and rebuilds the render pass the
        // composite pipeline was created against, so that pipeline has to be
        // rebuilt with it or the next present draws against a dead handle.
        if (m_CompositePipeline != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_Context->GetDevice());
            vkDestroyPipeline(m_Context->GetDevice(), m_CompositePipeline, nullptr);
            m_CompositePipeline = VK_NULL_HANDLE;
            vkDestroyPipelineLayout(m_Context->GetDevice(), m_CompositeLayout, nullptr);
            m_CompositeLayout = VK_NULL_HANDLE;
            if (!CreateCompositePipeline()) {
                return false;
            }
        }

        return ApplyUpscalerQuality();
    }

    bool SceneRenderer::ApplyUpscalerQuality() {
        if (!m_Context || m_DisplayWidth == 0 || m_DisplayHeight == 0) {
            return false;
        }

        const auto& settings = m_FSR.GetSettings();
        const float scale = (settings.Enabled && m_FSR.IsInitialized())
                                ? FSRRenderScaleFor(settings.Quality)
                                : 1.0f;
        const uint32_t renderWidth = std::max(16u, static_cast<uint32_t>(m_DisplayWidth * scale));
        const uint32_t renderHeight = std::max(16u, static_cast<uint32_t>(m_DisplayHeight * scale));

        vkDeviceWaitIdle(m_Context->GetDevice());
        DestroyTargets();
        if (!CreateTargets(renderWidth, renderHeight)) {
            return false;
        }

        m_Culler.Resize(renderWidth, renderHeight);
        m_GI.Resize(renderWidth, renderHeight);
        m_FSR.Resize(renderWidth, renderHeight, m_DisplayWidth, m_DisplayHeight);

        ENGINE_CORE_INFO("Render resolution {}x{} -> display {}x{} ({} upscaling)",
                         renderWidth, renderHeight, m_DisplayWidth, m_DisplayHeight,
                         FSRQualityModeName(settings.Quality));
        return true;
    }

    bool SceneRenderer::CreateResolvePipeline() {
        m_ResolvePipeline = RHI::CreateComputePipeline(m_Context->GetDevice(),
                                                       m_Context->GetPipelineCache(),
                                                       kResolveShader, "scene_resolve",
                                                       {m_ResolveSetLayout}, 0);
        return m_ResolvePipeline.IsValid();
    }

    bool SceneRenderer::CreateCompositePipeline() {
        VkDevice device = m_Context->GetDevice();

        auto vertSpirv = RHI::ShaderCompiler::CompileToSPIRV(kCompositeVertexShader,
                                                             RHI::ShaderStage::Vertex, "composite.vert");
        auto fragSpirv = RHI::ShaderCompiler::CompileToSPIRV(kCompositeFragmentShader,
                                                             RHI::ShaderStage::Fragment, "composite.frag");
        if (vertSpirv.empty() || fragSpirv.empty()) {
            ENGINE_CORE_ERROR("SceneRenderer: composite shader compilation failed");
            return false;
        }

        VkShaderModule vertModule = m_Context->CreateShaderModule(vertSpirv);
        VkShaderModule fragModule = m_Context->CreateShaderModule(fragSpirv);

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(Math::Vec4);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_CompositeSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_CompositeLayout) != VK_SUCCESS) {
            m_Context->DestroyShaderModule(vertModule);
            m_Context->DestroyShaderModule(fragModule);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";
        stages[1] = stages[0];
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;

        const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_CompositeLayout;
        pipelineInfo.renderPass = m_Context->GetRenderPass();
        pipelineInfo.subpass = 0;

        const VkResult result = vkCreateGraphicsPipelines(device, m_Context->GetPipelineCache(), 1,
                                                          &pipelineInfo, nullptr, &m_CompositePipeline);
        m_Context->DestroyShaderModule(vertModule);
        m_Context->DestroyShaderModule(fragModule);

        if (result != VK_SUCCESS) {
            ENGINE_CORE_ERROR("SceneRenderer: composite pipeline creation failed");
            return false;
        }
        return true;
    }

    // ========================================================================
    // Material pipelines
    // ========================================================================

    void SceneRenderer::EnsureMaterialPipelines() {
        auto& library = MaterialLibrary::Get();
        library.CompileDirty();
        if (library.GetRevision() == m_MaterialLibraryRevision) {
            return;
        }
        m_MaterialLibraryRevision = library.GetRevision();

        VkDevice device = m_Context->GetDevice();
        const std::string sceneUniforms = kSceneUniformBlock;
        const std::string vertexSource = Substitute(kGeometryVertexShader, "%SCENE_UNIFORMS%", sceneUniforms);

        auto vertSpirv = RHI::ShaderCompiler::CompileToSPIRV(vertexSource, RHI::ShaderStage::Vertex,
                                                             "geometry.vert");
        if (vertSpirv.empty()) {
            ENGINE_CORE_ERROR("SceneRenderer: geometry vertex shader failed to compile");
            return;
        }
        VkShaderModule vertModule = m_Context->CreateShaderModule(vertSpirv);

        for (uint32_t index = 0; index < library.GetMaterialCount(); ++index) {
            const MaterialInstance* material = library.GetMaterial(index);
            if (!material) {
                continue;
            }

            auto existing = m_MaterialPipelines.find(index);
            if (existing != m_MaterialPipelines.end() &&
                existing->second.GraphHash == material->Compiled.Hash &&
                existing->second.Valid) {
                continue; // unchanged graph, keep the pipeline
            }
            if (existing != m_MaterialPipelines.end() && existing->second.Pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, existing->second.Pipeline, nullptr);
                existing->second.Pipeline = VK_NULL_HANDLE;
            }

            // A graph that failed to compile still gets a pipeline, built from
            // the template's built-in defaults, so a broken material shows as
            // flat grey rather than dropping the object out of the frame.
            const std::string body = material->Compiled.Succeeded ? material->Compiled.FragmentBody
                                                                  : std::string();
            std::string fragmentSource = Substitute(kGeometryFragmentShader, "%SCENE_UNIFORMS%", sceneUniforms);
            fragmentSource = Substitute(fragmentSource, "%MATERIAL_BODY%", body);

            RHI::ShaderCompileRequest request{};
            request.Source = fragmentSource;
            request.Stage = RHI::ShaderStage::Fragment;
            request.SourceName = "material_" + material->Name;
            const auto compiled = RHI::ShaderCompiler::CompileToSPIRVChecked(request);
            if (!compiled.Succeeded) {
                ENGINE_CORE_ERROR("Material '{}' generated GLSL that does not compile: {}",
                                  material->Name, compiled.ErrorMessage);
                m_MaterialPipelines[index] = MaterialPipeline{VK_NULL_HANDLE, material->Compiled.Hash, false};
                continue;
            }

            VkShaderModule fragModule = m_Context->CreateShaderModule(compiled.Spirv);

            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertModule;
            stages[0].pName = "main";
            stages[1] = stages[0];
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragModule;

            VkVertexInputBindingDescription binding{};
            binding.binding = 0;
            binding.stride = sizeof(Vertex);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            VkVertexInputAttributeDescription attributes[4]{};
            attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
            attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
            attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord)};
            attributes[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)};

            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInput.vertexBindingDescriptionCount = 1;
            vertexInput.pVertexBindingDescriptions = &binding;
            vertexInput.vertexAttributeDescriptionCount = 4;
            vertexInput.pVertexAttributeDescriptions = attributes;

            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = material->DoubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_TRUE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

            VkPipelineColorBlendAttachmentState blendAttachments[3]{};
            for (auto& attachment : blendAttachments) {
                attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            }

            VkPipelineColorBlendStateCreateInfo colorBlend{};
            colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlend.attachmentCount = 3;
            colorBlend.pAttachments = blendAttachments;

            const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = 2;
            dynamicState.pDynamicStates = dynamicStates;

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = stages;
            pipelineInfo.pVertexInputState = &vertexInput;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &raster;
            pipelineInfo.pMultisampleState = &multisample;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &colorBlend;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = m_GeometryLayout;
            pipelineInfo.renderPass = m_ScenePassClear;
            pipelineInfo.subpass = 0;

            MaterialPipeline entry{};
            entry.GraphHash = material->Compiled.Hash;
            if (vkCreateGraphicsPipelines(device, m_Context->GetPipelineCache(), 1, &pipelineInfo,
                                          nullptr, &entry.Pipeline) == VK_SUCCESS) {
                entry.Valid = true;
            } else {
                ENGINE_CORE_ERROR("Material '{}' pipeline creation failed", material->Name);
            }
            m_MaterialPipelines[index] = entry;
            m_Context->DestroyShaderModule(fragModule);
        }

        m_Context->DestroyShaderModule(vertModule);

        uint32_t validPipelines = 0;
        for (const auto& [_, pipeline] : m_MaterialPipelines) {
            validPipelines += pipeline.Valid ? 1u : 0u;
        }
        m_Stats.MaterialPipelines = validPipelines;
        ENGINE_CORE_INFO("Material pipelines rebuilt: {} valid of {} materials",
                         validPipelines, library.GetMaterialCount());
    }

    VkPipeline SceneRenderer::GetPipelineForMaterial(uint32_t materialIndex) {
        auto it = m_MaterialPipelines.find(materialIndex);
        if (it != m_MaterialPipelines.end() && it->second.Valid) {
            return it->second.Pipeline;
        }
        // Fall back to material 0, which the library guarantees exists.
        auto fallback = m_MaterialPipelines.find(0);
        return fallback != m_MaterialPipelines.end() && fallback->second.Valid
                   ? fallback->second.Pipeline
                   : VK_NULL_HANDLE;
    }

    // ========================================================================
    // Frame
    // ========================================================================

    void SceneRenderer::UpdateSceneUniforms(const FrameRenderData& frame) {
        SceneUniforms uniforms{};
        uniforms.ViewProjection = frame.ViewProjection;
        uniforms.View = frame.View;
        uniforms.InverseViewProjection = glm::inverse(frame.ViewProjection);
        uniforms.CameraPosition = Math::Vec4(frame.CameraPosition, 1.0f);
        uniforms.Resolution = Math::Vec4(static_cast<float>(m_RenderWidth),
                                         static_cast<float>(m_RenderHeight),
                                         m_RenderWidth > 0 ? 1.0f / m_RenderWidth : 0.0f,
                                         m_RenderHeight > 0 ? 1.0f / m_RenderHeight : 0.0f);
        uniforms.AmbientColor = Math::Vec4(0.05f, 0.06f, 0.08f, 1.0f);
        uniforms.TimeSeconds = frame.TimeSeconds;

        const uint32_t directionalCount =
            std::min<uint32_t>(4, static_cast<uint32_t>(frame.DirectionalLights.size()));
        for (uint32_t i = 0; i < directionalCount; ++i) {
            const auto& light = frame.DirectionalLights[i];
            uniforms.DirectionalDirection[i] = Math::Vec4(light.Direction, 0.0f);
            uniforms.DirectionalColor[i] = Math::Vec4(light.Color, light.Intensity);
        }

        const uint32_t pointCount =
            std::min<uint32_t>(16, static_cast<uint32_t>(frame.PointLights.size()));
        for (uint32_t i = 0; i < pointCount; ++i) {
            const auto& light = frame.PointLights[i];
            uniforms.PointPositionRadius[i] = Math::Vec4(light.Position, light.Radius);
            uniforms.PointColorIntensity[i] = Math::Vec4(light.Color, light.Intensity);
        }
        uniforms.LightCounts = Math::UVec4(directionalCount, pointCount, 0, 0);

        if (m_SceneUniformBuffer.Mapped) {
            std::memcpy(m_SceneUniformBuffer.Mapped, &uniforms, sizeof(uniforms));
        }

        m_Stats.DirectionalLights = directionalCount;
        m_Stats.PointLights = pointCount;
        if (frame.DirectionalLights.size() > 4 || frame.PointLights.size() > 16) {
            ENGINE_CORE_WARN("Light list exceeds the forward shader's caps ({} directional, {} point); "
                             "the surplus is dropped this frame",
                             frame.DirectionalLights.size(), frame.PointLights.size());
        }
    }

    void SceneRenderer::BeginFrame(const FrameRenderData& frame) {
        if (!IsInitialized()) {
            return;
        }

        m_Frame = frame;
        EnsureMaterialPipelines();
        UpdateSceneUniforms(m_Frame);

        // Split the draw list: anything the GPU scene accepted goes through the
        // indirect path, the rest keeps a direct draw so it still appears.
        m_DirectDraws.clear();
        m_Stats.SkippedDraws = 0;

        if (m_GPUDrivenEnabled && m_GPUScene.IsInitialized()) {
            m_GPUScene.BeginFrame(m_Frame.DrawCommands.data(), m_Frame.DrawCommands.size());
        }

        for (const auto& command : m_Frame.DrawCommands) {
            const Mesh* mesh = command.Mesh;
            if (!mesh) {
                ++m_Stats.SkippedDraws;
                continue;
            }
            // Resident meshes are drawn by the indirect path; only the rest need
            // a direct draw.
            if (m_GPUDrivenEnabled && m_GPUScene.IsInitialized() && m_GPUScene.EnsureResident(mesh)) {
                continue;
            }
            if (!mesh->IsUploaded() || mesh->GetUploadedIndexCount() == 0) {
                ++m_Stats.SkippedDraws;
                continue;
            }
            if (mesh->GetUploadedVertexStride() != sizeof(Vertex)) {
                // Skinned meshes upload a wider vertex; drawing them through the
                // static layout would read garbage. Skipping is the honest
                // behaviour until GPU skinning feeds this path.
                if (!m_WarnedSkinned) {
                    ENGINE_CORE_WARN("Skeletal meshes are not drawn by SceneRenderer yet "
                                     "(vertex stride {} != {})",
                                     mesh->GetUploadedVertexStride(), sizeof(Vertex));
                    m_WarnedSkinned = true;
                }
                ++m_Stats.SkippedDraws;
                continue;
            }
            m_DirectDraws.push_back(&command);
        }

        // Descriptor set 0 points at this frame's uniforms and instance list.
        VkDescriptorBufferInfo uniformInfo{m_SceneUniformBuffer.Buffer, 0, sizeof(SceneUniforms)};
        VkBuffer instanceBuffer = m_GPUScene.IsInitialized() ? m_GPUScene.GetInstanceBuffer()
                                                             : VK_NULL_HANDLE;
        VkWriteDescriptorSet writes[2]{};
        uint32_t writeCount = 1;
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_SceneSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &uniformInfo;

        VkDescriptorBufferInfo instanceInfo{instanceBuffer, 0, VK_WHOLE_SIZE};
        if (instanceBuffer != VK_NULL_HANDLE) {
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = m_SceneSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].pBufferInfo = &instanceInfo;
            writeCount = 2;
        }
        vkUpdateDescriptorSets(m_Context->GetDevice(), writeCount, writes, 0, nullptr);
    }

    void SceneRenderer::RecordGeometry(VkCommandBuffer cmd, bool latePhase) {
        VkViewport viewport{};
        viewport.width = static_cast<float>(m_RenderWidth);
        viewport.height = static_cast<float>(m_RenderHeight);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = {m_RenderWidth, m_RenderHeight};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const VkDescriptorSet sets[] = {m_SceneSet, m_MaterialTextureSet};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GeometryLayout,
                                0, 2, sets, 0, nullptr);

        const bool indirectAvailable = m_GPUDrivenEnabled && m_GPUScene.IsInitialized() &&
                                       m_Culler.IsInitialized() &&
                                       m_GPUScene.GetFrameClusterSlotCount() > 0;
        if (indirectAvailable) {
            const VkBuffer drawBuffer = latePhase ? m_Culler.GetLateDrawBuffer()
                                                  : m_Culler.GetEarlyDrawBuffer();
            VkBuffer vertexBuffer = m_GPUScene.GetVertexBuffer();
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmd, m_GPUScene.GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            uint32_t issued = 0;
            for (const auto& batch : m_GPUScene.GetMaterialBatches()) {
                VkPipeline pipeline = GetPipelineForMaterial(batch.MaterialIndex);
                if (pipeline == VK_NULL_HANDLE || batch.ClusterSlotCount == 0) {
                    continue;
                }
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

                // One multi-draw per material. Culled clusters are still in the
                // buffer with instanceCount 0, which the GPU discards for free.
                const uint32_t pushInstancing = 1;
                Math::Mat4 identity(1.0f);
                vkCmdPushConstants(cmd, m_GeometryLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(Math::Mat4), &identity[0][0]);
                vkCmdPushConstants(cmd, m_GeometryLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                   sizeof(Math::Mat4), sizeof(uint32_t), &pushInstancing);

                vkCmdDrawIndexedIndirect(cmd, drawBuffer,
                                         static_cast<VkDeviceSize>(batch.FirstClusterSlot) *
                                             GPUDrivenCuller::kDrawCommandStride,
                                         batch.ClusterSlotCount,
                                         GPUDrivenCuller::kDrawCommandStride);
                ++issued;
            }
            if (!latePhase) {
                m_Stats.IndirectDraws = issued;
                m_Stats.GPUDrivenActive = issued > 0;
            }
        } else if (!latePhase) {
            m_Stats.IndirectDraws = 0;
            m_Stats.GPUDrivenActive = false;
        }

        // Direct draws only run in the early phase: they have no cluster
        // visibility state, so re-issuing them late would double-shade them.
        if (!latePhase) {
            RecordDirectDraws(cmd);
        }
    }

    void SceneRenderer::RecordDirectDraws(VkCommandBuffer cmd) {
        uint32_t drawn = 0;
        for (const ECS::DrawCommand* command : m_DirectDraws) {
            const Mesh* mesh = command->Mesh;
            const auto* vertexBuffer = static_cast<const RHI::VulkanBuffer*>(mesh->vertexBuffer.get());
            const auto* indexBuffer = static_cast<const RHI::VulkanBuffer*>(mesh->indexBuffer.get());
            if (!vertexBuffer || !indexBuffer) {
                continue;
            }

            VkPipeline pipeline = GetPipelineForMaterial(command->MaterialIndex);
            if (pipeline == VK_NULL_HANDLE) {
                continue;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            const uint32_t pushInstancing = 0;
            vkCmdPushConstants(cmd, m_GeometryLayout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(Math::Mat4), &command->Transform[0][0]);
            vkCmdPushConstants(cmd, m_GeometryLayout, VK_SHADER_STAGE_VERTEX_BIT,
                               sizeof(Math::Mat4), sizeof(uint32_t), &pushInstancing);

            VkBuffer vertexHandle = vertexBuffer->GetBuffer();
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexHandle, &offset);
            vkCmdBindIndexBuffer(cmd, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh->GetUploadedIndexCount(), 1, 0, 0, 0);
            ++drawn;
        }
        m_Stats.DirectDraws = drawn;
    }

    void SceneRenderer::RecordOffscreen(VkCommandBuffer cmd) {
        if (!IsInitialized() || m_SceneFramebuffer == VK_NULL_HANDLE) {
            return;
        }

        if (!m_DummyInitialized) {
            // One-shot: give the material texture slots defined contents before
            // any shader samples them.
            RHI::TransitionImage(cmd, m_DummyWhite, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkClearColorValue white{};
            white.float32[0] = white.float32[1] = white.float32[2] = white.float32[3] = 1.0f;
            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.levelCount = 1;
            range.layerCount = 1;
            vkCmdClearColorImage(cmd, m_DummyWhite.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &white, 1, &range);
            RHI::TransitionImage(cmd, m_DummyWhite, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            m_DummyInitialized = true;
        }

        GpuCullView cullView{};
        cullView.View = m_Frame.View;
        cullView.Projection = m_Frame.Projection;
        cullView.ViewProjection = m_Frame.ViewProjection;
        cullView.CameraPosition = m_Frame.CameraPosition;

        const bool indirectAvailable = m_GPUDrivenEnabled && m_GPUScene.IsInitialized() &&
                                       m_Culler.IsInitialized() &&
                                       m_GPUScene.GetFrameClusterSlotCount() > 0;
        if (indirectAvailable) {
            m_Culler.BeginFrame(cmd, m_GPUScene, cullView);
            m_Culler.CullEarly(cmd);
        }

        VkClearValue clearValues[4]{};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[2].color = {{0.5f, 0.5f, 0.5f, 0.0f}};
        clearValues[3].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo passBegin{};
        passBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        passBegin.renderPass = m_ScenePassClear;
        passBegin.framebuffer = m_SceneFramebuffer;
        passBegin.renderArea.extent = {m_RenderWidth, m_RenderHeight};
        passBegin.clearValueCount = 4;
        passBegin.pClearValues = clearValues;

        vkCmdBeginRenderPass(cmd, &passBegin, VK_SUBPASS_CONTENTS_INLINE);
        RecordGeometry(cmd, false);
        vkCmdEndRenderPass(cmd);

        // Track what the render pass's final layouts left behind, so the
        // transitions below start from the right place.
        m_SceneColor.Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        m_SceneAlbedo.Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        m_SceneNormal.Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        m_SceneDepth.Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        if (indirectAvailable && m_Culler.IsTwoPhaseEnabled()) {
            // Rebuild the HZB from the depth the early phase just wrote, then
            // give the clusters it rejected for occlusion a second test.
            RHI::TransitionImage(cmd, m_SceneDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            m_Culler.BuildHZB(cmd, m_SceneDepth.View, m_PointSampler);
            m_Culler.CullLate(cmd);
            RHI::TransitionImage(cmd, m_SceneDepth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            passBegin.renderPass = m_ScenePassLoad;
            passBegin.clearValueCount = 0;
            passBegin.pClearValues = nullptr;
            vkCmdBeginRenderPass(cmd, &passBegin, VK_SUBPASS_CONTENTS_INLINE);
            RecordGeometry(cmd, true);
            vkCmdEndRenderPass(cmd);
        }

        RHI::TransitionImage(cmd, m_SceneColor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        RHI::TransitionImage(cmd, m_SceneAlbedo, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        RHI::TransitionImage(cmd, m_SceneNormal, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        RHI::TransitionImage(cmd, m_SceneDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (indirectAvailable && !m_Culler.IsTwoPhaseEnabled()) {
            // Single-phase still needs the HZB refreshed for next frame's early test.
            m_Culler.BuildHZB(cmd, m_SceneDepth.View, m_PointSampler);
        }

        GIFrameInputs giInputs{};
        giInputs.DepthView = m_SceneDepth.View;
        giInputs.NormalView = m_SceneNormal.View;
        giInputs.ColorView = m_SceneColor.View;
        giInputs.LinearSampler = m_LinearSampler;
        giInputs.View = m_Frame.View;
        giInputs.Projection = m_Frame.Projection;
        giInputs.ViewProjection = m_Frame.ViewProjection;
        giInputs.PreviousViewProjection = m_PreviousViewProjection;
        giInputs.CameraPosition = m_Frame.CameraPosition;
        giInputs.FrameIndex = m_Frame.FrameIndex;
        m_GI.Render(cmd, giInputs);

        // Resolve: direct lighting plus albedo-modulated indirect.
        {
            Math::Vec4 resolveParams[2]{};
            resolveParams[0] = Math::Vec4(static_cast<float>(m_RenderWidth),
                                          static_cast<float>(m_RenderHeight),
                                          1.0f / static_cast<float>(m_RenderWidth),
                                          1.0f / static_cast<float>(m_RenderHeight));
            resolveParams[1] = Math::Vec4(m_GI.GetStats().Ready ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
            if (m_ResolveUniformBuffer.Mapped) {
                std::memcpy(m_ResolveUniformBuffer.Mapped, resolveParams, sizeof(resolveParams));
            }

            RHI::TransitionImage(cmd, m_Resolved, VK_IMAGE_LAYOUT_GENERAL);

            VkImageView giView = m_GI.GetStats().Ready ? m_GI.GetRadianceView() : m_SceneColor.View;
            VkDescriptorImageInfo imageInfos[4]{};
            imageInfos[0] = {m_LinearSampler, m_SceneColor.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            imageInfos[1] = {m_LinearSampler, m_SceneAlbedo.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            imageInfos[2] = {m_LinearSampler, giView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            imageInfos[3] = {VK_NULL_HANDLE, m_Resolved.View, VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorBufferInfo resolveInfo{m_ResolveUniformBuffer.Buffer, 0, sizeof(resolveParams)};

            VkWriteDescriptorSet writes[5]{};
            for (uint32_t i = 0; i < 3; ++i) {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = m_ResolveSet;
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].pImageInfo = &imageInfos[i];
            }
            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = m_ResolveSet;
            writes[3].dstBinding = 3;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[3].pImageInfo = &imageInfos[3];
            writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[4].dstSet = m_ResolveSet;
            writes[4].dstBinding = 4;
            writes[4].descriptorCount = 1;
            writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[4].pBufferInfo = &resolveInfo;
            vkUpdateDescriptorSets(m_Context->GetDevice(), 5, writes, 0, nullptr);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ResolvePipeline.Pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ResolvePipeline.Layout,
                                    0, 1, &m_ResolveSet, 0, nullptr);
            vkCmdDispatch(cmd,
                          (m_RenderWidth + kResolveGroupSize - 1) / kResolveGroupSize,
                          (m_RenderHeight + kResolveGroupSize - 1) / kResolveGroupSize, 1);

            RHI::TransitionImage(cmd, m_Resolved, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if (m_FSR.IsInitialized() && m_FSR.GetSettings().Enabled &&
            m_FSR.GetSettings().Quality != FSRQualityMode::Off) {
            m_FSR.Render(cmd, m_Resolved.View, m_LinearSampler);
        }

        m_PreviousViewProjection = m_Frame.ViewProjection;
    }

    VkImageView SceneRenderer::GetViewportImageView() const {
        if (m_FSR.GetStats().Active) {
            return m_FSR.GetOutputView();
        }
        return m_Resolved.View;
    }

    void SceneRenderer::RecordComposite(VkCommandBuffer cmd) {
        if (!IsInitialized() || m_CompositePipeline == VK_NULL_HANDLE) {
            return;
        }

        VkImageView sourceView = GetViewportImageView();
        if (sourceView == VK_NULL_HANDLE) {
            return;
        }

        VkDescriptorImageInfo sourceInfo{m_LinearSampler, sourceView,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_CompositeSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &sourceInfo;
        vkUpdateDescriptorSets(m_Context->GetDevice(), 1, &write, 0, nullptr);

        const VkExtent2D extent = m_Context->GetSwapchainExtent();
        VkViewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // An sRGB swapchain encodes gamma in hardware; anything else needs it in
        // the shader, or the image presents roughly twice as dark as intended.
        const float applyGamma = IsSrgbFormat(m_Context->GetSwapchainImageFormat()) ? 0.0f : 1.0f;
        const Math::Vec4 params(1.0f, applyGamma, 0.0f, 0.0f);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_CompositePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_CompositeLayout,
                                0, 1, &m_CompositeSet, 0, nullptr);
        vkCmdPushConstants(cmd, m_CompositeLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(Math::Vec4), &params);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    Math::Vec2 SceneRenderer::GetProjectionJitter(uint64_t frameIndex) const {
        if (!m_FSR.IsInitialized() || m_RenderWidth == 0 || m_RenderHeight == 0) {
            return Math::Vec2(0.0f);
        }
        const Math::Vec2 pixelJitter = m_FSR.GetJitter(frameIndex);
        // Pixels to NDC: a full pixel is 2/width of clip space.
        return Math::Vec2(pixelJitter.x * 2.0f / static_cast<float>(m_RenderWidth),
                          pixelJitter.y * 2.0f / static_cast<float>(m_RenderHeight));
    }

    void SceneRenderer::Shutdown() {
        if (!m_Context) {
            return;
        }
        VkDevice device = m_Context->GetDevice();
        vkDeviceWaitIdle(device);

        m_FSR.Shutdown();
        m_GI.Shutdown();
        m_Culler.Shutdown();
        m_GPUScene.Shutdown();

        for (auto& [_, pipeline] : m_MaterialPipelines) {
            if (pipeline.Pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline.Pipeline, nullptr);
            }
        }
        m_MaterialPipelines.clear();
        m_MaterialLibraryRevision = UINT64_MAX;

        DestroyTargets();
        RHI::DestroyGpuImage(device, m_Context->GetAllocator(), m_DummyWhite);
        m_DummyInitialized = false;

        RHI::DestroyComputePipeline(device, m_ResolvePipeline);
        if (m_CompositePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, m_CompositePipeline, nullptr);
            m_CompositePipeline = VK_NULL_HANDLE;
        }
        for (VkPipelineLayout* layout : {&m_GeometryLayout, &m_CompositeLayout}) {
            if (*layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, *layout, nullptr);
                *layout = VK_NULL_HANDLE;
            }
        }
        for (VkRenderPass* pass : {&m_ScenePassClear, &m_ScenePassLoad}) {
            if (*pass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, *pass, nullptr);
                *pass = VK_NULL_HANDLE;
            }
        }
        if (m_DescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }
        for (VkDescriptorSetLayout* layout : {&m_SceneSetLayout, &m_MaterialTextureSetLayout,
                                              &m_ResolveSetLayout, &m_CompositeSetLayout}) {
            if (*layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, *layout, nullptr);
                *layout = VK_NULL_HANDLE;
            }
        }
        for (VkSampler* sampler : {&m_LinearSampler, &m_PointSampler}) {
            if (*sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, *sampler, nullptr);
                *sampler = VK_NULL_HANDLE;
            }
        }
        RHI::DestroyGpuBuffer(m_Context->GetAllocator(), m_SceneUniformBuffer);
        RHI::DestroyGpuBuffer(m_Context->GetAllocator(), m_ResolveUniformBuffer);

        m_Context = nullptr;
    }

} // namespace Renderer
} // namespace Core
