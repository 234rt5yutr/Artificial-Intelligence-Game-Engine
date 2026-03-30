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
    Core/RHI/Vulkan/VulkanBuffer.h
    Core/RHI/Vulkan/VulkanBuffer.cpp
    Core/RHI/Vulkan/VulkanContext.h
    Core/RHI/Vulkan/VulkanContext.cpp
    Core/RHI/Vulkan/VulkanPipelineState.h
    Core/RHI/Vulkan/VulkanPipelineState.cpp
    Core/Renderer/ForwardPlus.h
    Core/Renderer/ForwardPlus.cpp
)

target_link_libraries(EngineCore PUBLIC spdlog::spdlog glm::glm Tracy::TracyClient SDL3::SDL3 Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator unofficial::shaderc::shaderc)
target_include_directories(EngineCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Option to enable Tracy profiling
option(ENABLE_PROFILING "Enable Tracy Profiling" ON)
if(ENABLE_PROFILING)
    target_compile_definitions(EngineCore PUBLIC TRACY_ENABLE)
endif()

# Sandbox / Test Executable
add_executable(AIGameEngine src/main.cpp)
target_link_libraries(AIGameEngine PRIVATE EngineCore)
