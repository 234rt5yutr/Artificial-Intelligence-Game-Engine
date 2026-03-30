#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>

namespace Core {
namespace RHI {

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;

        bool IsComplete() const {
            return graphicsFamily.has_value();
        }
    };

    class VulkanContext {
    public:
        VulkanContext();
        ~VulkanContext();

        void Init();
        void Shutdown();

        VkInstance GetInstance() const { return m_Instance; }
        VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        VkDevice GetDevice() const { return m_Device; }
        VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
        QueueFamilyIndices GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }

    private:
        void CreateInstance();
        void SetupDebugMessenger();
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        bool IsDeviceSuitable(VkPhysicalDevice device);
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
        bool CheckValidationLayerSupport();
        std::vector<const char*> GetRequiredExtensions();

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        QueueFamilyIndices m_QueueFamilyIndices;

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
