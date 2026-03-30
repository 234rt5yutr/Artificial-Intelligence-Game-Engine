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

# Core Engine Library
add_library(EngineCore
    Core/Log.cpp
    Core/Log.h
    Core/Math/Math.h
    Core/Memory/Allocator.h
    Core/Memory/LinearAllocator.cpp
    Core/Memory/LinearAllocator.h
    Core/Memory/PoolAllocator.cpp
    Core/Memory/PoolAllocator.h
    Core/JobSystem/JobSystem.h
    Core/JobSystem/JobSystem.cpp
)

target_link_libraries(EngineCore PRIVATE spdlog::spdlog glm::glm)
target_include_directories(EngineCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Sandbox / Test Executable
add_executable(AIGameEngine src/main.cpp)
target_link_libraries(AIGameEngine PRIVATE EngineCore)
