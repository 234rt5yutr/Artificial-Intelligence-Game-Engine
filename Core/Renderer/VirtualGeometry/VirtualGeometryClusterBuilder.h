#pragma once

#include "Core/Renderer/VirtualGeometry/VirtualGeometryTypes.h"

namespace Core::Renderer {

class VirtualGeometryClusterBuilderService {
public:
    Result<VirtualGeometryClusterBuildResult> Build(const VirtualGeometryClusterBuildRequest& request);
    const VirtualGeometryMetadata* TryGetMetadataForMesh(const Mesh* mesh) const;
    const VirtualGeometryMetadata* TryGetMetadataByKey(uint64_t metadataKey) const;
    void Clear();
};

VirtualGeometryClusterBuilderService& GetVirtualGeometryClusterBuilderService();

Result<VirtualGeometryClusterBuildResult> BuildVirtualizedGeometryClusters(
    const VirtualGeometryClusterBuildRequest& request);

} // namespace Core::Renderer

