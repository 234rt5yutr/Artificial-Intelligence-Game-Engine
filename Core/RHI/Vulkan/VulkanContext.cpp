#define VMA_IMPLEMENTATION
#include "VulkanContext.h"
#include "Core/Log.h"
#include "Core/Assert.h"
#include "Core/Window.h"
#include "Core/UI/UIManager.h"
#include "Core/RHI/PipelineCacheManager.h"
#include "Core/RHI/ShaderCompiler.h"
#include "Core/RHI/Vulkan/VulkanBuffer.h"
#include "Core/ECS/Systems/RenderSystem.h"
#include "Core/Renderer/Mesh.h"
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

// SDL extensions support
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <chrono>
#include <fstream>
#include <iostream>

namespace Core {
namespace RHI {

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData) {
        
        (void)messageType;
        (void)pUserData;
        
        if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            ENGINE_CORE_WARN("Vulkan Validation: {0}", pCallbackData->pMessage);
        } else {
            ENGINE_CORE_TRACE("Vulkan Validation: {0}", pCallbackData->pMessage);
        }

        return VK_FALSE;
    }

    VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, debugMessenger, pAllocator);
        }
    }

    VulkanContext::VulkanContext(Core::Window* window) 
        : m_Window(window) {
    }

    VulkanContext::~VulkanContext() {
        Shutdown();
    }

    void VulkanContext::Init() {
        CreateInstance();
        SetupDebugMessenger();
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();
        CreatePipelineCache();
        CreateAllocator();
        CreateCommandPool();
        CreateCommandBuffer();
        CreateSyncObjects();
        if (m_Window) {
            CreateSwapchain(m_Window->GetWidth(), m_Window->GetHeight());
            CreateImageViews();
            CreateRenderPass();
            CreateGraphicsPipeline();
            CreateMeshPipeline();
            CreateDepthResources();
            CreateFramebuffers();
        }
    }

    void VulkanContext::CleanupSwapchain() {
        for (auto framebuffer : m_SwapchainFramebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
            }
        }
        m_SwapchainFramebuffers.clear();

        for (auto imageView : m_SwapchainImageViews) {
            vkDestroyImageView(m_Device, imageView, nullptr);
        }
        m_SwapchainImageViews.clear();

        DestroyDepthResources();

        if (m_Swapchain) {
            vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }
    }

    void VulkanContext::RecreateSwapchain(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0 || m_Device == VK_NULL_HANDLE) return;
        
        vkDeviceWaitIdle(m_Device);
        
        CleanupSwapchain();

        if (m_GraphicsPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
            m_GraphicsPipeline = VK_NULL_HANDLE;
        }
        if (m_MeshPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_Device, m_MeshPipeline, nullptr);
            m_MeshPipeline = VK_NULL_HANDLE;
        }
        if (m_PipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
            m_PipelineLayout = VK_NULL_HANDLE;
        }
        if (m_MeshPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_Device, m_MeshPipelineLayout, nullptr);
            m_MeshPipelineLayout = VK_NULL_HANDLE;
        }
        if (m_RenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
        }

        CreateSwapchain(width, height);
        CreateImageViews();
        CreateRenderPass();
        CreateGraphicsPipeline();
        CreateMeshPipeline();
        CreateDepthResources();
        CreateFramebuffers();

        UI::UIManager::Get().OnResize(width, height);
    }

    void VulkanContext::Shutdown() {
        if (m_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_Device);
            CleanupSwapchain();

            if (m_GraphicsPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
                m_GraphicsPipeline = VK_NULL_HANDLE;
            }
            // The mesh pipeline references the render pass, which is rebuilt
            // below, so it has to go with it.
            if (m_MeshPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(m_Device, m_MeshPipeline, nullptr);
                m_MeshPipeline = VK_NULL_HANDLE;
            }
            if (m_PipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
                m_PipelineLayout = VK_NULL_HANDLE;
            }
            if (m_MeshPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(m_Device, m_MeshPipelineLayout, nullptr);
                m_MeshPipelineLayout = VK_NULL_HANDLE;
            }
            if (m_RenderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
                m_RenderPass = VK_NULL_HANDLE;
            }
            if (m_PipelineCache != VK_NULL_HANDLE) {
                vkDestroyPipelineCache(m_Device, m_PipelineCache, nullptr);
                m_PipelineCache = VK_NULL_HANDLE;
            }

            if (m_RenderFinishedSemaphore) {
                vkDestroySemaphore(m_Device, m_RenderFinishedSemaphore, nullptr);
                m_RenderFinishedSemaphore = VK_NULL_HANDLE;
            }

            if (m_ImageAvailableSemaphore) {
                vkDestroySemaphore(m_Device, m_ImageAvailableSemaphore, nullptr);
                m_ImageAvailableSemaphore = VK_NULL_HANDLE;
            }

            if (m_InFlightFence) {
                vkDestroyFence(m_Device, m_InFlightFence, nullptr);
                m_InFlightFence = VK_NULL_HANDLE;
            }

            if (m_TransferCommandPool) {
                vkDestroyCommandPool(m_Device, m_TransferCommandPool, nullptr);
                m_TransferCommandPool = VK_NULL_HANDLE;
            }

            if (m_CommandPool) {
                vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
                m_CommandPool = VK_NULL_HANDLE;
            }

            if (m_Allocator) {
                vmaDestroyAllocator(m_Allocator);
                m_Allocator = VK_NULL_HANDLE;
            }

            vkDestroyDevice(m_Device, nullptr);
            m_Device = VK_NULL_HANDLE;
        }

        if (m_EnableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
        }

        if (m_Surface) {
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        }

        if (m_Instance) {
            vkDestroyInstance(m_Instance, nullptr);
            m_Instance = VK_NULL_HANDLE;
        }
    }

    void VulkanContext::CreateSurface() {
        if (!m_Window) return;

        if (!SDL_Vulkan_CreateSurface(m_Window->GetNativeWindow(), m_Instance, nullptr, &m_Surface)) {
            ENGINE_CORE_ASSERT(false, "Failed to create Vulkan surface! Error: {0}", SDL_GetError());
        }
    }

    void VulkanContext::CreateInstance() {
        if (m_EnableValidationLayers && !CheckValidationLayerSupport()) {
            ENGINE_CORE_ASSERT(false, "Vulkan validation layers requested, but not available!");
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "AIGameEngine";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "AIGameEngine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        auto extensions = GetRequiredExtensions();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (m_EnableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(m_ValidationLayers.size());
            createInfo.ppEnabledLayerNames = m_ValidationLayers.data();

            debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugCreateInfo.pfnUserCallback = DebugCallback;
            createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
        } else {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
        ENGINE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan instance!");
        if (result == VK_SUCCESS) {
            ENGINE_CORE_INFO("Vulkan instance created successfully.");
        }
    }

    void VulkanContext::SetupDebugMessenger() {
        if (!m_EnableValidationLayers) return;

        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = DebugCallback;

        VkResult result = CreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessenger);
        ENGINE_CORE_ASSERT(result == VK_SUCCESS, "Failed to set up Vulkan debug messenger!");
    }

    bool VulkanContext::CheckValidationLayerSupport() {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : m_ValidationLayers) {
            bool layerFound = false;

            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound) {
                return false;
            }
        }

        return true;
    }

    std::vector<const char*> VulkanContext::GetRequiredExtensions() {
        uint32_t extCount = 0;
        const char * const * extNames = SDL_Vulkan_GetInstanceExtensions(&extCount);
        
        std::vector<const char*> extensions(extNames, extNames + extCount);

        if (m_EnableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    void VulkanContext::PickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
        ENGINE_CORE_ASSERT(deviceCount > 0, "Failed to find GPUs with Vulkan support!");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

        // Prefer discrete GPUs
        for (const auto& device : devices) {
            VkPhysicalDeviceProperties deviceProperties;
            vkGetPhysicalDeviceProperties(device, &deviceProperties);

            if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && IsDeviceSuitable(device)) {
                m_PhysicalDevice = device;
                ENGINE_CORE_INFO("Selected Discrete GPU: {0}", deviceProperties.deviceName);
                break;
            }
        }

        // Fallback to integrated GPUs or others if no discrete GPU was selected
        if (m_PhysicalDevice == VK_NULL_HANDLE) {
            for (const auto& device : devices) {
                if (IsDeviceSuitable(device)) {
                    VkPhysicalDeviceProperties deviceProperties;
                    vkGetPhysicalDeviceProperties(device, &deviceProperties);
                    m_PhysicalDevice = device;
                    ENGINE_CORE_INFO("Selected GPU: {0}", deviceProperties.deviceName);
                    break;
                }
            }
        }

        ENGINE_CORE_ASSERT(m_PhysicalDevice != VK_NULL_HANDLE, "Failed to find a suitable GPU!");
    }

    bool VulkanContext::IsDeviceSuitable(VkPhysicalDevice device) {
        QueueFamilyIndices indices = FindQueueFamilies(device);
        
        bool extensionsSupported = CheckDeviceExtensionSupport(device);
        bool swapChainAdequate = false;
        
        if (extensionsSupported && m_Surface) {
            SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }

        return indices.IsComplete() && extensionsSupported && (!m_Surface || swapChainAdequate);
    }

    bool VulkanContext::CheckDeviceExtensionSupport(VkPhysicalDevice device) const {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(m_DeviceExtensions.begin(), m_DeviceExtensions.end());

        for (const auto& extension : availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    QueueFamilyIndices VulkanContext::FindQueueFamilies(VkPhysicalDevice device) const {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }

            if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                // Prefer dedicated transfer queue
                indices.transferFamily = i;
            }

            if (m_Surface) {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
                if (presentSupport) {
                    indices.presentFamily = i;
                }
            } else {
                // If there's no surface (e.g. headless), we just say present family is the same as graphics to satisfy "completeness"
                // or we could adjust IsComplete. For now, pretend it's supported.
                indices.presentFamily = indices.graphicsFamily;
            }

            i++;
        }

        // If we didn't find a dedicated transfer queue, just use the graphics queue as it inherently supports transfer
        if (!indices.transferFamily.has_value() && indices.graphicsFamily.has_value()) {
            indices.transferFamily = indices.graphicsFamily;
        }

        return indices;
    }

    SwapChainSupportDetails VulkanContext::QuerySwapChainSupport(VkPhysicalDevice device) const {
        SwapChainSupportDetails details;

        if (!m_Surface) return details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    void VulkanContext::CreateLogicalDevice() {
        m_QueueFamilyIndices = FindQueueFamilies(m_PhysicalDevice);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies;
        if (m_QueueFamilyIndices.graphicsFamily.has_value()) {
            uniqueQueueFamilies.insert(m_QueueFamilyIndices.graphicsFamily.value());
        }
        if (m_QueueFamilyIndices.presentFamily.has_value()) {
            uniqueQueueFamilies.insert(m_QueueFamilyIndices.presentFamily.value());
        }
        if (m_QueueFamilyIndices.transferFamily.has_value()) {
            uniqueQueueFamilies.insert(m_QueueFamilyIndices.transferFamily.value());
        }

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

        createInfo.pEnabledFeatures = &deviceFeatures;

        createInfo.enabledExtensionCount = static_cast<uint32_t>(m_DeviceExtensions.size());
        createInfo.ppEnabledExtensionNames = m_DeviceExtensions.data();

        if (m_EnableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(m_ValidationLayers.size());
            createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        VkResult result = vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device);
        ENGINE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan logical device!");
        if (result == VK_SUCCESS) {
            ENGINE_CORE_INFO("Vulkan logical device created successfully.");
        }

        if (m_QueueFamilyIndices.graphicsFamily.has_value()) {
            vkGetDeviceQueue(m_Device, m_QueueFamilyIndices.graphicsFamily.value(), 0, &m_GraphicsQueue);
        }
        if (m_QueueFamilyIndices.presentFamily.has_value()) {
            vkGetDeviceQueue(m_Device, m_QueueFamilyIndices.presentFamily.value(), 0, &m_PresentQueue);
        }
        if (m_QueueFamilyIndices.transferFamily.has_value()) {
            vkGetDeviceQueue(m_Device, m_QueueFamilyIndices.transferFamily.value(), 0, &m_TransferQueue);
        }
    }

    void VulkanContext::CreateAllocator() {
        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.physicalDevice = m_PhysicalDevice;
        allocatorInfo.device = m_Device;
        allocatorInfo.instance = m_Instance;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;

        VkResult result = vmaCreateAllocator(&allocatorInfo, &m_Allocator);
        ENGINE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan Memory Allocator!");
        if (result == VK_SUCCESS) {
            ENGINE_CORE_INFO("Vulkan Memory Allocator created successfully.");
        }
    }

    void VulkanContext::CreateSwapchain(uint32_t width, uint32_t height) {
        SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(m_PhysicalDevice);

        VkSurfaceFormatKHR surfaceFormat = swapChainSupport.formats[0];
        for (const auto& availableFormat : swapChainSupport.formats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surfaceFormat = availableFormat;
                break;
            }
        }

        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // guaranteed to be available
        for (const auto& availablePresentMode : swapChainSupport.presentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = availablePresentMode;
                break;
            }
        }

        VkExtent2D extent;
        if (swapChainSupport.capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            extent = swapChainSupport.capabilities.currentExtent;
        } else {
            extent = { width, height };
            extent.width = std::max(swapChainSupport.capabilities.minImageExtent.width, std::min(swapChainSupport.capabilities.maxImageExtent.width, extent.width));
            extent.height = std::max(swapChainSupport.capabilities.minImageExtent.height, std::min(swapChainSupport.capabilities.maxImageExtent.height, extent.height));
        }

        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_Surface;

        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queueFamilyIndices[] = { m_QueueFamilyIndices.graphicsFamily.value(), m_QueueFamilyIndices.presentFamily.value() };

        if (m_QueueFamilyIndices.graphicsFamily != m_QueueFamilyIndices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }

        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_Swapchain) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "Failed to create swapchain!");
        }

        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, nullptr);
        m_SwapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, m_SwapchainImages.data());

        m_SwapchainImageFormat = surfaceFormat.format;
        m_SwapchainExtent = extent;
    }

    void VulkanContext::CreateImageViews() {
        m_SwapchainImageViews.resize(m_SwapchainImages.size());

        for (size_t i = 0; i < m_SwapchainImages.size(); i++) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_SwapchainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_SwapchainImageFormat;

            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(m_Device, &createInfo, nullptr, &m_SwapchainImageViews[i]) != VK_SUCCESS) {
                ENGINE_CORE_ASSERT(false, "Failed to create image views!");
            }
        }
    }

    void VulkanContext::CreateCommandPool() {
        QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_PhysicalDevice);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

        if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "Failed to create command pool!");
        }

        VkCommandPoolCreateInfo transferPoolInfo{};
        transferPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        transferPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        transferPoolInfo.queueFamilyIndex = queueFamilyIndices.transferFamily.value();
        if (vkCreateCommandPool(m_Device, &transferPoolInfo, nullptr, &m_TransferCommandPool) != VK_SUCCESS) {
            ENGINE_CORE_WARN("Failed to create transfer command pool!");
        }
    }

    void VulkanContext::CreateCommandBuffer() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_Device, &allocInfo, &m_CommandBuffer) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "Failed to allocate command buffer!");
        }
    }

    void VulkanContext::CreateSyncObjects() {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFence) != VK_SUCCESS) {
            
            ENGINE_CORE_ASSERT(false, "Failed to create synchronization objects for a frame!");
        }
    }

    VkShaderModule VulkanContext::CreateShaderModule(const std::vector<uint32_t>& code) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size() * sizeof(uint32_t);
        createInfo.pCode = code.data();

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_Device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "Failed to create shader module!");
            return VK_NULL_HANDLE;
        }

        return shaderModule;
    }

    void VulkanContext::DestroyShaderModule(VkShaderModule shaderModule) {
        if (shaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_Device, shaderModule, nullptr);
        }
    }

    bool VulkanContext::LoadPipelineCacheFromDisk(const std::filesystem::path& cacheBlobPath,
                                                  const PipelineCacheMetadata& runtimeMetadata,
                                                  std::string* reason) {
        if (m_Device == VK_NULL_HANDLE) {
            if (reason != nullptr) {
                *reason = "device is not initialized";
            }
            return false;
        }

        std::filesystem::path metadataPath = cacheBlobPath;
        metadataPath.replace_extension(".meta");
        const auto metadataResult = PipelineCacheManager::LoadMetadata(metadataPath);
        if (!metadataResult.Ok) {
            if (reason != nullptr) {
                *reason = metadataResult.Error;
            }
            CreatePipelineCache();
            return false;
        }

        std::string invalidationReason;
        if (PipelineCacheManager::ShouldInvalidate(metadataResult.Value, runtimeMetadata, &invalidationReason)) {
            if (reason != nullptr) {
                *reason = invalidationReason;
            }
            CreatePipelineCache();
            return false;
        }

        const auto blobResult = PipelineCacheManager::LoadCacheBlob(cacheBlobPath);
        if (!blobResult.Ok || blobResult.Value.empty()) {
            if (reason != nullptr) {
                *reason = blobResult.Ok ? "pipeline cache blob is empty" : blobResult.Error;
            }
            CreatePipelineCache();
            return false;
        }

        if (m_PipelineCache != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(m_Device, m_PipelineCache, nullptr);
            m_PipelineCache = VK_NULL_HANDLE;
        }

        VkPipelineCacheCreateInfo cacheCreateInfo{};
        cacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        cacheCreateInfo.initialDataSize = blobResult.Value.size();
        cacheCreateInfo.pInitialData = blobResult.Value.data();

        if (vkCreatePipelineCache(m_Device, &cacheCreateInfo, nullptr, &m_PipelineCache) != VK_SUCCESS) {
            if (reason != nullptr) {
                *reason = "vkCreatePipelineCache failed for persisted blob";
            }
            CreatePipelineCache();
            return false;
        }

        m_PipelineCacheMetadata = metadataResult.Value;
        return true;
    }

    bool VulkanContext::SavePipelineCacheToDisk(const std::filesystem::path& cacheBlobPath,
                                                const PipelineCacheMetadata& metadata,
                                                std::string* reason) const {
        if (m_Device == VK_NULL_HANDLE || m_PipelineCache == VK_NULL_HANDLE) {
            if (reason != nullptr) {
                *reason = "pipeline cache unavailable";
            }
            return false;
        }

        size_t cacheBlobSize = 0;
        if (vkGetPipelineCacheData(m_Device, m_PipelineCache, &cacheBlobSize, nullptr) != VK_SUCCESS || cacheBlobSize == 0u) {
            if (reason != nullptr) {
                *reason = "vkGetPipelineCacheData failed to report size";
            }
            return false;
        }

        std::vector<uint8_t> cacheBlob(cacheBlobSize);
        if (vkGetPipelineCacheData(m_Device, m_PipelineCache, &cacheBlobSize, cacheBlob.data()) != VK_SUCCESS) {
            if (reason != nullptr) {
                *reason = "vkGetPipelineCacheData failed to read data";
            }
            return false;
        }

        auto saveBlobResult = PipelineCacheManager::SaveCacheBlob(cacheBlobPath, cacheBlob);
        if (!saveBlobResult.Ok) {
            if (reason != nullptr) {
                *reason = saveBlobResult.Error;
            }
            return false;
        }

        PipelineCacheMetadata persistedMetadata = metadata;
        persistedMetadata.SchemaVersion = PipelineCacheManager::kSchemaVersion;
        persistedMetadata.CacheBlobSizeBytes = static_cast<uint64_t>(cacheBlobSize);
        persistedMetadata.LastUpdatedEpochSeconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        std::filesystem::path metadataPath = cacheBlobPath;
        metadataPath.replace_extension(".meta");
        auto saveMetadataResult = PipelineCacheManager::SaveMetadata(metadataPath, persistedMetadata);
        if (!saveMetadataResult.Ok) {
            if (reason != nullptr) {
                *reason = saveMetadataResult.Error;
            }
            return false;
        }

        return true;
    }

    void VulkanContext::RecordPipelineWarmupResult(bool cacheHit) {
        if (cacheHit) {
            ++m_PipelineWarmupHits;
            return;
        }
        ++m_PipelineWarmupMisses;
    }

    void VulkanContext::PushFrameMarker(const std::string& markerLabel) const {
        if (!m_FrameMarkerEnabled) {
            return;
        }
        ENGINE_CORE_TRACE("FrameMarker: {0}", markerLabel);
    }

    void VulkanContext::CreateRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_SwapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // Depth attachment: 3D geometry drawn without one resolves in submission
        // order rather than by distance, so back faces paint over front faces.
        m_DepthFormat = FindDepthFormat();

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = m_DepthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        const VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 2;
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "failed to create render pass!");
        }
    }

    void VulkanContext::CreatePipelineCache() {
        if (m_Device == VK_NULL_HANDLE) {
            return;
        }

        if (m_PipelineCache != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(m_Device, m_PipelineCache, nullptr);
            m_PipelineCache = VK_NULL_HANDLE;
        }

        VkPipelineCacheCreateInfo cacheCreateInfo{};
        cacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        cacheCreateInfo.initialDataSize = 0;
        cacheCreateInfo.pInitialData = nullptr;

        if (vkCreatePipelineCache(m_Device, &cacheCreateInfo, nullptr, &m_PipelineCache) != VK_SUCCESS) {
            ENGINE_CORE_WARN("Failed to create Vulkan pipeline cache. Falling back to null cache handle.");
            m_PipelineCache = VK_NULL_HANDLE;
        }
    }

    void VulkanContext::CreateGraphicsPipeline() {
        const std::string vertShaderSource = R"(
            #version 450
            layout(location = 0) out vec3 fragColor;
            vec2 positions[3] = vec2[](
                vec2(0.0, -0.5),
                vec2(0.5, 0.5),
                vec2(-0.5, 0.5)
            );
            vec3 colors[3] = vec3[](
                vec3(1.0, 0.0, 0.0),
                vec3(0.0, 1.0, 0.0),
                vec3(0.0, 0.0, 1.0)
            );
            void main() {
                gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
                fragColor = colors[gl_VertexIndex];
            }
        )";

        const std::string fragShaderSource = R"(
            #version 450
            layout(location = 0) in vec3 fragColor;
            layout(location = 0) out vec4 outColor;
            void main() {
                outColor = vec4(fragColor, 1.0);
            }
        )";

        std::vector<uint32_t> vertCode = Core::RHI::ShaderCompiler::CompileToSPIRV(vertShaderSource, Core::RHI::ShaderStage::Vertex, "vertex.glsl");
        std::vector<uint32_t> fragCode = Core::RHI::ShaderCompiler::CompileToSPIRV(fragShaderSource, Core::RHI::ShaderStage::Fragment, "fragment.glsl");

        VkShaderModule vertShaderModule = CreateShaderModule(vertCode);
        VkShaderModule fragShaderModule = CreateShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "failed to create pipeline layout!");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = m_RenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(m_Device, m_PipelineCache, 1, &pipelineInfo, nullptr, &m_GraphicsPipeline) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "failed to create graphics pipeline!");
        }
        RecordPipelineWarmupResult(m_PipelineCache != VK_NULL_HANDLE);

        DestroyShaderModule(fragShaderModule);
        DestroyShaderModule(vertShaderModule);
    }

    void VulkanContext::CreateFramebuffers() {
        m_SwapchainFramebuffers.resize(m_SwapchainImageViews.size());

        for (size_t i = 0; i < m_SwapchainImageViews.size(); i++) {
            VkImageView attachments[] = {
                m_SwapchainImageViews[i],
                m_DepthImageView
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_RenderPass;
            framebufferInfo.attachmentCount = 2;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = m_SwapchainExtent.width;
            framebufferInfo.height = m_SwapchainExtent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_SwapchainFramebuffers[i]) != VK_SUCCESS) {
                ENGINE_CORE_ASSERT(false, "failed to create framebuffer!");
            }
        }
    }

    void VulkanContext::DrawFrame() {
        vkWaitForFences(m_Device, 1, &m_InFlightFence, VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, m_ImageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            RecreateSwapchain(m_Window->GetWidth(), m_Window->GetHeight());
            return;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            ENGINE_CORE_ASSERT(false, "failed to acquire swap chain image!");
        }

        vkResetFences(m_Device, 1, &m_InFlightFence);
        vkResetCommandBuffer(m_CommandBuffer, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(m_CommandBuffer, &beginInfo) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "failed to begin recording command buffer!");
        }

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_RenderPass;
        if (imageIndex >= m_SwapchainFramebuffers.size()) {
            RecreateSwapchain(m_Window->GetWidth(), m_Window->GetHeight());
            return;
        }
        renderPassInfo.framebuffer = m_SwapchainFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_SwapchainExtent;

        VkClearValue clearValues[2]{};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = 2;
        renderPassInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(m_CommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float) m_SwapchainExtent.width;
        viewport.height = (float) m_SwapchainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(m_CommandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = m_SwapchainExtent;
        vkCmdSetScissor(m_CommandBuffer, 0, 1, &scissor);

        // Scene geometry when the application supplied a draw list this frame,
        // otherwise the original placeholder triangle so an empty scene still
        // shows the renderer is alive.
        RecordSceneDraws(m_CommandBuffer);
        if (m_LastDrawnMeshCount == 0) {
            vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);
            vkCmdDraw(m_CommandBuffer, 3, 1, 0, 0);
        }

        vkCmdEndRenderPass(m_CommandBuffer);

        // Render development/debug UI on top of the scene.
        UI::UIManager::Get().Render(m_CommandBuffer, imageIndex);

        if (vkEndCommandBuffer(m_CommandBuffer) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "failed to record command buffer!");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphore};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffer;

        VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphore};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFence) != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "failed to submit draw command buffer!");
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = {m_Swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            RecreateSwapchain(m_Window->GetWidth(), m_Window->GetHeight());
        } else if (result != VK_SUCCESS) {
            ENGINE_CORE_ASSERT(false, "failed to present swap chain image!");
        }
    }
    // ========================================================================
    // Depth resources
    // ========================================================================

    VkFormat VulkanContext::FindDepthFormat() const {
        // Prefer 32-bit float depth; fall back through formats that Vulkan
        // implementations are required to support for depth attachments.
        const VkFormat candidates[] = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        };

        for (VkFormat format : candidates) {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                return format;
            }
        }

        ENGINE_CORE_ERROR("No supported depth format found; falling back to D32_SFLOAT");
        return VK_FORMAT_D32_SFLOAT;
    }

    void VulkanContext::CreateDepthResources() {
        DestroyDepthResources();

        if (m_DepthFormat == VK_FORMAT_UNDEFINED) {
            m_DepthFormat = FindDepthFormat();
        }

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_SwapchainExtent.width;
        imageInfo.extent.height = m_SwapchainExtent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_DepthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(m_Allocator, &imageInfo, &allocInfo,
                           &m_DepthImage, &m_DepthAllocation, nullptr) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to create depth image");
            return;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_DepthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_DepthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_DepthImageView) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to create depth image view");
        }
    }

    void VulkanContext::DestroyDepthResources() {
        if (m_DepthImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, m_DepthImageView, nullptr);
            m_DepthImageView = VK_NULL_HANDLE;
        }
        if (m_DepthImage != VK_NULL_HANDLE) {
            vmaDestroyImage(m_Allocator, m_DepthImage, m_DepthAllocation);
            m_DepthImage = VK_NULL_HANDLE;
            m_DepthAllocation = VK_NULL_HANDLE;
        }
    }

    // ========================================================================
    // Mesh pipeline
    // ========================================================================

    void VulkanContext::CreateMeshPipeline() {
        // Unlit on purpose: lighting needs descriptor sets the RHI does not
        // route yet, and a correct unlit pass is more useful than a broken lit
        // one. Normals feed a hemisphere term so shape still reads.
        const std::string vertSource =
            "#version 450\n"
            "layout(location = 0) in vec3 inPosition;\n"
            "layout(location = 1) in vec3 inNormal;\n"
            "layout(location = 2) in vec2 inTexCoord;\n"
            "layout(location = 3) in vec4 inTangent;\n"
            "layout(push_constant) uniform Push { mat4 mvp; } push;\n"
            "layout(location = 0) out vec3 fragNormal;\n"
            "void main() {\n"
            "    gl_Position = push.mvp * vec4(inPosition, 1.0);\n"
            "    fragNormal = inNormal;\n"
            "}\n";

        const std::string fragSource =
            "#version 450\n"
            "layout(location = 0) in vec3 fragNormal;\n"
            "layout(location = 0) out vec4 outColor;\n"
            "void main() {\n"
            "    vec3 n = normalize(fragNormal);\n"
            "    float hemi = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);\n"
            "    vec3 base = mix(vec3(0.18, 0.20, 0.24), vec3(0.85, 0.87, 0.92), hemi);\n"
            "    outColor = vec4(base, 1.0);\n"
            "}\n";

        auto vertCode = ShaderCompiler::CompileToSPIRV(vertSource, ShaderStage::Vertex, "mesh.vert");
        auto fragCode = ShaderCompiler::CompileToSPIRV(fragSource, ShaderStage::Fragment, "mesh.frag");
        if (vertCode.empty() || fragCode.empty()) {
            ENGINE_CORE_ERROR("Mesh pipeline shader compilation failed; scene geometry will not render");
            return;
        }

        VkShaderModule vertModule = CreateShaderModule(vertCode);
        VkShaderModule fragModule = CreateShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        // Must match Renderer::Vertex exactly.
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Renderer::Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attributes[4]{};
        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Renderer::Vertex, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Renderer::Vertex, normal)};
        attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Renderer::Vertex, texCoord)};
        attributes[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Renderer::Vertex, tangent)};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 4;
        vertexInput.pVertexAttributeDescriptions = attributes;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1.0f;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;

        const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(float) * 16;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(m_Device, &layoutInfo, nullptr, &m_MeshPipelineLayout) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to create mesh pipeline layout");
            DestroyShaderModule(vertModule);
            DestroyShaderModule(fragModule);
            return;
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_MeshPipelineLayout;
        pipelineInfo.renderPass = m_RenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(m_Device, m_PipelineCache, 1, &pipelineInfo,
                                      nullptr, &m_MeshPipeline) != VK_SUCCESS) {
            ENGINE_CORE_ERROR("Failed to create mesh pipeline");
        } else {
            ENGINE_CORE_INFO("Mesh pipeline created (vertex input + MVP push constant + depth test)");
        }

        DestroyShaderModule(vertModule);
        DestroyShaderModule(fragModule);
    }

    // ========================================================================
    // Scene draw submission
    // ========================================================================

    void VulkanContext::SetSceneDrawData(const void* drawCommands, std::size_t drawCommandCount,
                                         const float* viewProjection) {
        m_SceneDrawCommands = drawCommands;
        m_SceneDrawCommandCount = drawCommandCount;
        if (viewProjection) {
            std::memcpy(m_SceneViewProjection, viewProjection, sizeof(m_SceneViewProjection));
        }
    }

    void VulkanContext::ClearSceneDrawData() {
        m_SceneDrawCommands = nullptr;
        m_SceneDrawCommandCount = 0;
    }

    void VulkanContext::RecordSceneDraws(VkCommandBuffer cmd) {
        m_LastDrawnMeshCount = 0;

        if (!m_SceneDrawCommands || m_SceneDrawCommandCount == 0 ||
            m_MeshPipeline == VK_NULL_HANDLE) {
            return;
        }

        const auto* commands = static_cast<const ECS::DrawCommand*>(m_SceneDrawCommands);
        const glm::mat4 viewProj = glm::make_mat4(m_SceneViewProjection);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_MeshPipeline);

        for (std::size_t i = 0; i < m_SceneDrawCommandCount; ++i) {
            const ECS::DrawCommand& command = commands[i];
            const Renderer::Mesh* mesh = command.Mesh;
            // Skip anything without GPU buffers rather than issuing a draw that
            // would read from a null binding.
            if (!mesh || !mesh->IsUploaded() || mesh->GetUploadedIndexCount() == 0) {
                continue;
            }

            const auto* vertexBuffer = static_cast<const VulkanBuffer*>(mesh->vertexBuffer.get());
            const auto* indexBuffer = static_cast<const VulkanBuffer*>(mesh->indexBuffer.get());
            if (!vertexBuffer || !indexBuffer) {
                continue;
            }

            const glm::mat4 mvp = viewProj * command.Transform;
            vkCmdPushConstants(cmd, m_MeshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(glm::mat4), &mvp[0][0]);

            VkBuffer vertexHandle = vertexBuffer->GetBuffer();
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexHandle, &offset);
            vkCmdBindIndexBuffer(cmd, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh->GetUploadedIndexCount(), 1, 0, 0, 0);

            ++m_LastDrawnMeshCount;
        }
    }

} // namespace RHI
} // namespace Core
