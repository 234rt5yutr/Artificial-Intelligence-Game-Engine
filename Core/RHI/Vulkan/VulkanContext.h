#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Core {
namespace RHI {

    class VulkanContext {
    public:
        VulkanContext();
        ~VulkanContext();

        void Init();
        void Shutdown();

        VkInstance GetInstance() const { return m_Instance; }

    private:
        void CreateInstance();
        void SetupDebugMessenger();
        bool CheckValidationLayerSupport();
        std::vector<const char*> GetRequiredExtensions();

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;

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
