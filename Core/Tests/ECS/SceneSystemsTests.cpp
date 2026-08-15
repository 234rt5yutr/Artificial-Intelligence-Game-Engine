// Checks the ECS systems that feed the renderer.
//
// The gap analysis flagged that nothing tested the ECS. These three systems are
// the whole bridge between gameplay state and a frame: TransformSystem decides
// where things are, RenderSystem decides what is drawn, LightSystem decides what
// lights it. A silent regression in any of them looks like a rendering bug, and
// all three run headless, so there is no reason for them to be untested.

#include "Core/ECS/Components/HierarchyComponent.h"
#include "Core/ECS/Components/LightComponent.h"
#include "Core/ECS/Components/MeshComponent.h"
#include "Core/ECS/Components/NameComponent.h"
#include "Core/ECS/Components/SkeletalMeshComponent.h"
#include "Core/ECS/Components/TransformComponent.h"
#include "Core/ECS/Entity.h"
#include "Core/ECS/Scene.h"
#include "Core/ECS/Systems/LightSystem.h"
#include "Core/ECS/Systems/RenderSystem.h"
#include "Core/ECS/Systems/TransformSystem.h"
#include "Core/Log.h"
#include "Core/Renderer/Mesh.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// ctest runs these with -C Release, where NDEBUG turns assert() into a no-op:
// the condition is not evaluated, so any call inside one silently disappears and
// nothing is actually verified. CHECK always evaluates and always reports.
#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n",                 \
                         #expr, __FILE__, __LINE__);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

namespace {

using namespace Core;
using namespace Core::ECS;

bool NearlyEqual(const Math::Vec3& a, const Math::Vec3& b, float tolerance = 1e-3f) {
    return glm::length(a - b) < tolerance;
}

bool NearlyIdentity(const Math::Mat4& matrix, float tolerance = 1e-4f) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            const float expected = column == row ? 1.0f : 0.0f;
            if (std::fabs(matrix[column][row] - expected) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

void TestEntityLifecycle() {
    Scene scene("Test");
    CHECK(scene.GetEntityCount() == 0);

    Entity first = scene.CreateEntity("First");
    Entity second = scene.CreateEntity("Second");
    CHECK(scene.GetEntityCount() == 2);

    auto& registry = scene.GetRegistry();
    CHECK(registry.valid(first.GetHandle()));
    CHECK(registry.all_of<NameComponent>(first.GetHandle()));

    scene.DestroyEntity(first);
    CHECK(scene.GetEntityCount() == 1);
    CHECK(!registry.valid(first.GetHandle()));
    // Destroying one entity must not disturb another; entt recycles ids, and a
    // stale handle that still validates is how a use-after-destroy hides.
    CHECK(registry.valid(second.GetHandle()));
}

void TestTransformHierarchyPropagates() {
    Scene scene("Hierarchy");
    auto& registry = scene.GetRegistry();

    Entity parent = scene.CreateEntity("Parent");
    Entity child = scene.CreateEntity("Child");
    const auto parentHandle = parent.GetHandle();
    const auto childHandle = child.GetHandle();

    auto& parentTransform = registry.emplace<TransformComponent>(parentHandle);
    parentTransform.Position = Math::Vec3(10.0f, 0.0f, 0.0f);

    auto& childTransform = registry.emplace<TransformComponent>(childHandle);
    childTransform.Position = Math::Vec3(0.0f, 5.0f, 0.0f);

    TransformSystem::SetParent(scene, childHandle, parentHandle);
    CHECK(registry.all_of<HierarchyComponent>(childHandle));
    CHECK(registry.get<HierarchyComponent>(childHandle).Parent == parentHandle);
    CHECK(registry.get<HierarchyComponent>(parentHandle).Children.size() == 1);

    TransformSystem transforms;
    transforms.ForceUpdateAll(scene);

    // The child's world position is its local offset composed with the parent's,
    // not its local position. Getting this wrong puts every attached object at
    // the origin, which reads as "the model did not load".
    const Math::Vec3 childWorld(registry.get<TransformComponent>(childHandle).WorldMatrix[3]);
    CHECK(NearlyEqual(childWorld, Math::Vec3(10.0f, 5.0f, 0.0f)));

    // Moving the parent moves the child.
    registry.get<TransformComponent>(parentHandle).Position = Math::Vec3(-4.0f, 1.0f, 2.0f);
    transforms.ForceUpdateAll(scene);
    const Math::Vec3 movedWorld(registry.get<TransformComponent>(childHandle).WorldMatrix[3]);
    CHECK(NearlyEqual(movedWorld, Math::Vec3(-4.0f, 6.0f, 2.0f)));

    // Detaching leaves the child where it was in local space, standing alone.
    TransformSystem::RemoveParent(scene, childHandle);
    transforms.ForceUpdateAll(scene);
    const Math::Vec3 detachedWorld(registry.get<TransformComponent>(childHandle).WorldMatrix[3]);
    CHECK(NearlyEqual(detachedWorld, Math::Vec3(0.0f, 5.0f, 0.0f)));
}

void TestParentScaleAppliesToChildOffset() {
    Scene scene("Scaled");
    auto& registry = scene.GetRegistry();

    Entity parent = scene.CreateEntity("Parent");
    Entity child = scene.CreateEntity("Child");
    const auto parentHandle = parent.GetHandle();
    const auto childHandle = child.GetHandle();

    auto& parentTransform = registry.emplace<TransformComponent>(parentHandle);
    parentTransform.Scale = Math::Vec3(2.0f);

    auto& childTransform = registry.emplace<TransformComponent>(childHandle);
    childTransform.Position = Math::Vec3(3.0f, 0.0f, 0.0f);

    TransformSystem::SetParent(scene, childHandle, parentHandle);
    TransformSystem transforms;
    transforms.ForceUpdateAll(scene);

    // A scaled parent scales its children's offsets, not just their sizes.
    const Math::Vec3 childWorld(registry.get<TransformComponent>(childHandle).WorldMatrix[3]);
    CHECK(NearlyEqual(childWorld, Math::Vec3(6.0f, 0.0f, 0.0f)));
}

void TestRenderSystemCollectsVisibleMeshes() {
    Scene scene("Draws");
    auto& registry = scene.GetRegistry();
    auto mesh = Renderer::Mesh::CreatePrimitive("box");
    CHECK(mesh != nullptr);

    Entity visible = scene.CreateEntity("Visible");
    registry.emplace<TransformComponent>(visible.GetHandle());
    auto& visibleMesh = registry.emplace<MeshComponent>(visible.GetHandle(), mesh);
    visibleMesh.CastShadows = true;

    Entity hidden = scene.CreateEntity("Hidden");
    registry.emplace<TransformComponent>(hidden.GetHandle());
    auto& hiddenMesh = registry.emplace<MeshComponent>(hidden.GetHandle(), mesh);
    hiddenMesh.Visible = false;

    // A mesh component with no mesh data is the shape an entity has before its
    // asset arrives; it must not produce a draw against a null pointer.
    Entity empty = scene.CreateEntity("Empty");
    registry.emplace<TransformComponent>(empty.GetHandle());
    registry.emplace<MeshComponent>(empty.GetHandle());

    RenderSystem renderSystem;
    renderSystem.Update(scene);

    const auto& commands = renderSystem.GetDrawCommands();
    CHECK(commands.size() == 1);
    CHECK(commands[0].Mesh == mesh.get());
    CHECK(commands[0].CastShadows);
    CHECK(renderSystem.GetTotalEntityCount() == 3);
    CHECK(renderSystem.GetVisibleEntityCount() == 1);

    // Re-running must not accumulate: the draw list is rebuilt every frame, and
    // a leak here would grow the GPU scene without bound.
    renderSystem.Update(scene);
    CHECK(renderSystem.GetDrawCommands().size() == 1);

    renderSystem.ClearDrawCommands();
    CHECK(renderSystem.GetDrawCommands().empty());
}

void TestRenderSystemHonoursVisibilityTest() {
    Scene scene("Culled");
    auto& registry = scene.GetRegistry();
    auto mesh = Renderer::Mesh::CreatePrimitive("sphere", 8);

    for (int i = 0; i < 4; ++i) {
        Entity entity = scene.CreateEntity("Mesh");
        auto& transform = registry.emplace<TransformComponent>(entity.GetHandle());
        transform.Position = Math::Vec3(static_cast<float>(i) * 10.0f, 0.0f, 0.0f);
        registry.emplace<MeshComponent>(entity.GetHandle(), mesh);
    }

    // The draw command's transform is the *world* matrix, so the transform pass
    // has to run first - exactly as SystemPipeline orders them. Without it every
    // world matrix is identity and a position-based visibility test sees every
    // object at the origin.
    TransformSystem transforms;
    transforms.ForceUpdateAll(scene);

    RenderSystem renderSystem;
    // Stand-in for frustum culling: keep only what is near the origin.
    renderSystem.SetVisibilityTest([](const Math::Mat4& transform, const Renderer::Mesh*) {
        return Math::Vec3(transform[3]).x < 15.0f;
    });
    renderSystem.Update(scene);

    CHECK(renderSystem.GetDrawCommands().size() == 2);
    CHECK(renderSystem.GetTotalEntityCount() == 4);
}

void TestLightSystemSeparatesLightTypes() {
    Scene scene("Lights");
    auto& registry = scene.GetRegistry();

    Entity sun = scene.CreateEntity("Sun");
    registry.emplace<TransformComponent>(sun.GetHandle());
    registry.emplace<LightComponent>(
        sun.GetHandle(),
        LightComponent::CreateDirectional(Math::Vec3(1.0f), 3.0f, true));

    Entity bulb = scene.CreateEntity("Bulb");
    auto& bulbTransform = registry.emplace<TransformComponent>(bulb.GetHandle());
    bulbTransform.Position = Math::Vec3(2.0f, 3.0f, 4.0f);
    auto bulbLight = LightComponent::CreatePoint(Math::Vec3(1.0f, 0.5f, 0.25f), 2.0f, 12.0f);
    bulbLight.CastShadows = true;
    registry.emplace<LightComponent>(bulb.GetHandle(), bulbLight);

    Entity lamp = scene.CreateEntity("Lamp");
    registry.emplace<TransformComponent>(lamp.GetHandle());
    auto lampLight = LightComponent::CreateSpot(Math::Vec3(1.0f), 5.0f, 20.0f, 15.0f, 35.0f);
    lampLight.CastShadows = false;
    registry.emplace<LightComponent>(lamp.GetHandle(), lampLight);

    Entity off = scene.CreateEntity("Disabled");
    registry.emplace<TransformComponent>(off.GetHandle());
    auto disabled = LightComponent::CreatePoint(Math::Vec3(1.0f), 1.0f, 5.0f);
    disabled.Enabled = false;
    registry.emplace<LightComponent>(off.GetHandle(), disabled);

    TransformSystem transforms;
    transforms.ForceUpdateAll(scene);

    LightSystem lightSystem;
    lightSystem.Update(scene);

    CHECK(lightSystem.GetDirectionalLightCount() == 1);
    CHECK(lightSystem.GetPointLightCount() == 1);
    CHECK(lightSystem.GetSpotLightCount() == 1);
    CHECK(lightSystem.GetTotalLightCount() == 3);

    // The shadow renderer picks its cascade light and allocates atlas tiles from
    // these flags. They used to be padding, so a regression here would silently
    // stop everything casting.
    CHECK(lightSystem.GetDirectionalLights()[0].CastShadows > 0.5f);
    CHECK(lightSystem.GetPointLights()[0].CastShadows > 0.5f);
    CHECK(lightSystem.GetSpotLights()[0].CastShadows < 0.5f);

    // Point light position comes from the transform's world matrix, not its
    // local position.
    CHECK(NearlyEqual(lightSystem.GetPointLights()[0].Position, Math::Vec3(2.0f, 3.0f, 4.0f)));
    CHECK(std::abs(lightSystem.GetPointLights()[0].Radius - 12.0f) < 1e-3f);

    // Cutoffs stay half-angles in radians all the way to the renderer, which
    // converts to cosines on upload. Storing cosines here instead would make the
    // shadow projection maths silently wrong.
    const float outer = lightSystem.GetSpotLights()[0].OuterCutoff;
    CHECK(outer > 0.5f && outer < 0.7f);   // 35 degrees is about 0.611 rad

    // Re-running rebuilds rather than appends.
    lightSystem.Update(scene);
    CHECK(lightSystem.GetTotalLightCount() == 3);
}

// The skinning matrices RenderSystem builds are what the GPU pass multiplies
// every vertex by, so getting the local -> global -> inverse-bind chain wrong
// silently deforms every character. CreateSkinnedPrimitive is rigged so that
// its bind pose is exactly the identity, which makes the chain checkable.
void TestSkeletalDrawCommandsCarryBoneMatrices() {
    Scene scene("Skinning");
    auto mesh = Renderer::Mesh::CreateSkinnedPrimitive(8, 4, 3);
    CHECK(mesh != nullptr);
    CHECK(mesh->IsSkeletal());
    CHECK(mesh->GetSkeleton().GetBoneCount() == 3);
    CHECK(mesh->skinnedVertices.size() == mesh->vertices.size());

    Entity rig = scene.CreateEntity("Rig");
    rig.AddComponent<TransformComponent>();
    auto& skeletal = rig.AddComponent<SkeletalMeshComponent>();
    skeletal.MeshData = mesh;

    RenderSystem renderSystem;
    renderSystem.Update(scene);

    const auto& commands = renderSystem.GetDrawCommands();
    CHECK(commands.size() == 1);
    CHECK(commands[0].BoneCount == 3);
    CHECK(commands[0].BoneOffset == 0);

    const auto& bones = renderSystem.GetBoneMatrices();
    CHECK(bones.size() == 3);
    // No pose was evaluated, so the bind pose is what came back, and skinning by
    // the bind pose must not move a vertex.
    for (const auto& matrix : bones) {
        CHECK(NearlyIdentity(matrix));
    }

    // Displacing the middle joint must leave the root untouched and carry the
    // bone above it along by the same amount. That is the whole point of
    // resolving the hierarchy parent-first: get the order wrong and the child
    // keeps its bind position while its parent walks away.
    auto& pose = skeletal.CurrentPose;
    CHECK(pose.LocalPoses.size() == 3);
    for (uint32_t i = 0; i < 3; ++i) {
        pose.LocalPoses[i].Translation =
            Math::Vec3(mesh->GetSkeleton().Bones[i].LocalTransform[3]);
    }
    pose.LocalPoses[1].Translation.x += 0.5f;

    renderSystem.Update(scene);
    const auto& posed = renderSystem.GetBoneMatrices();
    CHECK(posed.size() == 3);
    CHECK(NearlyIdentity(posed[0]));
    CHECK(NearlyEqual(Math::Vec3(posed[1][3]), Math::Vec3(0.5f, 0.0f, 0.0f)));
    CHECK(NearlyEqual(Math::Vec3(posed[2][3]), Math::Vec3(0.5f, 0.0f, 0.0f)));
}

} // namespace

int main() {
    Engine::Log::Init();

    TestEntityLifecycle();
    TestTransformHierarchyPropagates();
    TestParentScaleAppliesToChildOffset();
    TestRenderSystemCollectsVisibleMeshes();
    TestRenderSystemHonoursVisibilityTest();
    TestLightSystemSeparatesLightTypes();
    TestSkeletalDrawCommandsCarryBoneMatrices();

    std::printf("SceneSystemsTests: all checks passed\n");
    return 0;
}
