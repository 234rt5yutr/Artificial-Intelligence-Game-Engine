#include "Mesh.h"
#include "Core/Log.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace Core {
namespace Renderer {

Mesh::Mesh() = default;
Mesh::~Mesh() = default;

bool Mesh::LoadGLTF(const std::string& filepath) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);

    if (result != cgltf_result_success) {
        ENGINE_CORE_ERROR("Failed to parse GLTF file: {}", filepath);
        return false;
    }

    result = cgltf_load_buffers(&options, data, filepath.c_str());
    if (result != cgltf_result_success) {
        ENGINE_CORE_ERROR("Failed to load buffers for GLTF file: {}", filepath);
        cgltf_free(data);
        return false;
    }

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;

    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        const cgltf_mesh& mesh = data->meshes[i];
        
        for (cgltf_size j = 0; j < mesh.primitives_count; ++j) {
            const cgltf_primitive& primitive = mesh.primitives[j];
            
            // Only support triangles
            if (primitive.type != cgltf_primitive_type_triangles) continue;

            uint32_t firstIndex = indexOffset;
            uint32_t indexCount = 0;

            // Load Indices
            if (primitive.indices) {
                cgltf_accessor* accessor = primitive.indices;
                indexCount = (uint32_t)accessor->count;
                
                for (cgltf_size k = 0; k < accessor->count; ++k) {
                    uint32_t index = (uint32_t)cgltf_accessor_read_index(accessor, k);
                    indices.push_back(index + vertexOffset);
                }
                indexOffset += indexCount;
            }

            // Determine vertex count from the first attribute
            uint32_t vertexCount = 0;
            if (primitive.attributes_count > 0) {
                vertexCount = (uint32_t)primitive.attributes[0].data->count;
            }

            uint32_t primitiveVertexOffset = (uint32_t)vertices.size();
            vertices.resize(vertices.size() + vertexCount);

            // Load Attributes
            for (cgltf_size k = 0; k < primitive.attributes_count; ++k) {
                const cgltf_attribute& attribute = primitive.attributes[k];
                cgltf_accessor* accessor = attribute.data;

                for (cgltf_size v = 0; v < accessor->count; ++v) {
                    Vertex& vertex = vertices[primitiveVertexOffset + v];
                    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                    
                    cgltf_accessor_read_float(accessor, v, values, 4);

                    if (attribute.type == cgltf_attribute_type_position) {
                        vertex.position = Math::Vec3(values[0], values[1], values[2]);
                    } else if (attribute.type == cgltf_attribute_type_normal) {
                        vertex.normal = Math::Vec3(values[0], values[1], values[2]);
                    } else if (attribute.type == cgltf_attribute_type_texcoord) {
                        vertex.texCoord = Math::Vec2(values[0], values[1]);
                    } else if (attribute.type == cgltf_attribute_type_tangent) {
                        vertex.tangent = Math::Vec4(values[0], values[1], values[2], values[3]);
                    }
                }
            }
            vertexOffset += vertexCount;

            // Register primitive
            Primitive p;
            p.firstIndex = firstIndex;
            p.indexCount = indexCount;
            
            // Map material index
            p.materialIndex = 0; // Default
            if (primitive.material) {
                p.materialIndex = static_cast<uint32_t>(primitive.material - data->materials);
            }
            
            primitives.push_back(p);
        }
    }

    cgltf_free(data);
    ENGINE_CORE_INFO("Successfully loaded GLTF mesh: {0} ({1} vertices, {2} indices, {3} primitives)", 
                     filepath, vertices.size(), indices.size(), primitives.size());
    return true;
}

} // namespace Renderer
} // namespace Core
