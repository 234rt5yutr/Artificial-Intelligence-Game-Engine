#pragma once

// MCP Asset Import Tools
//
// The engine could render only what it could generate: `Mesh::LoadGLTF` was
// implemented against cgltf but nothing called it, and there was no image import
// at all, so every material texture slot resolved to a 1x1 white placeholder.
// That capped the material graph at flat colours and made the renderer
// impossible to point at real content.
//
// These run on the simulation thread inside the frame's idle window, which is
// what makes it safe for them to submit uploads on the graphics queue.

#include "MCPTool.h"
#include "Core/Application.h"
#include "Core/ECS/Scene.h"
#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanDevice.h"
#include "Core/Renderer/Material/MaterialGraph.h"
#include "Core/Renderer/Mesh.h"
#include "Core/Renderer/Textures/TextureLibrary.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace Core {
namespace MCP {

    namespace AssetToolsDetail {

        // Every path argument is resolved against the project root and rejected
        // if it escapes, matching the project-tool family. An import tool that
        // took absolute paths would be an arbitrary-file-read primitive.
        inline bool ResolveProjectPath(const std::string& relativePath,
                                       std::filesystem::path& resolved,
                                       std::string& error) {
            std::error_code code;
            const std::filesystem::path root = std::filesystem::absolute(
                std::filesystem::current_path(code), code);
            if (code) {
                error = "could not resolve the project root";
                return false;
            }

            std::filesystem::path candidate = root / relativePath;
            candidate = std::filesystem::weakly_canonical(candidate, code);
            if (code) {
                // weakly_canonical fails on a path that does not exist yet; fall
                // back to a lexical normalise so the escape check still runs.
                candidate = (root / relativePath).lexically_normal();
            }

            const std::string rootText = root.lexically_normal().string();
            const std::string candidateText = candidate.lexically_normal().string();
            if (candidateText.size() < rootText.size() ||
                candidateText.compare(0, rootText.size(), rootText) != 0) {
                error = "path escapes the project root";
                return false;
            }

            resolved = candidate;
            return true;
        }

        // Translates a glTF material into a MaterialGraph. This is the whole
        // point of keeping GltfMaterialDesc as plain data: the shape of a glTF
        // material and the shape of the node graph are different problems, and
        // the loader should not know about the second one.
        //
        // A factor and a texture multiply together, which is what glTF specifies
        // and also what makes an unimported texture harmless - the slot keeps
        // its white placeholder and the factor still applies.
        inline Renderer::MaterialGraph BuildGraphFromGltfMaterial(const Renderer::GltfMaterialDesc& desc) {
            using namespace Renderer;
            MaterialGraph graph(desc.Name);

            const uint32_t output = graph.AddNode(MaterialNodeType::Output, "Output");

            // Base colour: factor * texture.
            const uint32_t baseFactor = graph.AddNode(MaterialNodeType::ConstantColor, "BaseColorFactor");
            graph.SetNodeValue(baseFactor, desc.BaseColorFactor);
            if (!desc.BaseColorTexture.empty()) {
                const uint32_t baseTexture = graph.AddNode(MaterialNodeType::TextureSample, "BaseColorTexture");
                graph.SetNodeTextureSlot(baseTexture, desc.BaseColorTexture);
                const uint32_t multiply = graph.AddNode(MaterialNodeType::Multiply, "BaseColor");
                graph.Connect(baseFactor, multiply, 0);
                graph.Connect(baseTexture, multiply, 1);
                graph.Connect(multiply, output, static_cast<uint8_t>(MaterialOutputSlot::BaseColor));
            } else {
                graph.Connect(baseFactor, output, static_cast<uint8_t>(MaterialOutputSlot::BaseColor));
            }

            // glTF packs roughness in G and metallic in B of one texture, so the
            // two outputs are different channels of the same sample.
            if (!desc.MetallicRoughnessTexture.empty()) {
                const uint32_t sample = graph.AddNode(MaterialNodeType::TextureSample, "MetallicRoughness");
                graph.SetNodeTextureSlot(sample, desc.MetallicRoughnessTexture);

                const uint32_t roughnessChannel = graph.AddNode(MaterialNodeType::Split, "RoughnessChannel");
                graph.SetNodeValue(roughnessChannel, Math::Vec4(1.0f, 0.0f, 0.0f, 0.0f)); // G
                graph.Connect(sample, roughnessChannel, 0);
                const uint32_t roughnessFactor = graph.AddNode(MaterialNodeType::ConstantScalar, "RoughnessFactor");
                graph.SetNodeValue(roughnessFactor, Math::Vec4(desc.RoughnessFactor));
                const uint32_t roughness = graph.AddNode(MaterialNodeType::Multiply, "Roughness");
                graph.Connect(roughnessChannel, roughness, 0);
                graph.Connect(roughnessFactor, roughness, 1);
                graph.Connect(roughness, output, static_cast<uint8_t>(MaterialOutputSlot::Roughness));

                const uint32_t metallicChannel = graph.AddNode(MaterialNodeType::Split, "MetallicChannel");
                graph.SetNodeValue(metallicChannel, Math::Vec4(2.0f, 0.0f, 0.0f, 0.0f)); // B
                graph.Connect(sample, metallicChannel, 0);
                const uint32_t metallicFactor = graph.AddNode(MaterialNodeType::ConstantScalar, "MetallicFactor");
                graph.SetNodeValue(metallicFactor, Math::Vec4(desc.MetallicFactor));
                const uint32_t metallic = graph.AddNode(MaterialNodeType::Multiply, "Metallic");
                graph.Connect(metallicChannel, metallic, 0);
                graph.Connect(metallicFactor, metallic, 1);
                graph.Connect(metallic, output, static_cast<uint8_t>(MaterialOutputSlot::Metallic));
            } else {
                const uint32_t roughness = graph.AddNode(MaterialNodeType::ConstantScalar, "Roughness");
                graph.SetNodeValue(roughness, Math::Vec4(desc.RoughnessFactor));
                graph.Connect(roughness, output, static_cast<uint8_t>(MaterialOutputSlot::Roughness));

                const uint32_t metallic = graph.AddNode(MaterialNodeType::ConstantScalar, "Metallic");
                graph.SetNodeValue(metallic, Math::Vec4(desc.MetallicFactor));
                graph.Connect(metallic, output, static_cast<uint8_t>(MaterialOutputSlot::Metallic));
            }

            if (!desc.NormalTexture.empty()) {
                const uint32_t normal = graph.AddNode(MaterialNodeType::TextureSample, "NormalTexture");
                graph.SetNodeTextureSlot(normal, desc.NormalTexture);
                graph.Connect(normal, output, static_cast<uint8_t>(MaterialOutputSlot::Normal));
            }

            const bool hasEmissive = desc.EmissiveFactor.x > 0.0f || desc.EmissiveFactor.y > 0.0f ||
                                     desc.EmissiveFactor.z > 0.0f || !desc.EmissiveTexture.empty();
            if (hasEmissive) {
                const uint32_t emissiveFactor = graph.AddNode(MaterialNodeType::ConstantColor, "EmissiveFactor");
                graph.SetNodeValue(emissiveFactor, Math::Vec4(desc.EmissiveFactor, 1.0f));
                if (!desc.EmissiveTexture.empty()) {
                    const uint32_t emissiveTexture = graph.AddNode(MaterialNodeType::TextureSample, "EmissiveTexture");
                    graph.SetNodeTextureSlot(emissiveTexture, desc.EmissiveTexture);
                    const uint32_t emissive = graph.AddNode(MaterialNodeType::Multiply, "Emissive");
                    graph.Connect(emissiveFactor, emissive, 0);
                    graph.Connect(emissiveTexture, emissive, 1);
                    graph.Connect(emissive, output, static_cast<uint8_t>(MaterialOutputSlot::Emissive));
                } else {
                    graph.Connect(emissiveFactor, output, static_cast<uint8_t>(MaterialOutputSlot::Emissive));
                }
            }

            return graph;
        }

    } // namespace AssetToolsDetail

    // ========================================================================
    // LoadTexture
    // ========================================================================
    class LoadTextureTool : public MCPTool {
    public:
        LoadTextureTool()
            : MCPTool("LoadTexture",
                      "Import an image from the project (PNG, JPEG, TGA, BMP, PSD, GIF, HDR) "
                      "into the texture library under a name, with a full mip chain. That name "
                      "is what a material graph's TextureSample slot resolves against, so "
                      "loading a texture named 'BaseColor' fills every slot called 'BaseColor'. "
                      "Set srgb false for normal, roughness, and metallic maps: they are data, "
                      "not colour, and gamma-decoding them bends the lighting.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "Load Texture";
            annotations.IdempotentHint = true;
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            schema.Properties["name"] = Json{
                {"type", "string"},
                {"description", "Library name. Material texture slots resolve by this."}};
            schema.Properties["path"] = Json{
                {"type", "string"},
                {"description", "Project-relative path to the image file."}};
            schema.Properties["srgb"] = Json{
                {"type", "boolean"},
                {"description", "True for colour maps, false for normal/roughness/metallic data."}};
            schema.Properties["generateMips"] = Json{
                {"type", "boolean"},
                {"description", "Build the mip chain. Off only for lookup tables."}};
            schema.Properties["repeat"] = Json{
                {"type", "boolean"},
                {"description", "Repeat addressing for tiling maps; false clamps to edge."}};
            schema.Properties["compress"] = Json{
                {"type", "boolean"},
                {"description", "BC3 block compression: a quarter of the memory of RGBA8. "
                                "Ignored on devices without BC support."}};
            schema.Required = {"name", "path"};
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json& arguments, ECS::Scene*) override {
            if (!arguments.contains("name") || !arguments["name"].is_string() ||
                !arguments.contains("path") || !arguments["path"].is_string()) {
                return ToolResult::Error("'name' and 'path' are required strings");
            }
            auto& library = Renderer::TextureLibrary::Get();
            if (!library.IsInitialized()) {
                return ToolResult::Error(
                    "The texture library is not available. The engine is headless, or the "
                    "renderer failed to initialize.");
            }

            std::filesystem::path resolved;
            std::string error;
            if (!AssetToolsDetail::ResolveProjectPath(arguments["path"].get<std::string>(),
                                                      resolved, error)) {
                return ToolResult::Error("Texture path rejected: " + error);
            }
            if (!std::filesystem::exists(resolved)) {
                return ToolResult::Error("No such file: " + arguments["path"].get<std::string>());
            }

            Renderer::TextureImportOptions options;
            options.SRGB = arguments.value("srgb", true);
            options.GenerateMips = arguments.value("generateMips", true);
            options.Repeat = arguments.value("repeat", true);
            options.Compress = arguments.value("compress", true);

            const std::string name = arguments["name"].get<std::string>();
            const uint32_t index = library.LoadFromFile(name, resolved.string(), options);
            if (index == UINT32_MAX) {
                return ToolResult::Error("Failed to decode '" + arguments["path"].get<std::string>() +
                                         "'. The format may be unsupported or the file corrupt.");
            }

            const auto* texture = library.GetTexture(index);
            Json state;
            state["index"] = index;
            state["name"] = name;
            state["width"] = texture->Width;
            state["height"] = texture->Height;
            state["mipLevels"] = texture->MipLevels;
            state["srgb"] = texture->SRGB;
            state["bytes"] = texture->SizeBytes;
            state["uncompressedBytes"] = texture->UncompressedBytes;
            state["compressed"] = texture->Compressed;
            return ToolResult::Success(
                "Texture imported; materials with a matching slot name rebind next frame", state);
        }
    };

    // ========================================================================
    // ListTextures
    // ========================================================================
    class ListTexturesTool : public MCPTool {
    public:
        ListTexturesTool()
            : MCPTool("ListTextures",
                      "List every imported texture with its index, dimensions, mip count, and "
                      "colour space, plus the library's total GPU footprint.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "List Textures";
            annotations.ReadOnlyHint = true;
            annotations.IdempotentHint = true;
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            return schema;
        }

        bool RequiresScene() const override { return false; }

        ToolResult Execute(const Json&, ECS::Scene*) override {
            auto& library = Renderer::TextureLibrary::Get();

            Json textures = Json::array();
            const auto& all = library.GetTextures();
            for (std::size_t i = 0; i < all.size(); ++i) {
                Json entry;
                entry["index"] = static_cast<uint32_t>(i);
                entry["name"] = all[i].Name;
                entry["width"] = all[i].Width;
                entry["height"] = all[i].Height;
                entry["mipLevels"] = all[i].MipLevels;
                entry["srgb"] = all[i].SRGB;
                entry["bytes"] = all[i].SizeBytes;
                entry["uncompressedBytes"] = all[i].UncompressedBytes;
                entry["compressed"] = all[i].Compressed;
                if (!all[i].SourcePath.empty()) {
                    entry["source"] = all[i].SourcePath;
                }
                textures.push_back(entry);
            }

            const auto& stats = library.GetStats();
            Json report;
            report["initialized"] = library.IsInitialized();
            report["revision"] = library.GetRevision();
            report["totalBytes"] = stats.TotalBytes;
            report["uncompressedBytes"] = stats.UncompressedBytes;
            report["compressedTextures"] = stats.CompressedTextures;
            report["blockCompressionSupported"] = stats.BlockCompressionSupported;
            report["failedLoads"] = stats.FailedLoads;
            report["textures"] = textures;
            return ToolResult::SuccessJson(report);
        }
    };

    // ========================================================================
    // LoadMesh
    // ========================================================================
    class LoadMeshTool : public MCPTool {
    public:
        LoadMeshTool()
            : MCPTool("LoadMesh",
                      "Import a glTF or GLB mesh from the project and spawn it as an entity, "
                      "with its materials and textures. Base colour, metallic-roughness, normal, "
                      "and emissive become a material graph; images referenced by the file are "
                      "imported into the texture library under names the graph resolves against. "
                      "The mesh is clusterised into the GPU scene like any other, so it goes "
                      "straight through GPU-driven culling. Use SpawnEntity with a primitive "
                      "instead when you just need a box or a sphere.") {}

        ToolAnnotations GetAnnotations() const override {
            ToolAnnotations annotations;
            annotations.Title = "Load Mesh";
            return annotations;
        }

        ToolInputSchema GetInputSchema() const override {
            ToolInputSchema schema;
            schema.Properties = Json::object();
            schema.Properties["path"] = Json{
                {"type", "string"},
                {"description", "Project-relative path to a .gltf or .glb file."}};
            schema.Properties["name"] = Json{
                {"type", "string"},
                {"description", "Entity name. Defaults to the file stem."}};
            schema.Properties["materialIndex"] = Json{
                {"type", "integer"},
                {"description", "Material library index to shade it with. Defaults to 0."}};
            schema.Properties["position"] = Json{
                {"type", "array"},
                {"description", "World position [x, y, z]."},
                {"items", Json{{"type", "number"}}},
                {"minItems", 3}, {"maxItems", 3}};
            schema.Properties["scale"] = Json{
                {"type", "number"},
                {"description", "Uniform scale applied to the spawned entity."}};
            schema.Properties["importMaterials"] = Json{
                {"type", "boolean"},
                {"description", "Import the file's materials and textures and shade the mesh with "
                                "them. Off spawns geometry only, shaded by materialIndex."}};
            schema.Required = {"path"};
            return schema;
        }

        ToolResult Execute(const Json& arguments, ECS::Scene* scene) override {
            if (!scene) {
                return ToolResult::Error("No active scene bound to the MCP server");
            }
            if (!arguments.contains("path") || !arguments["path"].is_string()) {
                return ToolResult::Error("'path' is required and must be a string");
            }

            std::filesystem::path resolved;
            std::string error;
            if (!AssetToolsDetail::ResolveProjectPath(arguments["path"].get<std::string>(),
                                                      resolved, error)) {
                return ToolResult::Error("Mesh path rejected: " + error);
            }
            if (!std::filesystem::exists(resolved)) {
                return ToolResult::Error("No such file: " + arguments["path"].get<std::string>());
            }

            auto mesh = std::make_shared<Renderer::Mesh>();
            Renderer::GltfImportResult imported;
            if (!mesh->LoadGLTF(resolved.string(), &imported)) {
                return ToolResult::Error("Failed to parse '" + arguments["path"].get<std::string>() +
                                         "' as glTF. Check the engine log for the parser's reason.");
            }
            if (mesh->vertices.empty() || mesh->indices.size() < 3) {
                return ToolResult::Error("The file parsed but contained no triangle geometry");
            }

            // Upload for the direct fallback path; the GPU-driven path
            // clusterises from the CPU arrays either way.
            if (auto* application = Application::TryGet()) {
                if (auto device = application->GetRHIDevice()) {
                    mesh->UploadToGPU(*device);
                }
            }

            // Images before materials: a material resolves its texture slots by
            // name, and the library has to already hold them.
            uint32_t imagesImported = 0;
            uint32_t imagesFailed = 0;
            auto& textures = Renderer::TextureLibrary::Get();
            if (arguments.value("importMaterials", true) && textures.IsInitialized()) {
                for (const auto& image : imported.Images) {
                    Renderer::TextureImportOptions options;
                    options.SRGB = image.SRGB;
                    uint32_t index = UINT32_MAX;
                    if (!image.Encoded.empty()) {
                        index = textures.LoadFromEncodedMemory(image.Name, image.Encoded.data(),
                                                               image.Encoded.size(), options);
                    } else if (!image.Path.empty() && std::filesystem::exists(image.Path)) {
                        index = textures.LoadFromFile(image.Name, image.Path, options);
                    } else {
                        ENGINE_CORE_WARN("glTF image '{}' points at '{}', which is missing",
                                         image.Name, image.Path);
                    }
                    if (index == UINT32_MAX) {
                        ++imagesFailed;
                    } else {
                        ++imagesImported;
                    }
                }
            }

            uint32_t firstMaterialIndex = arguments.value("materialIndex", 0u);
            uint32_t materialsImported = 0;
            Json materialNames = Json::array();
            if (arguments.value("importMaterials", true)) {
                auto& library = Renderer::MaterialLibrary::Get();
                library.GetOrCreateDefault();
                for (std::size_t i = 0; i < imported.Materials.size(); ++i) {
                    const auto& desc = imported.Materials[i];
                    const uint32_t index = library.CreateMaterial(desc.Name);
                    if (auto* material = library.GetMaterial(index)) {
                        material->Graph = AssetToolsDetail::BuildGraphFromGltfMaterial(desc);
                        material->DoubleSided = desc.DoubleSided;
                        material->AlphaCutoff = desc.AlphaCutoff;
                        material->AlphaMode =
                            desc.AlphaMode == "BLEND" ? Renderer::MaterialAlphaMode::Blend
                            : desc.AlphaMode == "MASK" ? Renderer::MaterialAlphaMode::Masked
                                                       : Renderer::MaterialAlphaMode::Opaque;
                        library.MarkDirty(index);
                    }
                    if (i == 0) {
                        firstMaterialIndex = index;
                    }
                    materialNames.push_back(desc.Name);
                    ++materialsImported;
                }
                library.CompileDirty();
            }

            const std::string name = arguments.value("name", resolved.stem().string());
            auto& registry = scene->GetRegistry();
            const entt::entity entity = registry.create();

            registry.emplace<ECS::NameComponent>(entity, name);

            ECS::TransformComponent transform;
            if (arguments.contains("position") && arguments["position"].is_array() &&
                arguments["position"].size() == 3) {
                transform.Position = Math::Vec3(arguments["position"][0].get<float>(),
                                                arguments["position"][1].get<float>(),
                                                arguments["position"][2].get<float>());
            }
            const float scale = arguments.value("scale", 1.0f);
            transform.Scale = Math::Vec3(scale);
            registry.emplace<ECS::TransformComponent>(entity, transform);

            // A rigged mesh needs a SkeletalMeshComponent, or nothing advances its
            // pose and nothing collects it as a skinned draw - it would import
            // successfully and then sit in its bind pose forever.
            std::string animationStarted;
            if (mesh->IsSkeletal()) {
                ECS::SkeletalMeshComponent skeletal;
                skeletal.MeshData = mesh;
                skeletal.MaterialIndex = firstMaterialIndex;
                skeletal.CurrentPose.Resize(mesh->GetSkeleton().GetBoneCount());

                // Play the first clip by default. A character that imports
                // standing still looks like the animation import failed.
                const auto& clips = mesh->GetAnimations();
                if (!clips.empty() && arguments.value("playAnimation", true)) {
                    ECS::AnimationInstance instance;
                    instance.AnimationName = clips[0].Name;
                    instance.Clip = &clips[0];
                    instance.Loop = true;
                    instance.State = ECS::AnimationPlaybackState::Playing;
                    skeletal.ActiveAnimations.push_back(instance);
                    animationStarted = clips[0].Name;
                }
                registry.emplace<ECS::SkeletalMeshComponent>(entity, skeletal);
            } else {
                ECS::MeshComponent meshComponent;
                meshComponent.MeshData = mesh;
                meshComponent.MeshPath = arguments["path"].get<std::string>();
                // The file's own first material wins unless the caller named one.
                meshComponent.MaterialIndex = firstMaterialIndex;
                registry.emplace<ECS::MeshComponent>(entity, meshComponent);
            }

            Json state;
            state["entityId"] = static_cast<uint32_t>(entity);
            state["name"] = name;
            state["vertices"] = mesh->vertices.size();
            state["triangles"] = mesh->indices.size() / 3;
            state["submeshes"] = mesh->primitives.size();
            state["materialsImported"] = materialsImported;
            state["materialNames"] = materialNames;
            state["texturesImported"] = imagesImported;
            state["texturesFailed"] = imagesFailed;
            state["materialIndex"] = firstMaterialIndex;
            if (mesh->primitives.size() > 1 && materialsImported > 1) {
                state["multiMaterialNote"] =
                    "Each submesh is drawn as its own instance with its own material. The "
                    "reported materialIndex is the base; a submesh shades with base + its own "
                    "slot in the file.";
            }
            state["skeletal"] = mesh->IsSkeletal();
            state["bones"] = mesh->GetSkeleton().GetBoneCount();
            state["animations"] = mesh->GetAnimations().size();
            if (!animationStarted.empty()) {
                state["playing"] = animationStarted;
            }
            if (mesh->IsSkeletal() && mesh->GetAnimations().empty()) {
                state["note"] = "The file has a rig but no animation clips, so the mesh renders "
                                "in its bind pose.";
            }
            return ToolResult::Success("Mesh imported and spawned", state);
        }
    };

    inline std::vector<MCPToolPtr> CreateAssetTools() {
        std::vector<MCPToolPtr> tools;
        tools.push_back(std::make_shared<LoadTextureTool>());
        tools.push_back(std::make_shared<ListTexturesTool>());
        tools.push_back(std::make_shared<LoadMeshTool>());
        return tools;
    }

} // namespace MCP
} // namespace Core
