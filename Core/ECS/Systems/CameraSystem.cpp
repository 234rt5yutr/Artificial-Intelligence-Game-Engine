#include "Core/ECS/Systems/CameraSystem.h"
#include "Core/Log.h"

namespace Core {
namespace ECS {

    void CameraSystem::Update(Scene& scene)
    {
        PROFILE_FUNCTION();

        auto& registry = scene.GetRegistry();

        // Update all cameras with transforms
        auto view = registry.view<TransformComponent, CameraComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& camera = view.get<CameraComponent>(entity);

            // Update aspect ratio if screen dimensions changed
            if (camera.AspectRatio != m_AspectRatio) {
                camera.SetAspectRatio(m_AspectRatio);
            }

            // Recalculate projection matrix if dirty
            if (camera.IsDirty) {
                camera.RecalculateProjection();
            }

            // Always update view matrix from transform
            camera.CalculateViewMatrixFromWorldMatrix(transform.WorldMatrix);

            // If this is the active camera, cache its matrices
            if (camera.IsActive) {
                m_ActiveCameraEntity = entity;
                m_CachedView = camera.ViewMatrix;
                m_CachedProjection = camera.ProjectionMatrix;
                m_CachedViewProjection = camera.ViewProjectionMatrix;
            }
        }

        // If no active camera found, try to find one and activate it
        if (m_ActiveCameraEntity == entt::null || !registry.valid(m_ActiveCameraEntity)) {
            for (auto entity : view) {
                auto& camera = view.get<CameraComponent>(entity);
                camera.IsActive = true;
                m_ActiveCameraEntity = entity;
                m_CachedView = camera.ViewMatrix;
                m_CachedProjection = camera.ProjectionMatrix;
                m_CachedViewProjection = camera.ViewProjectionMatrix;
                ENGINE_CORE_INFO("CameraSystem: Auto-activated camera entity {}", static_cast<uint32_t>(entity));
                break;
            }
        }
    }

    void CameraSystem::SetScreenDimensions(uint32_t width, uint32_t height)
    {
        if (width > 0 && height > 0) {
            m_ScreenWidth = width;
            m_ScreenHeight = height;
            m_AspectRatio = static_cast<float>(width) / static_cast<float>(height);
        }
    }

    void CameraSystem::SetActiveCamera(Scene& scene, entt::entity entity)
    {
        auto& registry = scene.GetRegistry();

        // Deactivate current camera
        if (m_ActiveCameraEntity != entt::null && registry.valid(m_ActiveCameraEntity)) {
            if (auto* camera = registry.try_get<CameraComponent>(m_ActiveCameraEntity)) {
                camera->IsActive = false;
            }
        }

        // Activate new camera
        if (entity != entt::null && registry.valid(entity)) {
            if (auto* camera = registry.try_get<CameraComponent>(entity)) {
                camera->IsActive = true;
                m_ActiveCameraEntity = entity;
                ENGINE_CORE_INFO("CameraSystem: Switched to camera entity {}", static_cast<uint32_t>(entity));
            }
            else {
                ENGINE_CORE_WARN("CameraSystem: Entity {} does not have CameraComponent", static_cast<uint32_t>(entity));
            }
        }
    }

    CameraComponent* CameraSystem::GetActiveCamera(Scene& scene)
    {
        if (m_ActiveCameraEntity != entt::null) {
            auto& registry = scene.GetRegistry();
            if (registry.valid(m_ActiveCameraEntity)) {
                return registry.try_get<CameraComponent>(m_ActiveCameraEntity);
            }
        }
        return nullptr;
    }

    const CameraComponent* CameraSystem::GetActiveCamera(const Scene& scene) const
    {
        if (m_ActiveCameraEntity != entt::null) {
            const auto& registry = scene.GetRegistry();
            if (registry.valid(m_ActiveCameraEntity)) {
                return registry.try_get<CameraComponent>(m_ActiveCameraEntity);
            }
        }
        return nullptr;
    }

} // namespace ECS
} // namespace Core
