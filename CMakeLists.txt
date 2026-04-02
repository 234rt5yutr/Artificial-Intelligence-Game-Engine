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
    Core/Asset/AssetTypes.h
    Core/Asset/AssetCooker.h
    Core/Asset/AssetCooker.cpp
    Core/Asset/AssetPipeline.h
    Core/Asset/AssetPipeline.cpp
    Core/Asset/AssetLoader.h
    Core/Asset/AssetLoader.cpp
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
    Core/MCP/MCPAudioTools.h
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
    Core/Renderer/ShadowPass.h
    Core/Renderer/ShadowPass.cpp
    Core/Renderer/FrustumCulling.h
    Core/Renderer/FrustumCulling.cpp
    Core/Renderer/TAASystem.h
    Core/Renderer/TAASystem.cpp
    Core/Renderer/DynamicResolution.h
    Core/Renderer/DynamicResolution.cpp
    Core/Renderer/GPUSkinning.h
    Core/Renderer/GPUSkinning.cpp
    Core/ECS/Scene.h
    Core/ECS/Scene.cpp
    Core/ECS/Entity.h
    Core/ECS/Entity.cpp
    Core/ECS/ParallelECS.h
    Core/ECS/ParallelECS.cpp
    Core/ECS/Components/Components.h
    Core/ECS/Components/TransformComponent.h
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
    Core/UI/FontAtlas.h
    Core/UI/FontAtlas.cpp
    Core/UI/Anchoring.h
)

target_link_libraries(EngineCore PUBLIC spdlog::spdlog glm::glm Tracy::TracyClient SDL3::SDL3 Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator unofficial::shaderc::shaderc EnTT::EnTT Jolt::Jolt GameNetworkingSockets::GameNetworkingSockets httplib::httplib nlohmann_json::nlohmann_json ${LUA_LIBRARIES} imgui::imgui)
target_include_directories(EngineCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CGLTF_INCLUDE_DIRS} ${LUA_INCLUDE_DIR} ${MINIAUDIO_INCLUDE_DIRS} ${Stb_INCLUDE_DIR})

# Option to enable Tracy profiling
option(ENABLE_PROFILING "Enable Tracy Profiling" ON)
if(ENABLE_PROFILING)
    target_compile_definitions(EngineCore PUBLIC TRACY_ENABLE)
endif()

# Sandbox / Test Executable
add_executable(AIGameEngine src/main.cpp)
target_link_libraries(AIGameEngine PRIVATE EngineCore)
