#pragma once

#include "Core/ECS/Scene.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Components/CameraComponent.h"
#include "Core/Profile.h"
#include "Core/Log.h"
#include <entt/entt.hpp>

namespace Core {
namespace ECS {

    class CameraSystem {
    public:
        CameraSystem() = default;
        ~CameraSystem() = default;

        // Update all camera matrices
        void Update(Scene& scene);

        // Set screen dimensions (for aspect ratio updates)
        void SetScreenDimensions(uint32_t width, uint32_t height);

        // Get the active camera entity (or entt::null if none)
        entt::entity GetActiveCameraEntity() const { return m_ActiveCameraEntity; }

        // Set active camera by entity
        void SetActiveCamera(Scene& scene, entt::entity entity);

        // Get active camera component (nullptr if none)
        CameraComponent* GetActiveCamera(Scene& scene);
        const CameraComponent* GetActiveCamera(const Scene& scene) const;

        // Get view-projection matrix of active camera
        const Math::Mat4& GetViewProjectionMatrix() const { return m_CachedViewProjection; }
        const Math::Mat4& GetViewMatrix() const { return m_CachedView; }
        const Math::Mat4& GetProjectionMatrix() const { return m_CachedProjection; }

        // Screen dimensions
        uint32_t GetScreenWidth() const { return m_ScreenWidth; }
        uint32_t GetScreenHeight() const { return m_ScreenHeight; }
        float GetAspectRatio() const { return m_AspectRatio; }

    private:
        entt::entity m_ActiveCameraEntity = entt::null;
        uint32_t m_ScreenWidth = 1920;
        uint32_t m_ScreenHeight = 1080;
        float m_AspectRatio = 16.0f / 9.0f;

        // Cached matrices from active camera
        Math::Mat4 m_CachedViewProjection{ 1.0f };
        Math::Mat4 m_CachedView{ 1.0f };
        Math::Mat4 m_CachedProjection{ 1.0f };
    };

} // namespace ECS
} // namespace Core
