#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <optional>
#include <set>
#include <string>
#include <algorithm>

namespace Core {
    class Window;

namespace RHI {

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool IsComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
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
        QueueFamilyIndices GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }
        VmaAllocator GetAllocator() const { return m_Allocator; }
        VkSwapchainKHR GetSwapchain() const { return m_Swapchain; }
        const std::vector<VkImage>& GetSwapchainImages() const { return m_SwapchainImages; }
        VkFormat GetSwapchainImageFormat() const { return m_SwapchainImageFormat; }
        VkExtent2D GetSwapchainExtent() const { return m_SwapchainExtent; }
        const std::vector<VkImageView>& GetSwapchainImageViews() const { return m_SwapchainImageViews; }
        
        VkCommandPool GetCommandPool() const { return m_CommandPool; }
        VkCommandBuffer GetCommandBuffer() const { return m_CommandBuffer; }

        void RecreateSwapchain(uint32_t width, uint32_t height);

    private:
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
        void CleanupSwapchain();
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
        VmaAllocator m_Allocator = VK_NULL_HANDLE;
        QueueFamilyIndices m_QueueFamilyIndices;

        VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
        std::vector<VkImage> m_SwapchainImages;
        VkFormat m_SwapchainImageFormat;
        VkExtent2D m_SwapchainExtent;
        std::vector<VkImageView> m_SwapchainImageViews;

        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

        const std::vector<const char*> m_DeviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

#ifdef NDEBUG
        const bool m_EnableValidationLayers = false;
#else
        const bool m_EnableValidationLayers = true;
#endif

        const std::vector<const char*> m_ValidationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };
    };

} // namespace RHI
} // namespace Core
