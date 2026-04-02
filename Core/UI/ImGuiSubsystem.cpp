#include "ImGuiSubsystem.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

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

void ImGuiSubsystem::Render(VkCommandBuffer commandBuffer) {
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
    if (m_Config.showDemoWindow) {
        ImGui::ShowDemoWindow(&m_Config.showDemoWindow);
    }

    // Call custom debug callback if set
    if (m_DebugDrawCallback) {
        m_DebugDrawCallback();
    }

    // Finalize ImGui rendering
    ImGui::Render();

    // Get current swapchain image index
    // Note: This assumes you're calling Render with the correct swapchain index
    // In a real implementation, you'd pass the image index or get it from VulkanContext
    uint32_t imageIndex = 0; // TODO: Get from VulkanContext during DrawFrame

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

} // namespace UI
} // namespace Core
