#pragma once

#include "Core/Renderer/VirtualGeometry/VirtualGeometryTypes.h"

namespace Core::Renderer {

class VirtualGeometryStreamingService {
public:
    Result<VirtualGeometryStreamResult> Stream(const VirtualGeometryStreamRequest& request);
    VirtualGeometryStreamingDiagnostics GetDiagnostics() const;
    void Reset();
};

VirtualGeometryStreamingService& GetVirtualGeometryStreamingService();

Result<VirtualGeometryStreamResult> StreamVirtualGeometryPages(const VirtualGeometryStreamRequest& request);

VirtualGeometryStreamingDiagnostics GetVirtualGeometryStreamingDiagnostics();

} // namespace Core::Renderer

