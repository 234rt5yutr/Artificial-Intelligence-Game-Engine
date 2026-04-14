cmake_minimum_required(VERSION 3.20)

# Integrate vcpkg toolchain
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using custom toolchain: ${CMAKE_TOOLCHAIN_FILE}")
else()
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
    message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

project(AIGameEngine VERSION 1.0.0 LANGUAGES CXX)

# Require C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add standard compiler flags
if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Dependencies
find_package(spdlog CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-shaderc CONFIG REQUIRED)
find_path(CGLTF_INCLUDE_DIRS "cgltf.h")
find_package(EnTT CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(GameNetworkingSockets CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h")
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)

# Core Engine Library
add_library(EngineCore
    Core/Log.cpp
    Core/Log.h
    Core/Profile.h
    Core/Assert.h
    Core/CrashHandler.h
    Core/CrashHandler.cpp
    Core/Event.h
    Core/Window.h
    Core/Window.cpp
    Core/Application.h
    Core/Application.cpp
    Core/Input.h
    Core/Input.cpp
    Core/InputMapper.h
    Core/InputMapper.cpp
    Core/Math/Math.h
    Core/Memory/Allocator.h
    Core/Memory/LinearAllocator.cpp
    Core/Memory/LinearAllocator.h
    Core/Memory/PoolAllocator.cpp
    Core/Memory/PoolAllocator.h
    Core/JobSystem/JobSystem.h
    Core/JobSystem/JobSystem.cpp
    Core/RHI/RHIBuffer.h
    Core/RHI/RHITexture.h
    Core/RHI/RHIRenderPass.h
    Core/RHI/RHICommandList.h
    Core/RHI/RHIDevice.h
    Core/RHI/RHIPipelineState.h
    Core/RHI/ShaderCompiler.h
    Core/RHI/ShaderCompiler.cpp
    Core/RHI/ShaderPermutationLibrary.h
    Core/RHI/ShaderPermutationLibrary.cpp
    Core/RHI/PipelineCacheManager.h
    Core/RHI/PipelineCacheManager.cpp
    Core/RHI/RHIResourceUploader.h
    Core/RHI/Vulkan/VulkanBuffer.h
    Core/RHI/Vulkan/VulkanBuffer.cpp
    Core/RHI/Vulkan/VulkanTexture.h
    Core/RHI/Vulkan/VulkanTexture.cpp
    Core/RHI/Vulkan/VulkanResourceUploader.h
    Core/RHI/Vulkan/VulkanResourceUploader.cpp
    Core/RHI/Vulkan/VulkanContext.h
    Core/RHI/Vulkan/VulkanContext.cpp
    Core/RHI/Vulkan/VulkanPipelineState.h
    Core/RHI/Vulkan/VulkanPipelineState.cpp
    Core/Renderer/Mesh.h
    Core/Renderer/Mesh.cpp
    Core/Animation/AnimationRuntimeTypes.h
    Core/Animation/Graph/AnimationStateMachineGraph.h
    Core/Animation/Graph/AnimationStateMachineGraph.cpp
    Core/Animation/Graph/AnimationGraphValidator.h
    Core/Animation/Graph/AnimationGraphValidator.cpp
    Core/Animation/Blend/AnimationBlendTreeBuilder.h
    Core/Animation/Blend/AnimationBlendTreeBuilder.cpp
    Core/Animation/Blend/BlendTreeEvaluationCache.h
    Core/Animation/Blend/BlendTreeEvaluationCache.cpp
    Core/Animation/Parameters/AnimatorParameterChannel.h
    Core/Animation/Parameters/AnimatorParameterChannel.cpp
    Core/Animation/Events/AnimationEventSynchronizer.h
    Core/Animation/Events/AnimationEventSynchronizer.cpp
    Core/Animation/Retarget/RetargetTypes.h
    Core/Animation/Retarget/RetargetProfile.h
    Core/Animation/Retarget/RetargetProfile.cpp
    Core/Animation/Retarget/AnimationRetargeter.h
    Core/Animation/Retarget/AnimationRetargeter.cpp
    Core/Animation/ControlRig/ControlRigTypes.h
    Core/Animation/ControlRig/ControlRigRuntime.h
    Core/Animation/ControlRig/ControlRigRuntime.cpp
    Core/Animation/ControlRig/ControlRigBaker.h
    Core/Animation/ControlRig/ControlRigBaker.cpp
    Core/Animation/MotionMatching/MotionMatchingDatabase.h
    Core/Animation/MotionMatching/MotionMatchingDatabase.cpp
    Core/Animation/MotionMatching/PoseFeatureExtractor.h
    Core/Animation/MotionMatching/PoseFeatureExtractor.cpp
    Core/Animation/MotionMatching/MotionSearchIndex.h
    Core/Animation/MotionMatching/MotionSearchIndex.cpp
    Core/Animation/MotionMatching/TrajectoryPredictor.h
    Core/Animation/MotionMatching/TrajectoryPredictor.cpp
    Core/Animation/MotionMatching/MotionMatchingRuntime.h
    Core/Animation/MotionMatching/MotionMatchingRuntime.cpp
    Core/Asset/AssetTypes.h
    Core/Asset/AssetCooker.h
    Core/Asset/AssetCooker.cpp
    Core/Asset/AssetPipeline.h
    Core/Asset/AssetPipeline.cpp
    Core/Asset/AssetLoader.h
    Core/Asset/AssetLoader.cpp
    Core/Asset/Addressables/AddressablesCatalogTypes.h
    Core/Asset/Addressables/AddressablesCatalog.h
    Core/Asset/Addressables/AddressablesCatalog.cpp
    Core/Asset/Addressables/AddressableRuntimeTypes.h
    Core/Asset/Addressables/AddressableRuntime.h
    Core/Asset/Addressables/AddressableRuntime.cpp
    Core/Asset/Bundles/AssetBundleTypes.h
    Core/Asset/Bundles/AssetBundleBuilder.h
    Core/Asset/Bundles/AssetBundleBuilder.cpp
    Core/Asset/Bundles/AssetBundleMountService.h
    Core/Asset/Bundles/AssetBundleMountService.cpp
    Core/Asset/Bundles/AssetBundlePatcher.h
    Core/Asset/Bundles/AssetBundlePatcher.cpp
    Core/Asset/HotReload/AssetDependencyTracker.h
    Core/Asset/HotReload/AssetDependencyTracker.cpp
    Core/Asset/HotReload/AssetHotReloadTypes.h
    Core/Asset/HotReload/AssetHotReloadService.h
    Core/Asset/HotReload/AssetHotReloadService.cpp
    Core/MCP/JsonSerialization.h
    Core/MCP/HttpServer.h
    Core/MCP/HttpServer.cpp
    Core/MCP/MCPTypes.h
    Core/MCP/MCPTool.h
    Core/MCP/MCPServer.h
    Core/MCP/MCPServer.cpp
    Core/MCP/SceneSerialization.h
    Core/MCP/MCPSceneTools.h
    Core/MCP/ActionValidator.h
    Core/MCP/AutoLevelDesigner.h
    Core/MCP/MCPAllTools.h
    Core/MCP/MCPUITools.h
    Core/MCP/MCPAudioTools.h
    Core/MCP/MCPNetworkTools.h
    Core/State/SceneLoader.h
    Core/State/SceneLoader.cpp
    Core/State/TransitionManager.h
    Core/State/TransitionManager.cpp
    Core/State/SaveFile.h
    Core/State/SaveFile.cpp
    Core/State/SaveManager.h
    Core/State/SaveManager.cpp
    Core/World/OpenWorldRuntime.h
    Core/World/OpenWorldRuntime.cpp
    Core/Scripting/LuaEngine.h
    Core/Audio/AudioSystem.h
    Core/Audio/AudioSystem.cpp
    Core/Audio/AudioListenerSystem.h
    Core/Audio/AudioSourceSystem.h
    Core/Audio/PhysicsAudioIntegration.h
    Core/Audio/PhysicsAudioIntegration.cpp
    Core/Audio/ReverbZoneSystem.h
    Core/Renderer/ForwardPlus.h
    Core/Renderer/ForwardPlus.cpp
    Core/Renderer/ZPrepass.h
    Core/Renderer/ZPrepass.cpp
    Core/Renderer/RenderGraph/RenderGraphTypes.h
    Core/Renderer/RenderGraph/RenderGraphBuilder.h
    Core/Renderer/RenderGraph/RenderGraphBuilder.cpp
    Core/Renderer/RenderGraph/RenderGraphExecutor.h
    Core/Renderer/RenderGraph/RenderGraphExecutor.cpp
    Core/Renderer/RenderGraph/TransientResourceAllocator.h
    Core/Renderer/RenderGraph/TransientResourceAllocator.cpp
    Core/Renderer/ShadowPass.h
    Core/Renderer/ShadowPass.cpp
    Core/Renderer/RTShadowPass.h
    Core/Renderer/RTShadowPass.cpp
    Core/Renderer/RTReflectionPass.h
    Core/Renderer/RTReflectionPass.cpp
    Core/Renderer/GlobalIllumination.h
    Core/Renderer/GlobalIllumination.cpp
    Core/Renderer/RayTracingManager.h
    Core/Renderer/RayTracingManager.cpp
    Core/Renderer/VirtualShadows/VirtualShadowMapCache.h
    Core/Renderer/VirtualShadows/VirtualShadowMapCache.cpp
    Core/Renderer/FrustumCulling.h
    Core/Renderer/FrustumCulling.cpp
    Core/Renderer/TAASystem.h
    Core/Renderer/TAASystem.cpp
    Core/Renderer/DynamicResolution.h
    Core/Renderer/DynamicResolution.cpp
    Core/Renderer/Upscaling/TemporalUpscalerManager.h
    Core/Renderer/Upscaling/TemporalUpscalerManager.cpp
    Core/Renderer/Upscaling/FrameGenerationController.h
    Core/Renderer/Upscaling/FrameGenerationController.cpp
    Core/Renderer/Diagnostics/GPUFrameTraceService.h
    Core/Renderer/Diagnostics/GPUFrameTraceService.cpp
    Core/Diagnostics/ProfilerCaptureTypes.h
    Core/Diagnostics/CPUProfilerCapture.h
    Core/Diagnostics/CPUProfilerCapture.cpp
    Core/Diagnostics/GPUProfilerCapture.h
    Core/Diagnostics/GPUProfilerCapture.cpp
    Core/Diagnostics/TraceExporter.h
    Core/Diagnostics/TraceExporter.cpp
    Core/Automation/AutomationTypes.h
    Core/Automation/PlayModeTestRunner.h
    Core/Automation/PlayModeTestRunner.cpp
    Core/Automation/PerformanceTestRunner.h
    Core/Automation/PerformanceTestRunner.cpp
    Core/Build/BuildPipelineTypes.h
    Core/Build/BuildOrchestrator.h
    Core/Build/BuildOrchestrator.cpp
    Core/Build/StoreSubmissionPackager.h
    Core/Build/StoreSubmissionPackager.cpp
    Core/Build/DedicatedServerBuildService.h
    Core/Build/DedicatedServerBuildService.cpp
    Core/Audit/FieldAuditTypes.h
    Core/Audit/FieldInventoryService.h
    Core/Audit/FieldInventoryService.cpp
    Core/Audit/FieldValidationService.h
    Core/Audit/FieldValidationService.cpp
    Core/Renderer/GPUSkinning.h
    Core/Renderer/GPUSkinning.cpp
    Core/Renderer/VirtualGeometry/VirtualGeometryTypes.h
    Core/Renderer/VirtualGeometry/VirtualGeometryClusterBuilder.h
    Core/Renderer/VirtualGeometry/VirtualGeometryClusterBuilder.cpp
    Core/Renderer/VirtualGeometry/VirtualGeometryStreamingService.h
    Core/Renderer/VirtualGeometry/VirtualGeometryStreamingService.cpp
    Core/ECS/Scene.h
    Core/ECS/Scene.cpp
    Core/ECS/Entity.h
    Core/ECS/Entity.cpp
    Core/ECS/ParallelECS.h
    Core/ECS/ParallelECS.cpp
    Core/ECS/Components/Components.h
    Core/ECS/Components/TransformComponent.h
    Core/ECS/Components/NameComponent.h
    Core/ECS/Components/MeshComponent.h
    Core/ECS/Components/LightComponent.h
    Core/ECS/Components/HierarchyComponent.h
    Core/ECS/Components/RigidBodyComponent.h
    Core/ECS/Components/ColliderComponent.h
    Core/ECS/Components/CameraComponent.h
    Core/ECS/Components/CharacterControllerComponent.h
    Core/ECS/Components/PlayerControllerComponent.h
    Core/ECS/Components/FirstPersonCameraComponent.h
    Core/ECS/Components/ThirdPersonCameraComponent.h
    Core/ECS/Components/CameraViewInterpolatorComponent.h
    Core/ECS/Components/AudioListenerComponent.h
    Core/ECS/Components/AudioSourceComponent.h
    Core/ECS/Components/ReverbZoneComponent.h
    Core/ECS/Components/SkeletalMeshComponent.h
    Core/ECS/Components/AnimatorComponent.h
    Core/ECS/Components/NetworkTransformComponent.h
    Core/ECS/Systems/Systems.h
    Core/ECS/Systems/RenderSystem.h
    Core/ECS/Systems/RenderSystem.cpp
    Core/ECS/Systems/SkeletalRenderSystem.h
    Core/ECS/Systems/SkeletalRenderSystem.cpp
    Core/ECS/Systems/AnimatorSystem.h
    Core/ECS/Systems/AnimatorSystem.cpp
    Core/ECS/Systems/LightSystem.h
    Core/ECS/Systems/LightSystem.cpp
    Core/ECS/Systems/TransformSystem.h
    Core/ECS/Systems/TransformSystem.cpp
    Core/ECS/Systems/PhysicsSystem.h
    Core/ECS/Systems/PhysicsSystem.cpp
    Core/ECS/Systems/PhysicsSyncSystem.h
    Core/ECS/Systems/PhysicsSyncSystem.cpp
    Core/ECS/Systems/CameraSystem.h
    Core/ECS/Systems/CameraSystem.cpp
    Core/ECS/Systems/CharacterControllerSystem.h
    Core/ECS/Systems/CharacterControllerSystem.cpp
    Core/ECS/Systems/PlayerControllerSystem.h
    Core/ECS/Systems/PlayerControllerSystem.cpp
    Core/ECS/Systems/FirstPersonCameraSystem.h
    Core/ECS/Systems/FirstPersonCameraSystem.cpp
    Core/ECS/Systems/ThirdPersonCameraSystem.h
    Core/ECS/Systems/ThirdPersonCameraSystem.cpp
    Core/ECS/Systems/CameraViewInterpolatorSystem.h
    Core/ECS/Systems/CameraViewInterpolatorSystem.cpp
    Core/Physics/PhysicsLayers.h
    Core/Physics/PhysicsLayers.cpp
    Core/Physics/PhysicsWorld.h
    Core/Physics/PhysicsWorld.cpp
    Core/Physics/EngineJobSystemAdapter.h
    Core/Physics/EngineJobSystemAdapter.cpp
    Core/Network/NetworkHash.h
    Core/Network/NetworkContractState.h
    Core/Network/NetworkContractState.cpp
    Core/Network/Diagnostics/NetworkDiagnosticsState.h
    Core/Network/Diagnostics/NetworkDiagnosticsState.cpp
    Core/Network/Session/SessionTypes.h
    Core/Network/Session/SessionOrchestrator.h
    Core/Network/Session/SessionOrchestrator.cpp
    Core/Network/Policies/ReplicationPolicyTypes.h
    Core/Network/Policies/ReplicationPolicyRegistry.h
    Core/Network/Policies/ReplicationPolicyRegistry.cpp
    Core/Network/RPC/NetworkRPCTypes.h
    Core/Network/RPC/NetworkRPCRegistry.h
    Core/Network/RPC/NetworkRPCRegistry.cpp
    Core/Network/Replay/ReplayTypes.h
    Core/Network/Replay/NetworkReplayRecorder.h
    Core/Network/Replay/NetworkReplayRecorder.cpp
    Core/Network/Replay/NetworkReplayPlayer.h
    Core/Network/Replay/NetworkReplayPlayer.cpp
    Core/Network/Rollback/RollbackTypes.h
    Core/Network/Rollback/RollbackCoordinator.h
    Core/Network/Rollback/RollbackCoordinator.cpp
    Core/Network/Rollback/PredictionResimulation.h
    Core/Network/Rollback/PredictionResimulation.cpp
    Core/Network/Migration/HostMigrationTypes.h
    Core/Network/Migration/HostMigrationCoordinator.h
    Core/Network/Migration/HostMigrationCoordinator.cpp
    Core/Network/MultiplayerRuntimeFeatures.h
    Core/Network/MultiplayerRuntimeFeatures.cpp
    Core/Network/MultiplayerProductLayer.h
    Core/Network/MultiplayerProductLayer.cpp
    Core/Network/NetworkManager.h
    Core/Network/NetworkManager.cpp
    Core/Network/NetworkPackets.h
    Core/Network/NetworkServer.h
    Core/Network/NetworkServer.cpp
    Core/Network/NetworkClient.h
    Core/Network/NetworkClient.cpp
    Core/Network/ServerReplicationSystem.h
    Core/Network/ServerReplicationSystem.cpp
    Core/Network/ClientReplicationSystem.h
    Core/Network/ClientReplicationSystem.cpp
    Core/Network/ClientPredictionSystem.h
    Core/Network/ClientPredictionSystem.cpp
    Core/Network/ServerReconciliationSystem.h
    Core/Network/ServerReconciliationSystem.cpp
    Core/Renderer/Particles/TextureAtlas.h
    Core/Renderer/Particles/ParticlePool.h
    Core/Renderer/Particles/ParticlePool.cpp
    Core/Renderer/Particles/ParticleSystem.h
    Core/Renderer/Particles/ParticleSystem.cpp
    Core/Renderer/Particles/ParticleRenderPass.h
    Core/Renderer/Particles/ParticleRenderPass.cpp
    Core/ECS/Components/ParticleEmitterComponent.h
    Core/ECS/Components/PrefabInstanceComponent.h
    Core/ECS/Components/UIComponent.h
    Core/ECS/Components/WorldUIComponent.h
    Core/ECS/Systems/UISystem.h
    Core/ECS/Systems/UISystem.cpp
    # Phase 15: Post-Processing
    Core/ECS/Components/PostProcessComponent.h
    Core/ECS/Systems/PostProcessSystem.h
    Core/ECS/Systems/PostProcessSystem.cpp
    Core/Renderer/PostProcess/PostProcessPass.h
    Core/Renderer/PostProcess/FramebufferChain.h
    Core/Renderer/PostProcess/FramebufferChain.cpp
    Core/Renderer/PostProcess/PostProcessManager.h
    Core/Renderer/PostProcess/PostProcessManager.cpp
    # Phase 16: UI Framework
    Core/UI/ImGuiSubsystem.h
    Core/UI/ImGuiSubsystem.cpp
    Core/UI/UIManager.h
    Core/UI/UIManager.cpp
    Core/UI/TextRenderer.h
    Core/UI/TextRenderer.cpp
    Core/UI/FontAtlas.h
    Core/UI/FontAtlas.cpp
    Core/UI/Anchoring.h
    Core/UI/Widgets/Widget.h
    Core/UI/Widgets/Widget.cpp
    Core/UI/Widgets/WidgetSystem.h
    Core/UI/Widgets/WidgetSystem.cpp
    Core/UI/Widgets/HUDWidgets.h
    Core/UI/Widgets/HUDWidgets.cpp
    # Phase 27: Runtime UI Authoring Foundations
    Core/UI/Authoring/WidgetBlueprintAsset.h
    Core/UI/Authoring/WidgetBlueprintAsset.cpp
    Core/UI/Authoring/WidgetLayoutAsset.h
    Core/UI/Authoring/WidgetLayoutAsset.cpp
    Core/UI/Authoring/UIAssetAuthoringService.h
    Core/UI/Authoring/UIAssetAuthoringService.cpp
    Core/UI/Binding/UIBindingTypes.h
    Core/UI/Binding/UIBindingService.h
    Core/UI/Binding/UIBindingService.cpp
    Core/UI/Animation/WidgetTransitionTypes.h
    Core/UI/Animation/WidgetTransitionService.h
    Core/UI/Animation/WidgetTransitionService.cpp
    Core/UI/Localization/LocalizationTable.h
    Core/UI/Localization/LocalizationService.h
    Core/UI/Localization/LocalizationService.cpp
    Core/UI/Modal/ModalDialogService.h
    Core/UI/Modal/ModalDialogService.cpp
    Core/UI/Focus/UIFocusRouter.h
    Core/UI/Focus/UIFocusRouter.cpp
    Core/UI/World/WorldSpaceWidgetRenderer.h
    Core/UI/World/WorldSpaceWidgetRenderer.cpp
    # Phase 21: Editor Foundations, Prefab Workflow & Visual Authoring
    Core/Editor/Result.h
    Core/Editor/EditorState.h
    Core/Editor/EditorContext.h
    Core/Editor/EditorAPI.h
    Core/Editor/EditorAPI.cpp
    Core/Editor/EditorSelection.h
    Core/Editor/EditorSelection.cpp
    Core/Editor/EditorModule.h
    Core/Editor/EditorModule.cpp
    Core/Editor/Panels/SceneHierarchyPanel.h
    Core/Editor/Panels/SceneHierarchyPanel.cpp
    Core/Editor/Panels/InspectorPanel.h
    Core/Editor/Panels/InspectorPanel.cpp
    Core/Editor/Commands/IEditorCommand.h
    Core/Editor/Commands/CommandTransaction.h
    Core/Editor/Commands/CommandStack.h
    Core/Editor/Commands/CommandStack.cpp
    Core/Editor/Commands/ComponentPropertyCommand.h
    Core/Editor/Commands/ComponentPropertyCommand.cpp
    Core/Editor/Prefab/PrefabAsset.h
    Core/Editor/VisualScripting/GraphAsset.h
    Core/Editor/Sequencer/TimelineAsset.h
)

target_link_libraries(EngineCore PUBLIC spdlog::spdlog glm::glm Tracy::TracyClient SDL3::SDL3 Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator unofficial::shaderc::shaderc EnTT::EnTT Jolt::Jolt GameNetworkingSockets::GameNetworkingSockets httplib::httplib nlohmann_json::nlohmann_json ${LUA_LIBRARIES} imgui::imgui RecastNavigation::Recast RecastNavigation::Detour RecastNavigation::DetourCrowd RecastNavigation::DetourTileCache)
target_include_directories(EngineCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CGLTF_INCLUDE_DIRS} ${LUA_INCLUDE_DIR} ${MINIAUDIO_INCLUDE_DIRS} ${Stb_INCLUDE_DIR})

# Option to enable Tracy profiling
option(ENABLE_PROFILING "Enable Tracy Profiling" ON)
if(ENABLE_PROFILING)
    target_compile_definitions(EngineCore PUBLIC TRACY_ENABLE)
endif()

# Sandbox / Test Executable
add_executable(AIGameEngine src/main.cpp)
target_link_libraries(AIGameEngine PRIVATE EngineCore)

enable_testing()

add_executable(EngineCoreTests
    Core/Tests/Diagnostics/ProfilerCaptureTests.cpp
)
target_link_libraries(EngineCoreTests PRIVATE EngineCore)
add_test(NAME EngineCoreTests COMMAND EngineCoreTests)

add_executable(EngineCoreTraceExportTests
    Core/Tests/Diagnostics/TraceExportTests.cpp
)
target_link_libraries(EngineCoreTraceExportTests PRIVATE EngineCore)
add_test(NAME EngineCoreTraceExportTests COMMAND EngineCoreTraceExportTests)

add_executable(EngineCorePlayModeRunnerTests
    Core/Tests/Automation/PlayModeRunnerTests.cpp
)
target_link_libraries(EngineCorePlayModeRunnerTests PRIVATE EngineCore)
add_test(NAME EngineCorePlayModeRunnerTests COMMAND EngineCorePlayModeRunnerTests)

add_executable(EngineCorePerformanceRunnerTests
    Core/Tests/Automation/PerformanceRunnerTests.cpp
)
target_link_libraries(EngineCorePerformanceRunnerTests PRIVATE EngineCore)
add_test(NAME EngineCorePerformanceRunnerTests COMMAND EngineCorePerformanceRunnerTests)

add_executable(EngineCoreBuildPipelineTests
    Core/Tests/Build/BuildPipelineTests.cpp
)
target_link_libraries(EngineCoreBuildPipelineTests PRIVATE EngineCore)
add_test(NAME EngineCoreBuildPipelineTests COMMAND EngineCoreBuildPipelineTests)

add_executable(EngineCoreArtifactPackagingTests
    Core/Tests/Build/ArtifactPackagingTests.cpp
)
target_link_libraries(EngineCoreArtifactPackagingTests PRIVATE EngineCore)
add_test(NAME EngineCoreArtifactPackagingTests COMMAND EngineCoreArtifactPackagingTests)

add_executable(EngineCoreDedicatedServerArtifactTests
    Core/Tests/Build/DedicatedServerArtifactTests.cpp
)
target_link_libraries(EngineCoreDedicatedServerArtifactTests PRIVATE EngineCore)
add_test(NAME EngineCoreDedicatedServerArtifactTests COMMAND EngineCoreDedicatedServerArtifactTests)

add_executable(EngineCoreFieldInventoryTests
    Core/Tests/Audit/FieldInventoryTests.cpp
)
target_link_libraries(EngineCoreFieldInventoryTests PRIVATE EngineCore)
add_test(NAME EngineCoreFieldInventoryTests COMMAND EngineCoreFieldInventoryTests)

add_executable(EngineCoreFieldValidationTests
    Core/Tests/Audit/FieldValidationTests.cpp
)
target_link_libraries(EngineCoreFieldValidationTests PRIVATE EngineCore)
add_test(NAME EngineCoreFieldValidationTests COMMAND EngineCoreFieldValidationTests)
