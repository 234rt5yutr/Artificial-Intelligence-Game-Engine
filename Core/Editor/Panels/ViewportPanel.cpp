#include "ViewportPanel.h"

#include "Core/ECS/Components/MeshComponent.h"
#include "Core/ECS/Components/NameComponent.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Systems/CameraSystem.h"
#include "Core/ECS/SystemPipeline.h"
#include "Core/Log.h"
#include "Core/RHI/Vulkan/VulkanContext.h"
#include "Core/Renderer/SceneRenderer.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Core {
namespace Editor {

    namespace {

        constexpr float kNearPlane = 0.1f;
        constexpr float kFarPlane = 2000.0f;
        constexpr float kFieldOfView = 1.0471976f; // 60 degrees

        ImVec2 ToImVec(const Math::Vec2& v) { return ImVec2(v.x, v.y); }

        // Ray/AABB slab test. Returns the near hit distance, or a negative value
        // when the ray misses.
        float RayAABB(const Math::Vec3& origin, const Math::Vec3& direction,
                      const Math::Vec3& boundsMin, const Math::Vec3& boundsMax) {
            float tMin = 0.0f;
            float tMax = std::numeric_limits<float>::max();
            for (int axis = 0; axis < 3; ++axis) {
                if (std::abs(direction[axis]) < 1e-8f) {
                    if (origin[axis] < boundsMin[axis] || origin[axis] > boundsMax[axis]) {
                        return -1.0f;
                    }
                    continue;
                }
                const float inverse = 1.0f / direction[axis];
                float t1 = (boundsMin[axis] - origin[axis]) * inverse;
                float t2 = (boundsMax[axis] - origin[axis]) * inverse;
                if (t1 > t2) {
                    std::swap(t1, t2);
                }
                tMin = std::max(tMin, t1);
                tMax = std::min(tMax, t2);
                if (tMin > tMax) {
                    return -1.0f;
                }
            }
            return tMin;
        }

    } // namespace

    void ViewportPanel::Initialize(RHI::VulkanContext* context) {
        m_Context = context;
    }

    void ViewportPanel::Shutdown() {
        if (m_ImGuiTexture != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(m_ImGuiTexture);
            m_ImGuiTexture = VK_NULL_HANDLE;
        }
        m_BoundView = VK_NULL_HANDLE;
        m_Context = nullptr;
    }

    void ViewportPanel::EnsureTextureBinding() {
        if (!m_Context) {
            return;
        }
        auto* sceneRenderer = m_Context->GetSceneRenderer();
        if (!sceneRenderer) {
            return;
        }

        VkImageView view = sceneRenderer->GetViewportImageView();
        VkSampler sampler = sceneRenderer->GetViewportSampler();
        if (view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE || view == m_BoundView) {
            return;
        }

        // A resize or an upscaler quality change replaces the image, so the old
        // descriptor points at freed memory and must go.
        if (m_ImGuiTexture != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(m_ImGuiTexture);
            m_ImGuiTexture = VK_NULL_HANDLE;
        }
        m_ImGuiTexture = ImGui_ImplVulkan_AddTexture(sampler, view,
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_BoundView = view;
    }

    Math::Mat4 ViewportPanel::BuildView() const {
        const Math::Vec3 forward(std::cos(m_CameraPitch) * std::cos(m_CameraYaw),
                                 std::sin(m_CameraPitch),
                                 std::cos(m_CameraPitch) * std::sin(m_CameraYaw));
        return glm::lookAt(m_CameraPosition, m_CameraPosition + forward, Math::Vec3(0.0f, 1.0f, 0.0f));
    }

    Math::Mat4 ViewportPanel::BuildProjection() const {
        Math::Mat4 projection = glm::perspective(kFieldOfView, m_Aspect, kNearPlane, kFarPlane);
        // Vulkan's clip space has +Y down relative to OpenGL's, which is what glm
        // produces by default.
        projection[1][1] *= -1.0f;
        return projection;
    }

    bool ViewportPanel::GetCameraOverride(Math::Mat4& view, Math::Mat4& projection) const {
        if (!m_State.UseEditorCamera) {
            return false;
        }
        view = BuildView();
        projection = BuildProjection();
        return true;
    }

    void ViewportPanel::PlaceCamera(const Math::Vec3& position, const Math::Vec3& target) {
        m_CameraPosition = position;

        const Math::Vec3 offset = target - position;
        const float horizontal = std::sqrt(offset.x * offset.x + offset.z * offset.z);
        if (horizontal < 1e-5f && std::abs(offset.y) < 1e-5f) {
            // Target equals position: keep the current orientation rather than
            // producing a NaN basis.
            return;
        }
        // Yaw is measured from +X in the XZ plane and pitch from that plane, the
        // same convention BuildView reconstructs the forward vector with.
        m_CameraYaw = std::atan2(offset.z, offset.x);
        m_CameraPitch = std::atan2(offset.y, std::max(horizontal, 1e-5f));
    }

    Math::Vec3 ViewportPanel::GetCameraForward() const {
        return Math::Vec3(std::cos(m_CameraPitch) * std::cos(m_CameraYaw),
                          std::sin(m_CameraPitch),
                          std::cos(m_CameraPitch) * std::sin(m_CameraYaw));
    }

    void ViewportPanel::UpdateEditorCamera(float deltaTime, bool viewportHovered) {
        if (!m_State.UseEditorCamera) {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        // Right mouse held is the standard "I am flying" contract; without it the
        // viewport keeps normal click-to-select behaviour.
        const bool flying = viewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (flying) {
            m_CameraYaw += io.MouseDelta.x * m_State.MouseSensitivity;
            m_CameraPitch -= io.MouseDelta.y * m_State.MouseSensitivity;
            m_CameraPitch = std::clamp(m_CameraPitch, -1.5533f, 1.5533f);
        }

        if (!flying) {
            return;
        }

        const Math::Vec3 forward(std::cos(m_CameraPitch) * std::cos(m_CameraYaw),
                                 std::sin(m_CameraPitch),
                                 std::cos(m_CameraPitch) * std::sin(m_CameraYaw));
        const Math::Vec3 right = glm::normalize(glm::cross(forward, Math::Vec3(0.0f, 1.0f, 0.0f)));

        float speed = m_State.CameraSpeed * deltaTime;
        if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
            speed *= 4.0f;
        }

        if (ImGui::IsKeyDown(ImGuiKey_W)) m_CameraPosition += forward * speed;
        if (ImGui::IsKeyDown(ImGuiKey_S)) m_CameraPosition -= forward * speed;
        if (ImGui::IsKeyDown(ImGuiKey_D)) m_CameraPosition += right * speed;
        if (ImGui::IsKeyDown(ImGuiKey_A)) m_CameraPosition -= right * speed;
        if (ImGui::IsKeyDown(ImGuiKey_E)) m_CameraPosition.y += speed;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) m_CameraPosition.y -= speed;
    }

    void ViewportPanel::FocusOnSelection(EditorContext& editorContext) {
        if (!editorContext.ActiveScene || editorContext.Selection.Primary == entt::null) {
            return;
        }
        auto& registry = editorContext.ActiveScene->GetRegistry();
        if (!registry.valid(editorContext.Selection.Primary) ||
            !registry.all_of<ECS::TransformComponent>(editorContext.Selection.Primary)) {
            return;
        }
        const auto& transform = registry.get<ECS::TransformComponent>(editorContext.Selection.Primary);
        const Math::Vec3 forward(std::cos(m_CameraPitch) * std::cos(m_CameraYaw),
                                 std::sin(m_CameraPitch),
                                 std::cos(m_CameraPitch) * std::sin(m_CameraYaw));
        m_CameraPosition = transform.Position - forward * 6.0f;
        m_State.UseEditorCamera = true;
    }

    void ViewportPanel::HandlePicking(EditorContext& editorContext, const Math::Vec2& imageMin,
                                      const Math::Vec2& imageSize) {
        if (!editorContext.ActiveScene || imageSize.x <= 0.0f || imageSize.y <= 0.0f) {
            return;
        }

        const ImVec2 mouse = ImGui::GetMousePos();
        const Math::Vec2 local(mouse.x - imageMin.x, mouse.y - imageMin.y);
        if (local.x < 0.0f || local.y < 0.0f || local.x > imageSize.x || local.y > imageSize.y) {
            return;
        }

        // Unproject the click through the same matrices the frame was rendered
        // with, so what you click is what you saw.
        Math::Mat4 view = BuildView();
        Math::Mat4 projection = BuildProjection();
        if (!m_State.UseEditorCamera) {
            if (auto* pipeline = editorContext.ActiveScene->GetSystemPipeline()) {
                if (const auto* cameraSystem = pipeline->GetCameraSystem()) {
                    view = cameraSystem->GetViewMatrix();
                    projection = cameraSystem->GetProjectionMatrix();
                }
            }
        }

        const Math::Mat4 inverseViewProjection = glm::inverse(projection * view);
        const float ndcX = (local.x / imageSize.x) * 2.0f - 1.0f;
        const float ndcY = (local.y / imageSize.y) * 2.0f - 1.0f;

        Math::Vec4 nearPoint = inverseViewProjection * Math::Vec4(ndcX, ndcY, 0.0f, 1.0f);
        Math::Vec4 farPoint = inverseViewProjection * Math::Vec4(ndcX, ndcY, 1.0f, 1.0f);
        if (std::abs(nearPoint.w) < 1e-6f || std::abs(farPoint.w) < 1e-6f) {
            return;
        }
        const Math::Vec3 rayOrigin = Math::Vec3(nearPoint) / nearPoint.w;
        const Math::Vec3 rayDirection = glm::normalize(Math::Vec3(farPoint) / farPoint.w - rayOrigin);

        auto& registry = editorContext.ActiveScene->GetRegistry();
        entt::entity best = entt::null;
        float bestDistance = std::numeric_limits<float>::max();

        auto view3d = registry.view<ECS::TransformComponent, ECS::MeshComponent>();
        for (auto entity : view3d) {
            const auto& transform = view3d.get<ECS::TransformComponent>(entity);
            const auto& mesh = view3d.get<ECS::MeshComponent>(entity);
            if (!mesh.Visible) {
                continue;
            }

            // Mesh-local bounds are not cached anywhere, so pick against the
            // transform's scaled unit box. Precise enough to select an object,
            // and it costs nothing to keep correct as meshes stream in and out.
            const Math::Vec3 halfExtent = glm::abs(transform.Scale) * 0.5f + Math::Vec3(0.05f);
            const Math::Vec3 boundsMin = transform.Position - halfExtent;
            const Math::Vec3 boundsMax = transform.Position + halfExtent;

            const float distance = RayAABB(rayOrigin, rayDirection, boundsMin, boundsMax);
            if (distance >= 0.0f && distance < bestDistance) {
                bestDistance = distance;
                best = entity;
            }
        }

        editorContext.Selection.Primary = best;
        editorContext.Selection.SelectionSet.clear();
        if (best != entt::null) {
            editorContext.Selection.SelectionSet.push_back(best);
        }
        editorContext.Selection.Source = "viewport";
        editorContext.Selection.Dirty = true;
    }

    void ViewportPanel::DrawGizmo(EditorContext& editorContext, const Math::Vec2& imageMin,
                                  const Math::Vec2& imageSize) {
        if (m_State.Gizmo == GizmoMode::None || !editorContext.ActiveScene ||
            editorContext.Selection.Primary == entt::null) {
            return;
        }

        auto& registry = editorContext.ActiveScene->GetRegistry();
        if (!registry.valid(editorContext.Selection.Primary) ||
            !registry.all_of<ECS::TransformComponent>(editorContext.Selection.Primary)) {
            return;
        }
        auto& transform = registry.get<ECS::TransformComponent>(editorContext.Selection.Primary);

        Math::Mat4 view = BuildView();
        Math::Mat4 projection = BuildProjection();
        if (!m_State.UseEditorCamera) {
            if (auto* pipeline = editorContext.ActiveScene->GetSystemPipeline()) {
                if (const auto* cameraSystem = pipeline->GetCameraSystem()) {
                    view = cameraSystem->GetViewMatrix();
                    projection = cameraSystem->GetProjectionMatrix();
                }
            }
        }
        const Math::Mat4 viewProjection = projection * view;

        auto project = [&](const Math::Vec3& world, Math::Vec2& out) -> bool {
            const Math::Vec4 clip = viewProjection * Math::Vec4(world, 1.0f);
            if (clip.w <= 1e-5f) {
                return false;
            }
            const Math::Vec3 ndc = Math::Vec3(clip) / clip.w;
            out = Math::Vec2(imageMin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x,
                             imageMin.y + (ndc.y * 0.5f + 0.5f) * imageSize.y);
            return true;
        };

        Math::Vec2 originScreen;
        if (!project(transform.Position, originScreen)) {
            return;
        }

        const Math::Vec3 axes[3] = {
            Math::Vec3(1.0f, 0.0f, 0.0f),
            Math::Vec3(0.0f, 1.0f, 0.0f),
            Math::Vec3(0.0f, 0.0f, 1.0f),
        };
        const ImU32 colors[3] = {IM_COL32(230, 70, 70, 255), IM_COL32(90, 220, 90, 255),
                                 IM_COL32(80, 140, 240, 255)};
        const float handleLength = 1.0f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        Math::Vec2 tips[3];
        bool tipVisible[3] = {false, false, false};

        for (int axis = 0; axis < 3; ++axis) {
            tipVisible[axis] = project(transform.Position + axes[axis] * handleLength, tips[axis]);
            if (!tipVisible[axis]) {
                continue;
            }
            const ImU32 color = (m_DragAxis == axis) ? IM_COL32(255, 235, 120, 255) : colors[axis];
            drawList->AddLine(ToImVec(originScreen), ToImVec(tips[axis]), color, 2.5f);
            drawList->AddCircleFilled(ToImVec(tips[axis]), 5.0f, color);
        }

        const ImVec2 mouse = ImGui::GetMousePos();
        if (m_DragAxis < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            for (int axis = 0; axis < 3; ++axis) {
                if (!tipVisible[axis]) {
                    continue;
                }
                const float dx = mouse.x - tips[axis].x;
                const float dy = mouse.y - tips[axis].y;
                if (dx * dx + dy * dy <= 100.0f) {
                    m_DragAxis = axis;
                    m_DragStartPosition = transform.Position;
                    m_DragStartMouse = Math::Vec2(mouse.x, mouse.y);
                    break;
                }
            }
        }

        if (m_DragAxis >= 0) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_DragAxis = -1;
            } else if (tipVisible[m_DragAxis]) {
                // Project the mouse delta onto the axis as it appears on screen,
                // scaled by how long that axis is in pixels. Dragging then tracks
                // the handle at any camera angle or distance.
                const Math::Vec2 axisScreen = tips[m_DragAxis] - originScreen;
                const float axisLengthSq = glm::dot(axisScreen, axisScreen);
                if (axisLengthSq > 1.0f) {
                    const Math::Vec2 mouseDelta(mouse.x - m_DragStartMouse.x,
                                                mouse.y - m_DragStartMouse.y);
                    const float amount = glm::dot(mouseDelta, axisScreen) / axisLengthSq;
                    switch (m_State.Gizmo) {
                        case GizmoMode::Translate:
                            transform.Position = m_DragStartPosition + axes[m_DragAxis] * amount * handleLength;
                            break;
                        case GizmoMode::Scale:
                            transform.Scale[m_DragAxis] = std::max(0.01f, transform.Scale[m_DragAxis] + amount * 0.05f);
                            m_DragStartMouse = Math::Vec2(mouse.x, mouse.y);
                            break;
                        case GizmoMode::Rotate:
                            transform.Rotation[m_DragAxis] += amount * 0.05f;
                            m_DragStartMouse = Math::Vec2(mouse.x, mouse.y);
                            break;
                        case GizmoMode::None:
                            break;
                    }
                    editorContext.Dirty = true;
                }
            }
        }
    }

    void ViewportPanel::DrawToolbar(EditorContext& editorContext) {
        auto* pipeline = editorContext.ActiveScene ? editorContext.ActiveScene->GetSystemPipeline()
                                                   : nullptr;

        // Play in editor: the pipeline already supports pause, single-step, and
        // time scale; nothing exposed them.
        const bool paused = pipeline && pipeline->IsPaused();
        if (ImGui::Button(paused ? "Play" : "Pause")) {
            if (pipeline) {
                pipeline->SetPaused(!paused);
                editorContext.IsPlayMode = !pipeline->IsPaused();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Step")) {
            if (pipeline) {
                pipeline->SetPaused(true);
                pipeline->RequestStepFrames(1);
            }
        }

        ImGui::SameLine();
        ImGui::Checkbox("Editor camera", &m_State.UseEditorCamera);
        ImGui::SameLine();
        if (ImGui::Button("Focus")) {
            FocusOnSelection(editorContext);
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        const char* gizmoNames[] = {"None", "Move", "Rotate", "Scale"};
        int gizmoIndex = static_cast<int>(m_State.Gizmo);
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::Combo("##gizmo", &gizmoIndex, gizmoNames, 4)) {
            m_State.Gizmo = static_cast<GizmoMode>(gizmoIndex);
        }
    }

    void ViewportPanel::Render(EditorContext& editorContext, float deltaTime) {
        if (!m_State.Open) {
            return;
        }

        EnsureTextureBinding();

        ImGui::SetNextWindowSize(ImVec2(960.0f, 600.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Viewport", &m_State.Open)) {
            ImGui::End();
            return;
        }

        DrawToolbar(editorContext);
        ImGui::Separator();

        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x < 8.0f || available.y < 8.0f) {
            ImGui::End();
            return;
        }
        m_Aspect = available.x / available.y;

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const Math::Vec2 imageMin(cursor.x, cursor.y);
        const Math::Vec2 imageSize(available.x, available.y);

        if (m_ImGuiTexture != VK_NULL_HANDLE) {
            ImGui::Image(reinterpret_cast<ImTextureID>(m_ImGuiTexture), available);
        } else {
            // No renderer output yet (headless, or before the first frame).
            ImGui::Dummy(available);
            ImGui::GetWindowDrawList()->AddRectFilled(
                cursor, ImVec2(cursor.x + available.x, cursor.y + available.y),
                IM_COL32(24, 26, 32, 255));
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(cursor.x + 12.0f, cursor.y + 12.0f), IM_COL32(200, 200, 210, 255),
                "Renderer output unavailable");
        }

        const bool hovered = ImGui::IsItemHovered();
        UpdateEditorCamera(deltaTime, hovered);

        // Gizmo first: a click that grabs a handle must not also re-pick.
        const int dragBefore = m_DragAxis;
        DrawGizmo(editorContext, imageMin, imageSize);
        if (hovered && m_DragAxis < 0 && dragBefore < 0 &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            HandlePicking(editorContext, imageMin, imageSize);
        }

        if (m_State.ShowStats && m_Context) {
            if (auto* sceneRenderer = m_Context->GetSceneRenderer()) {
                const auto& stats = sceneRenderer->GetStats();
                const auto& cull = sceneRenderer->GetCuller().GetStats();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(cursor.x + 10.0f, cursor.y + 10.0f), IM_COL32(240, 240, 245, 220),
                    "");
                ImGui::SetCursorScreenPos(ImVec2(cursor.x + 10.0f, cursor.y + 10.0f));
                ImGui::BeginGroup();
                ImGui::Text("%ux%u -> %ux%u", stats.RenderWidth, stats.RenderHeight,
                            stats.DisplayWidth, stats.DisplayHeight);
                ImGui::Text("indirect %u  direct %u  skipped %u",
                            stats.IndirectDraws, stats.DirectDraws, stats.SkippedDraws);
                ImGui::Text("clusters %u  visible %u/%u  culled f%u c%u o%u",
                            cull.ClusterSlots, cull.VisibleEarly, cull.VisibleLate,
                            cull.FrustumCulled, cull.ConeCulled, cull.OcclusionCulled);
                ImGui::EndGroup();
            }
        }

        ImGui::End();
    }

} // namespace Editor
} // namespace Core
