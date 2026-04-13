#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Core::Renderer {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class RenderGraphQueue : uint8_t {
    Graphics = 0,
    Compute = 1,
    Transfer = 2
};

enum class RenderGraphResourceState : uint8_t {
    Unknown = 0,
    ShaderRead = 1,
    ShaderWrite = 2,
    ReadWrite = 3,
    ColorAttachment = 4,
    DepthRead = 5,
    DepthWrite = 6,
    TransferSrc = 7,
    TransferDst = 8,
    Present = 9
};

struct RenderGraphResourceAccessDeclaration {
    std::string Resource;
    RenderGraphResourceState State = RenderGraphResourceState::Unknown;
    uint64_t EstimatedSizeBytes = 0;
};

struct RenderGraphPassHandle {
    uint32_t Value = 0;
    std::string PassId;
};

using RenderGraphPassCallback = std::function<void()>;

struct RenderGraphPassRegistration {
    std::string PassId;
    std::string PassName;
    std::string DebugLabel;
    RenderGraphQueue Queue = RenderGraphQueue::Graphics;
    RenderGraphPassCallback Callback;
    std::vector<RenderGraphResourceAccessDeclaration> Reads;
    std::vector<RenderGraphResourceAccessDeclaration> Writes;
    std::vector<std::string> DependsOn;
    std::vector<std::string> ImportedResources;
    bool HasSideEffects = false;
};

struct RenderGraphExecutionContext {
    std::string FrameTag;
    bool AllowTransientAliasing = true;
    bool EmitDiagnostics = true;
    bool StrictMode = false;
    double MaxCompileBudgetMs = 0.0;
};

struct RenderGraphPassTiming {
    std::string PassId;
    double DurationMs = 0.0;
};

struct RenderGraphResourceLifetime {
    std::string Resource;
    uint32_t FirstPassIndex = 0;
    uint32_t LastPassIndex = 0;
    uint32_t AliasGroup = 0;
    uint64_t EstimatedSizeBytes = 0;
    bool Imported = false;
};

struct RenderGraphExecutionReport {
    std::vector<std::string> PassOrder;
    std::vector<RenderGraphPassTiming> PassTimings;
    std::vector<RenderGraphResourceLifetime> ResourceLifetimes;
    std::vector<std::string> ValidationDiagnostics;
    std::string CompileDigest;
    std::string DiagnosticsJson;
    uint32_t BarrierCount = 0;
    uint32_t QueueSynchronizationCount = 0;
    uint64_t AliasSavingsBytes = 0;
};

} // namespace Core::Renderer

