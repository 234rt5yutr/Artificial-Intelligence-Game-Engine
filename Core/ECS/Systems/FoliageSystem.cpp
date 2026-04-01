#include "Core/ECS/Systems/FoliageSystem.h"

namespace Core {
namespace ECS {

    FoliageSystem::FoliageSystem()
        : m_FoliageScatter(std::make_unique<Renderer::FoliageScatter>())
    {
    }

    FoliageSystem::~FoliageSystem()
    {
        Shutdown();
    }

    void FoliageSystem::Initialize(std::shared_ptr<RHI::RHIDevice> device)
    {
        PROFILE_FUNCTION();

        m_Device = device;
        
        if (m_FoliageScatter) {
            m_FoliageScatter->Initialize(device);
        }

        m_IsInitialized = true;
        ENGINE_CORE_INFO("FoliageSystem initialized");
    }

    void FoliageSystem::Shutdown()
    {
        PROFILE_FUNCTION();

        if (!m_IsInitialized) return;

        if (m_FoliageScatter) {
            m_FoliageScatter->Shutdown();
        }

        m_EntityToScatterIndex.clear();
        m_IsInitialized = false;

        ENGINE_CORE_INFO("FoliageSystem shutdown");
    }

    void FoliageSystem::Update(Scene& scene, float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_IsInitialized || !m_FoliageScatter) return;

        m_TotalTime += deltaTime;

        // Sync foliage components with scatter system
        SyncFoliageComponents(scene);

        // Update camera for culling
        UpdateCamera(scene);

        // Update wind animation
        UpdateWind(deltaTime);

        ENGINE_CORE_TRACE("FoliageSystem: {} types, {} visible instances",
                          GetFoliageTypeCount(), GetVisibleInstanceCount());
    }

    void FoliageSystem::DispatchScatter(std::shared_ptr<RHI::RHICommandList> commandList)
    {
        PROFILE_FUNCTION();

        if (!m_IsInitialized || !m_FoliageScatter || !commandList) return;

        // Execute GPU scatter compute shader
        m_FoliageScatter->ScatterFoliage(commandList);

        // Update wind animation
        m_FoliageScatter->UpdateWindAnimation(commandList, 0.016f);
    }

    void FoliageSystem::Render(std::shared_ptr<RHI::RHICommandList> commandList)
    {
        PROFILE_FUNCTION();

        if (!m_IsInitialized || !m_FoliageScatter || !commandList) return;

        // Get instance buffer for rendering
        auto instanceBuffer = m_FoliageScatter->GetInstanceBuffer();
        auto drawCommandBuffer = m_FoliageScatter->GetDrawCommandBuffer();

        if (!instanceBuffer || !drawCommandBuffer) return;

        // Actual draw calls would be submitted here via the command list
        // This would typically use indirect drawing:
        // commandList->DrawIndexedIndirect(drawCommandBuffer, ...);

        // For now, just log the operation
        uint32_t instanceCount = m_FoliageScatter->GetVisibleInstanceCount();
        ENGINE_CORE_TRACE("FoliageSystem: Rendering {} foliage instances", instanceCount);
    }

    void FoliageSystem::SetWindDirection(const Math::Vec3& direction)
    {
        m_Config.WindDirection = glm::normalize(direction);
    }

    void FoliageSystem::SetWindStrength(float strength)
    {
        m_Config.GlobalWindStrength = strength;
        if (m_FoliageScatter) {
            m_FoliageScatter->SetGlobalWindStrength(strength);
        }
    }

    uint32_t FoliageSystem::GetVisibleInstanceCount() const
    {
        return m_FoliageScatter ? m_FoliageScatter->GetVisibleInstanceCount() : 0;
    }

    uint32_t FoliageSystem::GetTotalInstanceCount() const
    {
        return m_FoliageScatter ? m_FoliageScatter->GetStats().TotalInstances : 0;
    }

    uint32_t FoliageSystem::GetFoliageTypeCount() const
    {
        return m_FoliageScatter ? m_FoliageScatter->GetFoliageTypeCount() : 0;
    }

    void FoliageSystem::SyncFoliageComponents(Scene& scene)
    {
        PROFILE_FUNCTION();

        if (!m_FoliageScatter) return;

        auto view = scene.View<TransformComponent, FoliageComponent>();

        // Track which entities we've seen this frame
        std::unordered_map<uint32_t, bool> seenEntities;

        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& foliage = view.get<FoliageComponent>(entity);

            if (!foliage.Enabled) continue;

            uint32_t entityId = static_cast<uint32_t>(entity);
            seenEntities[entityId] = true;

            auto it = m_EntityToScatterIndex.find(entityId);

            if (it == m_EntityToScatterIndex.end()) {
                // New foliage entity - register it
                uint32_t scatterIndex = m_FoliageScatter->RegisterFoliage(
                    foliage, transform.WorldMatrix);
                m_EntityToScatterIndex[entityId] = scatterIndex;
                foliage.NeedsRescatter = false;
            } else if (foliage.NeedsRescatter) {
                // Existing entity needs update
                m_FoliageScatter->UpdateFoliage(it->second, foliage, transform.WorldMatrix);
                foliage.NeedsRescatter = false;
            }
        }

        // Remove foliage for entities that no longer exist
        std::vector<uint32_t> toRemove;
        for (const auto& [entityId, scatterIndex] : m_EntityToScatterIndex) {
            if (seenEntities.find(entityId) == seenEntities.end()) {
                m_FoliageScatter->RemoveFoliage(scatterIndex);
                toRemove.push_back(entityId);
            }
        }
        for (uint32_t entityId : toRemove) {
            m_EntityToScatterIndex.erase(entityId);
        }
    }

    void FoliageSystem::UpdateCamera(Scene& scene)
    {
        PROFILE_FUNCTION();

        if (!m_FoliageScatter) return;

        auto cameraView = scene.View<TransformComponent, CameraComponent>();

        for (auto entity : cameraView) {
            auto& transform = cameraView.get<TransformComponent>(entity);
            auto& camera = cameraView.get<CameraComponent>(entity);

            if (camera.IsActive) {
                Math::Mat4 viewProjection = camera.GetProjectionMatrix() * 
                                            camera.GetViewMatrix(transform);
                Math::Vec3 cameraPos = Math::Vec3(transform.WorldMatrix[3]);

                m_FoliageScatter->UpdateCamera(viewProjection, cameraPos);
                break;
            }
        }
    }

    void FoliageSystem::UpdateWind(float deltaTime)
    {
        PROFILE_FUNCTION();

        if (!m_FoliageScatter) return;

        // Animate wind phase
        m_WindPhase += deltaTime * m_Config.WindSpeed;
        if (m_WindPhase > 6.28318f) {
            m_WindPhase -= 6.28318f;
        }

        // Add gustiness variation
        float gustFactor = 1.0f + m_Config.WindGustiness * 
            std::sin(m_WindPhase * 2.7f) * std::sin(m_WindPhase * 0.3f);

        float effectiveStrength = m_Config.GlobalWindStrength * gustFactor;

        m_FoliageScatter->UpdateWind(
            m_Config.WindDirection,
            effectiveStrength,
            m_TotalTime
        );
    }

} // namespace ECS
} // namespace Core
