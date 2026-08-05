#pragma once

#include "Core/RHI/PipelineCacheManager.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <filesystem>
#include <memory>
#include <vector>
#include <optional>
#include <set>
#include <string>
#include <algorithm>

namespace Core {
    class Window;
namespace Renderer {
    class SceneRenderer;
    struct FrameRenderData;
}

namespace RHI {

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        std::optional<uint32_t> transferFamily;

        bool IsComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value() && transferFamily.has_value();
        }
    };

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    class VulkanContext {
    public:
        VulkanContext(Core::Window* window = nullptr);
        ~VulkanContext();

        void Init();
        void Shutdown();

        VkInstance GetInstance() const { return m_Instance; }
        VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        VkDevice GetDevice() const { return m_Device; }
        VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
        VkQueue GetPresentQueue() const { return m_PresentQueue; }
        VkQueue GetTransferQueue() const { return m_TransferQueue; }
        QueueFamilyIndices GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }
        VmaAllocator GetAllocator() const { return m_Allocator; }
        VkSwapchainKHR GetSwapchain() const { return m_Swapchain; }
        const std::vector<VkImage>& GetSwapchainImages() const { return m_SwapchainImages; }
        VkFormat GetSwapchainImageFormat() const { return m_SwapchainImageFormat; }
        VkExtent2D GetSwapchainExtent() const { return m_SwapchainExtent; }
        const std::vector<VkImageView>& GetSwapchainImageViews() const { return m_SwapchainImageViews; }
        VkRenderPass GetRenderPass() const { return m_RenderPass; }
        VkPipelineCache GetPipelineCache() const { return m_PipelineCache; }
        
        VkCommandPool GetCommandPool() const { return m_CommandPool; }
        VkCommandPool GetTransferCommandPool() const { return m_TransferCommandPool; }
        VkCommandBuffer GetCommandBuffer() const { return m_CommandBuffer; }

        VkSemaphore GetImageAvailableSemaphore() const { return m_ImageAvailableSemaphore; }
        VkSemaphore GetRenderFinishedSemaphore() const { return m_RenderFinishedSemaphore; }
        VkFence GetInFlightFence() const { return m_InFlightFence; }

        // Scene rendering ---------------------------------------------------
        // Hand the renderer this frame's simulation output. The data is copied,
        // so the caller may rebuild its lists immediately afterwards - which is
        // what lets the simulation run on a different thread from submission.
        void SubmitFrameRenderData(const Renderer::FrameRenderData& frame);
        void ClearSceneDrawData();
        uint32_t GetLastDrawnMeshCount() const { return m_LastDrawnMeshCount; }

        // The frame pipeline: cull -> G-buffer -> HZB -> GI -> resolve -> FSR.
        // Null until Init() runs with a window.
        Renderer::SceneRenderer* GetSceneRenderer() const { return m_SceneRenderer.get(); }

        VkFormat GetDepthFormat() const { return m_DepthFormat; }
        // False when the driver refused multiDrawIndirect/drawIndirectFirstInstance,
        // in which case GPU-driven cluster draws cannot be issued at all.
        bool SupportsGPUDrivenDraw() const { return m_SupportsGPUDrivenDraw; }

        VkShaderModule CreateShaderModule(const std::vector<uint32_t>& code);
        void DestroyShaderModule(VkShaderModule shaderModule);

        void SetPipelineCacheMetadata(const PipelineCacheMetadata& metadata) { m_PipelineCacheMetadata = metadata; }
        const PipelineCacheMetadata& GetPipelineCacheMetadata() const { return m_PipelineCacheMetadata; }
        bool LoadPipelineCacheFromDisk(const std::filesystem::path& cacheBlobPath, const PipelineCacheMetadata& runtimeMetadata, std::string* reason);
        bool SavePipelineCacheToDisk(const std::filesystem::path& cacheBlobPath, const PipelineCacheMetadata& metadata, std::string* reason) const;

        void RecordPipelineWarmupResult(bool cacheHit);
        uint64_t GetPipelineWarmupHitCount() const { return m_PipelineWarmupHits; }
        uint64_t GetPipelineWarmupMissCount() const { return m_PipelineWarmupMisses; }

        void SetFrameMarkerEnabled(bool enabled) { m_FrameMarkerEnabled = enabled; }
        bool IsFrameMarkerEnabled() const { return m_FrameMarkerEnabled; }
        void PushFrameMarker(const std::string& markerLabel) const;

        void RecreateSwapchain(uint32_t width, uint32_t height);
        void DrawFrame();

    private:
        void CreateRenderPass();
        void CreateGraphicsPipeline();
        void CreateFramebuffers();
        void CreatePipelineCache();

        void CreateInstance();
        void SetupDebugMessenger();
        void CreateSurface();
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateAllocator();
        void CreateSwapchain(uint32_t width, uint32_t height);
        void CreateImageViews();
        void CreateCommandPool();
        void CreateCommandBuffer();
        void CreateSyncObjects();
        void CleanupSwapchain();
        // Depth buffer: 3D geometry cannot be rendered correctly without one, and
        // the context had none. Recreated with the swapchain.
        void CreateDepthResources();
        void DestroyDepthResources();
        VkFormat FindDepthFormat() const;
        // Pipeline that accepts Renderer::Vertex input and an MVP push constant.
        // The original pipeline has no vertex input at all - it draws a hard-coded
        // triangle - so scene geometry needs its own.
        void CreateMeshPipeline();
        bool IsDeviceSuitable(VkPhysicalDevice device);
        bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
        SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
        bool CheckValidationLayerSupport();
        std::vector<const char*> GetRequiredExtensions();

    private:
        Core::Window* m_Window = nullptr;
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;
        VkQueue m_TransferQueue = VK_NULL_HANDLE;
        VmaAllocator m_Allocator = VK_NULL_HANDLE;
        QueueFamilyIndices m_QueueFamilyIndices;

        VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
        std::vector<VkImage> m_SwapchainImages;
        VkFormat m_SwapchainImageFormat;
        VkExtent2D m_SwapchainExtent;
        std::vector<VkImageView> m_SwapchainImageViews;
        std::vector<VkFramebuffer> m_SwapchainFramebuffers;

        VkImage m_DepthImage = VK_NULL_HANDLE;
        VmaAllocation m_DepthAllocation = VK_NULL_HANDLE;
        VkImageView m_DepthImageView = VK_NULL_HANDLE;
        VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;

        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_MeshPipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_MeshPipeline = VK_NULL_HANDLE;

        // Owns everything between "the command buffer has begun" and "the
        // swapchain pass is about to end": culling, the G-buffer, GI, upscaling.
        std::unique_ptr<Renderer::SceneRenderer> m_SceneRenderer;
        bool m_HasFrameData = false;
        bool m_SupportsGPUDrivenDraw = false;
        uint32_t m_LastDrawnMeshCount = 0;
        VkPipelineCache m_PipelineCache = VK_NULL_HANDLE;
        PipelineCacheMetadata m_PipelineCacheMetadata{};
        bool m_FrameMarkerEnabled = false;
        uint64_t m_PipelineWarmupHits = 0;
        uint64_t m_PipelineWarmupMisses = 0;

        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        VkCommandPool m_TransferCommandPool = VK_NULL_HANDLE;
        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

        VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore m_RenderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence m_InFlightFence = VK_NULL_HANDLE;

        const std::vector<const char*> m_DeviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        // On in debug builds, and forceable in release with
        // AIGE_VULKAN_VALIDATION=1 - a release build is the only place the
        // GPU-driven and GI passes run at full speed, so it has to be possible
        // to validate them there.
#ifdef NDEBUG
        bool m_EnableValidationLayers = false;
#else
        bool m_EnableValidationLayers = true;
#endif

        const std::vector<const char*> m_ValidationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };
    };

} // namespace RHI
} // namespace Core
