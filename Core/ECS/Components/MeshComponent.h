#pragma once

#include "Core/Renderer/Mesh.h"
#include <cstdint>
#include <memory>
#include <string>

namespace Core {
namespace ECS {

    struct MeshComponent {
        std::shared_ptr<Renderer::Mesh> MeshData;
        
        // Material properties (to be expanded in future steps)
        uint32_t MaterialIndex = 0;
        
        // Rendering flags
        bool Visible = true;
        bool CastShadows = true;
        bool ReceiveShadows = true;

        // Optional mesh identifier for debugging/serialization
        std::string MeshPath;

        // Hot-reload generation tracking (Stage 23)
        uint64_t AssetGeneration = 0;
        uint64_t LastBoundGeneration = 0;

        MeshComponent() = default;
        MeshComponent(std::shared_ptr<Renderer::Mesh> mesh)
            : MeshData(mesh) {}
        MeshComponent(std::shared_ptr<Renderer::Mesh> mesh, const std::string& path)
            : MeshData(mesh), MeshPath(path) {}

        bool IsValid() const { return MeshData != nullptr; }
    };

} // namespace ECS
} // namespace Core
