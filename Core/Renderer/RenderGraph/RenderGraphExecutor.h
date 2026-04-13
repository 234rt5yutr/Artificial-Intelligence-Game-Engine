#pragma once

#include "Core/Renderer/RenderGraph/RenderGraphTypes.h"

namespace Core::Renderer {

class RenderGraphExecutorService {
public:
    Result<RenderGraphExecutionReport> Execute(const RenderGraphExecutionContext& context) const;
};

RenderGraphExecutorService& GetRenderGraphExecutorService();

Result<RenderGraphExecutionReport> ExecuteRenderGraph(const RenderGraphExecutionContext& context);

} // namespace Core::Renderer

