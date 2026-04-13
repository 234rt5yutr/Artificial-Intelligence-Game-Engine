#include "ImGuiSubsystem.h"
#include "Core/Asset/Addressables/AddressableRuntime.h"
#include "Core/Asset/Bundles/AssetBundleMountService.h"
#include "Core/Asset/HotReload/AssetHotReloadService.h"
#include "Core/Log.h"
#include "Core/Network/Diagnostics/NetworkDiagnosticsState.h"
#include "Core/Network/MultiplayerProductLayer.h"
#include "Core/Renderer/Diagnostics/GPUFrameTraceService.h"
#include "Core/Renderer/Upscaling/FrameGenerationController.h"
#include "Core/Renderer/Upscaling/TemporalUpscalerManager.h"
#include "Core/Renderer/VirtualGeometry/VirtualGeometryStreamingService.h"
#include "Core/Window.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <algorithm>
#include <deque>

namespace Core {
namespace UI {

ImGuiSubsystem::~ImGuiSubsystem() {
    if (m_Initialized) {
        Shutdown();
    }
}

bool ImGuiSubsystem::Initialize(RHI::VulkanContext* vulkanContext, Window* window) {
    if (m_Initialized) {
        ENGINE_CORE_WARN("ImGuiSubsystem::Initialize called but already initialized");
        return true;
    }

    if (!vulkanContext || !window) {
        ENGINE_CORE_ERROR("ImGuiSubsystem::Initialize failed: null context or window");
        return false;
    }

    m_VulkanContext = vulkanContext;
    m_Window = window;

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    
    // Enable docking and multi-viewport (experimental)
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Note: Multi-viewport requires additional Vulkan setup, disabled for now
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    
    // Setup style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.Alpha = m_Config.overlayAlpha;

    // Create Vulkan resources
    if (!CreateVulkanResources()) {
        ENGINE_CORE_ERROR("ImGuiSubsystem: Failed to create Vulkan resources");
        ImGui::DestroyContext();
        return false;
    }

    // Initialize Platform/Renderer backends
    if (!ImGui_ImplSDL3_InitForVulkan(window->GetNativeWindow())) {
        ENGINE_CORE_ERROR("ImGuiSubsystem: Failed to initialize SDL3 backend");
        DestroyVulkanResources();
        ImGui::DestroyContext();
        return false;
    }

    // Initialize Vulkan backend with new API (ImGui 1.92+)
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = vulkanContext->GetInstance();
    initInfo.PhysicalDevice = vulkanContext->GetPhysicalDevice();
    initInfo.Device = vulkanContext->GetDevice();
    initInfo.QueueFamily = vulkanContext->GetQueueFamilyIndices().graphicsFamily.value();
    initInfo.Queue = vulkanContext->GetGraphicsQueue();
    initInfo.DescriptorPool = m_DescriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = static_cast<uint32_t>(vulkanContext->GetSwapchainImages().size());
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = [](VkResult result) {
        if (result != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ImGui Vulkan error: {}", static_cast<int>(result));
        }
    };

    // New API: Pipeline info is in a separate structure
    initInfo.PipelineInfoMain.RenderPass = m_RenderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        ENGINE_CORE_ERROR("ImGuiSubsystem: Failed to initialize Vulkan backend");
        ImGui_ImplSDL3_Shutdown();
        DestroyVulkanResources();
        ImGui::DestroyContext();
        return false;
    }

    m_LastFrameTime = std::chrono::high_resolution_clock::now();
    m_Initialized = true;
    ENGINE_CORE_INFO("ImGuiSubsystem initialized successfully");
    return true;
}

void ImGuiSubsystem::Shutdown() {
    if (!m_Initialized) {
        return;
    }

    // Wait for device to be idle before cleanup
    if (m_VulkanContext) {
        vkDeviceWaitIdle(m_VulkanContext->GetDevice());
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    DestroyVulkanResources();
    ImGui::DestroyContext();

    m_Initialized = false;
    m_VulkanContext = nullptr;
    m_Window = nullptr;
    ENGINE_CORE_INFO("ImGuiSubsystem shut down");
}

bool ImGuiSubsystem::CreateVulkanResources() {
    VkDevice device = m_VulkanContext->GetDevice();

    // Create descriptor pool for ImGui
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        ENGINE_CORE_ERROR("Failed to create ImGui descriptor pool");
        return false;
    }

    // Create render pass for ImGui (renders to swapchain directly)
    VkAttachmentDescription attachment = {};
    attachment.format = m_VulkanContext->GetSwapchainImageFormat();
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Preserve existing content
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
        ENGINE_CORE_ERROR("Failed to create ImGui render pass");
        vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    // Create framebuffers for each swapchain image
    const auto& imageViews = m_VulkanContext->GetSwapchainImageViews();
    VkExtent2D extent = m_VulkanContext->GetSwapchainExtent();
    m_Framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &imageViews[i];
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_Framebuffers[i]) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to create ImGui framebuffer {}", i);
            // Cleanup already created framebuffers
            for (size_t j = 0; j < i; j++) {
                vkDestroyFramebuffer(device, m_Framebuffers[j], nullptr);
            }
            m_Framebuffers.clear();
            vkDestroyRenderPass(device, m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
            vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
            return false;
        }
    }

    return true;
}

void ImGuiSubsystem::DestroyVulkanResources() {
    if (!m_VulkanContext) {
        return;
    }

    VkDevice device = m_VulkanContext->GetDevice();

    for (auto framebuffer : m_Framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
    }
    m_Framebuffers.clear();

    if (m_RenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_RenderPass, nullptr);
        m_RenderPass = VK_NULL_HANDLE;
    }

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

bool ImGuiSubsystem::ProcessEvent(const SDL_Event& event) {
    if (!m_Initialized) {
        return false;
    }
    return ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImGuiSubsystem::BeginFrame() {
    if (!m_Initialized) {
        return;
    }

    // Calculate delta time
    auto now = std::chrono::high_resolution_clock::now();
    m_DeltaTime = std::chrono::duration<float>(now - m_LastFrameTime).count();
    m_LastFrameTime = now;

    // Start the ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiSubsystem::UpdateStats(const PerformanceStats& stats) {
    m_Stats = stats;
}

void ImGuiSubsystem::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    if (!m_Initialized) {
        return;
    }

    // Render debug overlays based on config
    if (m_Config.showPerformance) {
        RenderPerformanceOverlay();
    }
    if (m_Config.showMemory) {
        RenderMemoryPanel();
    }
    if (m_Config.showEntityInspector) {
        RenderEntityInspector();
    }
    if (m_Config.showRenderStats) {
        RenderRenderStats();
    }
    if (m_Config.showContentDelivery) {
        RenderContentDeliveryPanel();
    }
    if (m_Config.showDemoWindow) {
        ImGui::ShowDemoWindow(&m_Config.showDemoWindow);
    }

    // Call custom debug callback if set
    if (m_DebugDrawCallback) {
        m_DebugDrawCallback();
    }

    // Finalize ImGui rendering
    ImGui::Render();

    if (imageIndex >= m_Framebuffers.size()) {
        ENGINE_CORE_WARN("ImGuiSubsystem: Invalid framebuffer index {}, recreating framebuffers", imageIndex);
        OnSwapchainRecreated();
        if (imageIndex >= m_Framebuffers.size()) {
            return;
        }
    }

    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_Framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_VulkanContext->GetSwapchainExtent();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Record ImGui draw commands
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRenderPass(commandBuffer);
}

void ImGuiSubsystem::EndFrame() {
    // Handled in Render() with ImGui::Render()
}

void ImGuiSubsystem::OnSwapchainRecreated() {
    if (!m_Initialized || !m_VulkanContext || m_RenderPass == VK_NULL_HANDLE) {
        return;
    }

    VkDevice device = m_VulkanContext->GetDevice();

    for (auto framebuffer : m_Framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
    }
    m_Framebuffers.clear();

    const auto& imageViews = m_VulkanContext->GetSwapchainImageViews();
    VkExtent2D extent = m_VulkanContext->GetSwapchainExtent();
    m_Framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); ++i) {
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &imageViews[i];
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_Framebuffers[i]) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("ImGuiSubsystem: Failed to recreate framebuffer {}", i);
            m_Framebuffers.resize(i);
            break;
        }
    }

    if (!imageViews.empty()) {
        ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(imageViews.size()));
    }
}

bool ImGuiSubsystem::WantsKeyboardInput() const {
    if (!m_Initialized) {
        return false;
    }
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiSubsystem::WantMouseInput() const {
    if (!m_Initialized) {
        return false;
    }
    return ImGui::GetIO().WantCaptureMouse;
}

void ImGuiSubsystem::RenderPerformanceOverlay() {
    ImGuiWindowFlags windowFlags = 
        ImGuiWindowFlags_NoDecoration | 
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | 
        ImGuiWindowFlags_NoFocusOnAppearing | 
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove;

    const float padding = 10.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 workPos = viewport->WorkPos;
    ImVec2 windowPos(workPos.x + padding, workPos.y + padding);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.75f);

    if (ImGui::Begin("Performance", nullptr, windowFlags)) {
        ImGui::Text("FPS: %.1f (%.2f ms)", m_Stats.fps, m_Stats.frameTimeMs);
        ImGui::Separator();
        ImGui::Text("CPU: %.2f ms", m_Stats.cpuTimeMs);
        ImGui::Text("GPU: %.2f ms", m_Stats.gpuTimeMs);
        
        // FPS color indicator
        ImVec4 fpsColor;
        if (m_Stats.fps >= 60.0f) {
            fpsColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
        } else if (m_Stats.fps >= 30.0f) {
            fpsColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
        } else {
            fpsColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
        }
        ImGui::TextColored(fpsColor, "Status: %s", 
            m_Stats.fps >= 60.0f ? "Excellent" : 
            m_Stats.fps >= 30.0f ? "Good" : "Poor");
    }
    ImGui::End();
}

void ImGuiSubsystem::RenderMemoryPanel() {
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("Memory", &m_Config.showMemory, windowFlags)) {
        ImGui::Text("Total: %zu MB", m_Stats.totalMemoryMB);
        ImGui::Text("Used: %zu MB", m_Stats.usedMemoryMB);
        
        float usagePercent = m_Stats.totalMemoryMB > 0 
            ? static_cast<float>(m_Stats.usedMemoryMB) / static_cast<float>(m_Stats.totalMemoryMB)
            : 0.0f;
        
        ImGui::ProgressBar(usagePercent, ImVec2(-1, 0), "");
        ImGui::Text("Usage: %.1f%%", usagePercent * 100.0f);
    }
    ImGui::End();
}

void ImGuiSubsystem::RenderEntityInspector() {
    if (ImGui::Begin("Entity Inspector", &m_Config.showEntityInspector)) {
        ImGui::Text("Total Entities: %u", m_Stats.entityCount);
        ImGui::Separator();
        
        // Placeholder for entity selection and component display
        ImGui::TextWrapped("Select an entity in the scene to inspect its components.");
        
        // TODO: Integrate with ECS to show entity list and component editors
        if (ImGui::CollapsingHeader("Entities")) {
            ImGui::Text("Entity inspection coming soon...");
        }
    }
    ImGui::End();
}

void ImGuiSubsystem::RenderRenderStats() {
    ImGuiWindowFlags windowFlags = 
        ImGuiWindowFlags_NoDecoration | 
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | 
        ImGuiWindowFlags_NoFocusOnAppearing | 
        ImGuiWindowFlags_NoNav;

    const float padding = 10.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 workPos = viewport->WorkPos;
    ImVec2 workSize = viewport->WorkSize;
    ImVec2 windowPos(workPos.x + workSize.x - padding, workPos.y + padding);
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.75f);

    if (ImGui::Begin("Render Stats", nullptr, windowFlags)) {
        ImGui::Text("Draw Calls: %u", m_Stats.drawCalls);
        ImGui::Text("Triangles: %u", m_Stats.triangleCount);
    }
    ImGui::End();
}

void ImGuiSubsystem::RenderContentDeliveryPanel() {
    if (ImGui::Begin("Content Delivery", &m_Config.showContentDelivery)) {
        const auto runtimeDiagnostics =
            Asset::Addressables::AddressableRuntimeService::Get().GetDiagnostics();
        const auto mountedEntries =
            Asset::Bundles::AssetBundleMountService::Get().SnapshotMountTable();
        const auto hotReloadEvents =
            Asset::HotReload::AssetHotReloadService::Get().GetRecentEvents();

        ImGui::Text("Addressable Runtime");
        ImGui::Separator();
        ImGui::Text("Load Requests: %llu", static_cast<unsigned long long>(runtimeDiagnostics.TotalLoadRequests));
        ImGui::Text("Cache Hits: %llu", static_cast<unsigned long long>(runtimeDiagnostics.CacheHits));
        ImGui::Text("Shared Tickets: %llu", static_cast<unsigned long long>(runtimeDiagnostics.SharedInFlightTickets));
        ImGui::Text("Failed Loads: %llu", static_cast<unsigned long long>(runtimeDiagnostics.FailedLoadRequests));

        ImGui::Spacing();
        ImGui::Text("Mounted Bundle Overrides: %zu", mountedEntries.size());
        if (ImGui::CollapsingHeader("Mount Table", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& entry : mountedEntries) {
                ImGui::BulletText("%s -> %s (bundle=%s, priority=%d)",
                                  entry.AddressKey.c_str(),
                                  entry.ResolvedCookedPath.c_str(),
                                  entry.BundleId.c_str(),
                                  entry.MountPriority);
            }
        }

        ImGui::Spacing();
        ImGui::Text("Hot Reload Events: %zu", hotReloadEvents.size());
        if (ImGui::CollapsingHeader("Recent Events", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& eventRecord : hotReloadEvents) {
                ImGui::BulletText("Event %llu @ frame %llu: %s%s",
                                  static_cast<unsigned long long>(eventRecord.EventId),
                                  static_cast<unsigned long long>(eventRecord.AppliedFrameIndex),
                                  eventRecord.Success ? "Success" : "Failed",
                                  eventRecord.RolledBack ? " (Rolled Back)" : "");
                if (!eventRecord.Diagnostics.empty()) {
                    ImGui::TextWrapped("    %s", eventRecord.Diagnostics.c_str());
                }
                if (!eventRecord.FailedAddressKeys.empty()) {
                    ImGui::Text("    Failed keys: %zu", eventRecord.FailedAddressKeys.size());
                }
            }
        }

        const Network::NetworkDiagnosticsSnapshot networkDiagnostics =
            Network::NetworkDiagnosticsState::Get().GetSnapshot();
        ImGui::Spacing();
        ImGui::Text("Network Contract Compatibility");
        ImGui::Separator();
        ImGui::Text("Contract Hash: %llu",
                    static_cast<unsigned long long>(networkDiagnostics.ContractHash));
        ImGui::Text("Replication Policies: %u", networkDiagnostics.RegisteredReplicationPolicies);
        ImGui::Text("RPC Contracts: %u", networkDiagnostics.RegisteredRPCContracts);
        ImGui::Text("Mismatch Alarms: %u", networkDiagnostics.ContractHashMismatchCount);
        if (networkDiagnostics.ContractHashMismatchCount > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                               "Contract compatibility alarms detected");
        }

        ImGui::Spacing();
        ImGui::Text("Replay / Rollback / Migration");
        ImGui::Separator();
        ImGui::Text("Replay Tick: %u", networkDiagnostics.ReplayCurrentTick);
        ImGui::Text("Replay Mode: %s", networkDiagnostics.ReplayPlaybackMode.c_str());
        ImGui::Text("Replay Seek Drift: %.2f ticks", networkDiagnostics.ReplaySeekDriftTicks);
        ImGui::Text("Replay Decode Time: %llu us",
                    static_cast<unsigned long long>(networkDiagnostics.ReplayLastDecodeMicroseconds));
        ImGui::Text("Rollback Tick: %u", networkDiagnostics.LastRollbackTick);
        ImGui::Text("Snapshot Ring Usage: %u", networkDiagnostics.RollbackSnapshotRingUsage);
        ImGui::Text("Pending / Last Resim Frames: %u / %u",
                    networkDiagnostics.PendingResimFrames,
                    networkDiagnostics.LastResimulatedFrames);
        ImGui::Text("Migration State: %s (epoch %llu)",
                    networkDiagnostics.MigrationState.c_str(),
                    static_cast<unsigned long long>(networkDiagnostics.MigrationEpoch));
        ImGui::Text("Replay Starts / Completed / Failed: %llu / %llu / %llu",
                    static_cast<unsigned long long>(networkDiagnostics.ReplayRecordingsStarted),
                    static_cast<unsigned long long>(networkDiagnostics.ReplayRecordingsCompleted),
                    static_cast<unsigned long long>(networkDiagnostics.ReplayRecordingFailures));
        ImGui::Text("Playback Starts / Failed: %llu / %llu",
                    static_cast<unsigned long long>(networkDiagnostics.ReplayPlaybacksStarted),
                    static_cast<unsigned long long>(networkDiagnostics.ReplayPlaybackFailures));
        ImGui::Text("Rollbacks Applied / Fallbacks: %llu / %llu",
                    static_cast<unsigned long long>(networkDiagnostics.RollbacksApplied),
                    static_cast<unsigned long long>(networkDiagnostics.RollbackFallbacks));
        ImGui::Text("Resimulations / Hard Corrections: %llu / %llu",
                    static_cast<unsigned long long>(networkDiagnostics.ResimulationsExecuted),
                    static_cast<unsigned long long>(networkDiagnostics.ResimulationHardCorrections));
        ImGui::Text("Migrations Started / Completed / Failed: %llu / %llu / %llu",
                    static_cast<unsigned long long>(networkDiagnostics.HostMigrationsStarted),
                    static_cast<unsigned long long>(networkDiagnostics.HostMigrationsCompleted),
                    static_cast<unsigned long long>(networkDiagnostics.HostMigrationsFailed));

        Network::MultiplayerRuntimeFeatureGates featureGates =
            Network::GetMultiplayerRuntimeFeatureGates();
        bool updatedFeatureGates = false;
        updatedFeatureGates |= ImGui::Checkbox("Replay Enabled", &featureGates.ReplayEnabled);
        updatedFeatureGates |= ImGui::Checkbox("Rollback Enabled", &featureGates.RollbackEnabled);
        updatedFeatureGates |= ImGui::Checkbox("Resimulation Enabled", &featureGates.ResimulationEnabled);
        updatedFeatureGates |= ImGui::Checkbox("Host Migration Enabled", &featureGates.HostMigrationEnabled);
        if (updatedFeatureGates) {
            Network::SetMultiplayerRuntimeFeatureGates(featureGates);
        }

        RenderVirtualGeometryResidencySection();
        if (m_Config.showUpscalingDiagnostics) {
            RenderUpscalingDiagnosticsSection();
        }
    }
    ImGui::End();
}

void ImGuiSubsystem::RenderVirtualGeometryResidencySection() {
    ImGui::Spacing();
    ImGui::Text("Virtual Geometry Residency");
    ImGui::Separator();

    const Renderer::VirtualGeometryStreamingDiagnostics virtualGeometryDiagnostics =
        Renderer::GetVirtualGeometryStreamingDiagnostics();

    ImGui::Text("Resident Pages: %u", virtualGeometryDiagnostics.ResidentPageCount);
    ImGui::Text("Pending Requests: %u", virtualGeometryDiagnostics.PendingRequestCount);
    ImGui::Text("Last Requested / Loaded: %u / %u",
                virtualGeometryDiagnostics.LastRequestedCount,
                virtualGeometryDiagnostics.LastLoadedCount);
    ImGui::Text("Last Evicted / Failed: %u / %u",
                virtualGeometryDiagnostics.LastEvictedCount,
                virtualGeometryDiagnostics.LastFailedCount);

    const float queuePressureDisplay = std::clamp(virtualGeometryDiagnostics.QueuePressure, 0.0f, 1.0f);
    const float budgetSaturationDisplay = std::clamp(virtualGeometryDiagnostics.BudgetSaturation, 0.0f, 1.0f);

    ImGui::Text("Queue Pressure");
    ImGui::ProgressBar(queuePressureDisplay, ImVec2(-1.0f, 0.0f));
    ImGui::Text("Budget Saturation");
    ImGui::ProgressBar(budgetSaturationDisplay, ImVec2(-1.0f, 0.0f));

    if (virtualGeometryDiagnostics.BudgetSaturated) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Budget saturation detected");
    } else {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Within stream budget");
    }
}

void ImGuiSubsystem::RenderUpscalingDiagnosticsSection() {
    const Renderer::TemporalUpscalerRuntimeState upscalerState =
        Renderer::GetTemporalUpscalerManager().GetState();
    const std::deque<Renderer::UpscalerPolicyTransitionEvent> transitionEvents =
        Renderer::GetTemporalUpscalerManager().GetRecentTransitionEvents();
    const Renderer::FrameGenerationResult frameGenerationState =
        Renderer::GetFrameGenerationController().GetLastResult();
    const std::deque<Renderer::GPUFrameTraceArtifact> traceArtifacts =
        Renderer::GetGPUFrameTraceService().GetRecentArtifacts();

    ImGui::Spacing();
    ImGui::Text("Upscaling + Frame Generation");
    ImGui::Separator();
    ImGui::Text("Upscaler Backend: %s", Renderer::ToString(upscalerState.ActiveBackend));
    ImGui::Text("Quality Preset: %s", Renderer::ToString(upscalerState.QualityPreset));
    ImGui::Text("History Reset Pending: %s", upscalerState.HistoryResetPending ? "yes" : "no");
    ImGui::Text("History Reset Serial: %llu", static_cast<unsigned long long>(upscalerState.HistoryResetSerial));

    ImGui::Spacing();
    ImGui::Text("Frame Generation");
    ImGui::Separator();
    ImGui::Text("Active: %s", frameGenerationState.Active ? "yes" : "no");
    ImGui::Text("Added Latency: %.2f ms", frameGenerationState.AddedLatencyMs);
    ImGui::Text("Effective Latency: %.2f ms", frameGenerationState.EffectiveLatencyMs);
    if (frameGenerationState.FallbackUsed) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Fallback: %s",
                           frameGenerationState.FallbackReason.c_str());
    }

    if (ImGui::CollapsingHeader("Policy Events", ImGuiTreeNodeFlags_DefaultOpen)) {
        const size_t eventStart =
            transitionEvents.size() > 8 ? transitionEvents.size() - 8 : 0;
        for (size_t index = eventStart; index < transitionEvents.size(); ++index) {
            const Renderer::UpscalerPolicyTransitionEvent& eventRecord = transitionEvents[index];
            ImGui::BulletText("#%llu frame %llu: %s -> %s",
                              static_cast<unsigned long long>(eventRecord.EventId),
                              static_cast<unsigned long long>(eventRecord.FrameIndex),
                              Renderer::ToString(eventRecord.PreviousBackend),
                              Renderer::ToString(eventRecord.NewBackend));
            if (!eventRecord.FallbackReason.empty()) {
                ImGui::TextWrapped("    fallback: %s", eventRecord.FallbackReason.c_str());
            }
            if (eventRecord.HistoryResetRequired) {
                ImGui::TextWrapped("    history reset required");
            }
        }
    }

    if (ImGui::CollapsingHeader("GPU Frame Trace", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Recent Captures: %zu", traceArtifacts.size());
        if (!traceArtifacts.empty()) {
            const Renderer::GPUFrameTraceArtifact& latest = traceArtifacts.back();
            ImGui::Text("Latest: %s", latest.TraceCaptureId.c_str());
            ImGui::Text("Passes: %zu, Resources: %zu", latest.Passes.size(), latest.Resources.size());
            ImGui::Text("Markers: %u", latest.MarkerCount);
            ImGui::TextWrapped("JSON: %s", latest.JsonArtifactPath.string().c_str());
            ImGui::TextWrapped("TEXT: %s", latest.TextArtifactPath.string().c_str());
        }
    }
}

} // namespace UI
} // namespace Core
