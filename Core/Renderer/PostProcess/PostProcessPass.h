#pragma once

#include <vulkan/vulkan.h>

namespace Core {

namespace ECS {
    struct PostProcessSettings;
}

namespace Renderer {

    class FramebufferChain;

    class PostProcessPass {
    public:
        virtual ~PostProcessPass() = default;

        virtual void Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                               VkRenderPass renderPass, VkExtent2D extent) = 0;
        virtual void Execute(VkCommandBuffer cmd, FramebufferChain& chain,
                            const ECS::PostProcessSettings& settings) = 0;
        virtual void Resize(VkExtent2D newExtent) = 0;
        virtual void Cleanup() = 0;
        virtual bool IsEnabled(const ECS::PostProcessSettings& settings) const = 0;
        virtual const char* GetName() const = 0;
    };

} // namespace Renderer
} // namespace Core
