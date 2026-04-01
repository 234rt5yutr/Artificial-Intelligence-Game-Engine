#pragma once

#include "Core/Renderer/PostProcess/PostProcessManager.h"
#include <entt/entt.hpp>
#include <vulkan/vulkan.h>

namespace Core {
namespace ECS {

    class PostProcessSystem {
    public:
        PostProcessSystem() = default;
        ~PostProcessSystem() = default;

        // Non-copyable
        PostProcessSystem(const PostProcessSystem&) = delete;
        PostProcessSystem& operator=(const PostProcessSystem&) = delete;

        void Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                       VkExtent2D extent);
        void Update(entt::registry& registry, float deltaTime);
        void Render(VkCommandBuffer cmd, entt::registry& registry,
                   VkImageView sceneColor, VkImageView sceneDepth);
        void Resize(VkExtent2D newExtent);
        void Cleanup();

        Renderer::PostProcessManager& GetManager() { return m_Manager; }
        const Renderer::PostProcessManager& GetManager() const { return m_Manager; }
        VkImageView GetFinalOutput() const { return m_Manager.GetFinalOutput(); }

        bool IsInitialized() const { return m_Manager.IsInitialized(); }

    private:
        void UpdateBlending(entt::registry& registry, float deltaTime);
        void InterpolateSettings(PostProcessSettings& src, const PostProcessSettings& dst, float t);

        Renderer::PostProcessManager m_Manager;
    };

} // namespace ECS
} // namespace Core
