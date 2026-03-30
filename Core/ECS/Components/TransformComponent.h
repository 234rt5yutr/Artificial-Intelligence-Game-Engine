#pragma once

#include "Core/Math/Math.h"

namespace Core {
namespace ECS {

    struct TransformComponent {
        Math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
        Math::Vec3 Rotation{ 0.0f, 0.0f, 0.0f };  // Euler angles in radians
        Math::Vec3 Scale{ 1.0f, 1.0f, 1.0f };

        // Cached world matrix (updated by transform system)
        Math::Mat4 WorldMatrix{ 1.0f };

        // Dirty flag for optimization
        bool IsDirty = true;

        TransformComponent() = default;
        TransformComponent(const Math::Vec3& position)
            : Position(position) {}
        TransformComponent(const Math::Vec3& position, const Math::Vec3& rotation, const Math::Vec3& scale)
            : Position(position), Rotation(rotation), Scale(scale) {}

        // Compute local transform matrix
        Math::Mat4 GetLocalMatrix() const
        {
            Math::Mat4 translation = glm::translate(Math::Mat4(1.0f), Position);
            Math::Mat4 rotation = glm::toMat4(Math::Quat(Rotation));
            Math::Mat4 scale = glm::scale(Math::Mat4(1.0f), Scale);
            return translation * rotation * scale;
        }

        // Direction vectors
        Math::Vec3 GetForward() const
        {
            return glm::normalize(Math::Vec3(WorldMatrix[2]));
        }

        Math::Vec3 GetRight() const
        {
            return glm::normalize(Math::Vec3(WorldMatrix[0]));
        }

        Math::Vec3 GetUp() const
        {
            return glm::normalize(Math::Vec3(WorldMatrix[1]));
        }

        // Mark transform as needing update
        void SetDirty() { IsDirty = true; }
    };

} // namespace ECS
} // namespace Core
