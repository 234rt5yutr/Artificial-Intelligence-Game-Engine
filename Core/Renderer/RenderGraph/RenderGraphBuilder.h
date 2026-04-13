#pragma once

#include "Core/Renderer/RenderGraph/RenderGraphTypes.h"

#include <vector>

namespace Core::Renderer {

struct RegisteredRenderGraphPass {
    RenderGraphPassHandle Handle;
    RenderGraphPassRegistration Registration;
};

class RenderGraphBuilderService {
public:
    Result<RenderGraphPassHandle> RegisterPass(const RenderGraphPassRegistration& registration);
    std::vector<RegisteredRenderGraphPass> GetRegisteredPasses() const;
    void Clear();
};

RenderGraphBuilderService& GetRenderGraphBuilderService();

Result<RenderGraphPassHandle> RegisterRenderGraphPass(const RenderGraphPassRegistration& registration);

} // namespace Core::Renderer

