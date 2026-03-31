#pragma once

#include "Core/Math/Math.h"

namespace Core {
namespace ECS {

    enum class ProjectionType : uint8_t {
        Perspective = 0,
        Orthographic = 1
    };

    struct CameraComponent {
        // Projection type
        ProjectionType Projection = ProjectionType::Perspective;

        // Perspective properties
        float FieldOfView = 60.0f;     // In degrees (vertical FOV)
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        float AspectRatio = 16.0f / 9.0f;

        // Orthographic properties
        float OrthoSize = 10.0f;       // Half-height of ortho view

        // Cached matrices (updated by CameraSystem)
        Math::Mat4 ProjectionMatrix{ 1.0f };
        Math::Mat4 ViewMatrix{ 1.0f };
        Math::Mat4 ViewProjectionMatrix{ 1.0f };

        // State flags
        bool IsActive = true;          // Is this the active camera?
        bool IsDirty = true;           // Needs projection matrix recalculation

        // Constructors
        CameraComponent() = default;

        CameraComponent(float fov, float nearPlane, float farPlane)
            : FieldOfView(fov)
            , NearPlane(nearPlane)
            , FarPlane(farPlane)
        {}

        // Factory methods
        static CameraComponent CreatePerspective(float fovDegrees, float nearPlane, float farPlane, float aspectRatio = 16.0f / 9.0f)
        {
            CameraComponent camera;
            camera.Projection = ProjectionType::Perspective;
            camera.FieldOfView = fovDegrees;
            camera.NearPlane = nearPlane;
            camera.FarPlane = farPlane;
            camera.AspectRatio = aspectRatio;
            camera.IsDirty = true;
            return camera;
        }

        static CameraComponent CreateOrthographic(float size, float nearPlane, float farPlane, float aspectRatio = 16.0f / 9.0f)
        {
            CameraComponent camera;
            camera.Projection = ProjectionType::Orthographic;
            camera.OrthoSize = size;
            camera.NearPlane = nearPlane;
            camera.FarPlane = farPlane;
            camera.AspectRatio = aspectRatio;
            camera.IsDirty = true;
            return camera;
        }

        // Set aspect ratio and mark dirty
        void SetAspectRatio(float aspect)
        {
            if (AspectRatio != aspect) {
                AspectRatio = aspect;
                IsDirty = true;
            }
        }

        // Set FOV (perspective only) and mark dirty
        void SetFieldOfView(float fovDegrees)
        {
            if (FieldOfView != fovDegrees) {
                FieldOfView = fovDegrees;
                IsDirty = true;
            }
        }

        // Set near/far planes and mark dirty
        void SetClipPlanes(float nearPlane, float farPlane)
        {
            if (NearPlane != nearPlane || FarPlane != farPlane) {
                NearPlane = nearPlane;
                FarPlane = farPlane;
                IsDirty = true;
            }
        }

        // Set ortho size (orthographic only) and mark dirty
        void SetOrthoSize(float size)
        {
            if (OrthoSize != size) {
                OrthoSize = size;
                IsDirty = true;
            }
        }

        // Recalculate projection matrix based on current settings
        void RecalculateProjection()
        {
            if (Projection == ProjectionType::Perspective) {
                ProjectionMatrix = glm::perspective(
                    glm::radians(FieldOfView),
                    AspectRatio,
                    NearPlane,
                    FarPlane
                );
            }
            else {
                float halfWidth = OrthoSize * AspectRatio;
                float halfHeight = OrthoSize;
                ProjectionMatrix = glm::ortho(
                    -halfWidth, halfWidth,
                    -halfHeight, halfHeight,
                    NearPlane,
                    FarPlane
                );
            }

            // Flip Y for Vulkan coordinate system (clip space Y points down)
            ProjectionMatrix[1][1] *= -1.0f;

            IsDirty = false;
        }

        // Calculate view matrix from world transform
        void CalculateViewMatrix(const Math::Vec3& position, const Math::Vec3& rotation)
        {
            // Build rotation from euler angles
            Math::Quat orientation = Math::Quat(rotation);

            // Get forward direction (camera looks down negative Z in view space)
            Math::Vec3 forward = glm::normalize(orientation * Math::Vec3(0.0f, 0.0f, -1.0f));
            Math::Vec3 up = glm::normalize(orientation * Math::Vec3(0.0f, 1.0f, 0.0f));

            ViewMatrix = glm::lookAt(position, position + forward, up);
            ViewProjectionMatrix = ProjectionMatrix * ViewMatrix;
        }

        // Calculate view matrix from transform matrix directly
        void CalculateViewMatrixFromWorldMatrix(const Math::Mat4& worldMatrix)
        {
            // Extract position from world matrix
            Math::Vec3 position = Math::Vec3(worldMatrix[3]);

            // Extract basis vectors (camera looks down -Z)
            Math::Vec3 forward = -glm::normalize(Math::Vec3(worldMatrix[2]));  // -Z axis
            Math::Vec3 up = glm::normalize(Math::Vec3(worldMatrix[1]));         // Y axis

            ViewMatrix = glm::lookAt(position, position + forward, up);
            ViewProjectionMatrix = ProjectionMatrix * ViewMatrix;
        }

        // Get frustum planes for culling (returns array of 6 planes in the order: left, right, bottom, top, near, far)
        void GetFrustumPlanes(Math::Vec4 planes[6]) const
        {
            const Math::Mat4& m = ViewProjectionMatrix;

            // Left plane
            planes[0] = Math::Vec4(
                m[0][3] + m[0][0],
                m[1][3] + m[1][0],
                m[2][3] + m[2][0],
                m[3][3] + m[3][0]
            );

            // Right plane
            planes[1] = Math::Vec4(
                m[0][3] - m[0][0],
                m[1][3] - m[1][0],
                m[2][3] - m[2][0],
                m[3][3] - m[3][0]
            );

            // Bottom plane
            planes[2] = Math::Vec4(
                m[0][3] + m[0][1],
                m[1][3] + m[1][1],
                m[2][3] + m[2][1],
                m[3][3] + m[3][1]
            );

            // Top plane
            planes[3] = Math::Vec4(
                m[0][3] - m[0][1],
                m[1][3] - m[1][1],
                m[2][3] - m[2][1],
                m[3][3] - m[3][1]
            );

            // Near plane
            planes[4] = Math::Vec4(
                m[0][3] + m[0][2],
                m[1][3] + m[1][2],
                m[2][3] + m[2][2],
                m[3][3] + m[3][2]
            );

            // Far plane
            planes[5] = Math::Vec4(
                m[0][3] - m[0][2],
                m[1][3] - m[1][2],
                m[2][3] - m[2][2],
                m[3][3] - m[3][2]
            );

            // Normalize planes
            for (int i = 0; i < 6; ++i) {
                float length = glm::length(Math::Vec3(planes[i]));
                planes[i] /= length;
            }
        }

        // Screen-to-world ray calculation (for picking, etc.)
        void ScreenPointToRay(float screenX, float screenY, float screenWidth, float screenHeight,
                              Math::Vec3& outOrigin, Math::Vec3& outDirection) const
        {
            // Convert screen coordinates to NDC (-1 to 1)
            float ndcX = (2.0f * screenX) / screenWidth - 1.0f;
            float ndcY = 1.0f - (2.0f * screenY) / screenHeight;  // Flip Y

            // Inverse view-projection
            Math::Mat4 inverseVP = glm::inverse(ViewProjectionMatrix);

            // Near and far points in clip space
            Math::Vec4 nearPoint = Math::Vec4(ndcX, ndcY, 0.0f, 1.0f);
            Math::Vec4 farPoint = Math::Vec4(ndcX, ndcY, 1.0f, 1.0f);

            // Transform to world space
            Math::Vec4 nearWorld = inverseVP * nearPoint;
            Math::Vec4 farWorld = inverseVP * farPoint;

            // Perspective divide
            nearWorld /= nearWorld.w;
            farWorld /= farWorld.w;

            outOrigin = Math::Vec3(nearWorld);
            outDirection = glm::normalize(Math::Vec3(farWorld) - Math::Vec3(nearWorld));
        }

        // Get camera position from view matrix
        Math::Vec3 GetPosition() const
        {
            Math::Mat4 inverseView = glm::inverse(ViewMatrix);
            return Math::Vec3(inverseView[3]);
        }

        // Get camera forward direction from view matrix
        Math::Vec3 GetForward() const
        {
            Math::Mat4 inverseView = glm::inverse(ViewMatrix);
            return -glm::normalize(Math::Vec3(inverseView[2]));
        }

        // Get camera right direction from view matrix
        Math::Vec3 GetRight() const
        {
            Math::Mat4 inverseView = glm::inverse(ViewMatrix);
            return glm::normalize(Math::Vec3(inverseView[0]));
        }

        // Get camera up direction from view matrix
        Math::Vec3 GetUp() const
        {
            Math::Mat4 inverseView = glm::inverse(ViewMatrix);
            return glm::normalize(Math::Vec3(inverseView[1]));
        }
    };

} // namespace ECS
} // namespace Core
