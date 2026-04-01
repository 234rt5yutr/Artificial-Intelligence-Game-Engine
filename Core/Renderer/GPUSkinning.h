#pragma once

// GPUSkinning.h
// GPU-based skeletal mesh skinning system
// Supports both compute shader and vertex shader SSBO approaches

#include "Core/Math/Math.h"
#include "Core/RHI/RHIBuffer.h"
#include "Core/RHI/RHIDevice.h"
#include "Core/Renderer/Mesh.h"
#include "Core/ECS/Components/SkeletalMeshComponent.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace Core {
namespace Renderer {

    //=========================================================================
    // Constants
    //=========================================================================

    constexpr uint32_t SKINNING_WORKGROUP_SIZE = 64;
    constexpr uint32_t MAX_SKINNED_MESHES = 1024;
    constexpr uint32_t MAX_TOTAL_BONES = MAX_BONES * MAX_SKINNED_MESHES;

    //=========================================================================
    // Skinning Mode
    //=========================================================================

    enum class SkinningMode : uint8_t {
        ComputeShader = 0,  // Separate compute pass, outputs to vertex buffer
        VertexShaderSSBO,   // Skinning in vertex shader via SSBO
        CPU                 // CPU fallback (not recommended)
    };

    //=========================================================================
    // GPU-aligned Structures (must match shaders)
    //=========================================================================

    // Skinning uniform data for compute shader
    struct alignas(16) SkinningUniforms {
        uint32_t VertexCount;
        uint32_t BoneCount;
        uint32_t MeshInstanceId;
        uint32_t Padding;

        SkinningUniforms()
            : VertexCount(0), BoneCount(0), MeshInstanceId(0), Padding(0) {}
    };
    static_assert(sizeof(SkinningUniforms) == 16, "SkinningUniforms must be 16 bytes");

    // Push constants for vertex shader skinning
    struct SkinningPushConstants {
        uint32_t BoneOffset;     // Offset into global bone matrix buffer
        uint32_t InstanceId;     // For instanced rendering
        uint32_t Flags;          // Bit 0: use bone offsets buffer
        uint32_t Padding;

        SkinningPushConstants()
            : BoneOffset(0), InstanceId(0), Flags(0), Padding(0) {}
    };
    static_assert(sizeof(SkinningPushConstants) == 16, "SkinningPushConstants must be 16 bytes");

    //=========================================================================
    // Skinned Mesh Instance Data
    //=========================================================================

    struct SkinnedMeshInstance {
        uint32_t InstanceId;
        uint32_t BoneOffset;           // Offset into global bone buffer
        uint32_t VertexOffset;         // Offset into skinned vertex buffer
        uint32_t VertexCount;
        uint32_t BoneCount;
        bool Dirty;                    // Needs bone matrix update

        // Cached bone matrices (computed each frame)
        std::vector<Math::Mat4> SkinningMatrices;

        SkinnedMeshInstance()
            : InstanceId(0), BoneOffset(0), VertexOffset(0)
            , VertexCount(0), BoneCount(0), Dirty(true) {}
    };

    //=========================================================================
    // Skinning Statistics
    //=========================================================================

    struct SkinningStats {
        uint32_t TotalSkinnedMeshes;
        uint32_t TotalVertices;
        uint32_t TotalBones;
        uint32_t TotalBoneMatrices;
        double SkinningTimeMs;

        SkinningStats()
            : TotalSkinnedMeshes(0), TotalVertices(0)
            , TotalBones(0), TotalBoneMatrices(0)
            , SkinningTimeMs(0.0) {}

        void Reset() {
            TotalSkinnedMeshes = 0;
            TotalVertices = 0;
            TotalBones = 0;
            TotalBoneMatrices = 0;
            SkinningTimeMs = 0.0;
        }
    };

    //=========================================================================
    // Configuration
    //=========================================================================

    struct GPUSkinningConfig {
        SkinningMode Mode = SkinningMode::VertexShaderSSBO;
        bool EnableDualQuaternionSkinning = false;  // Future: DQS for better rotation
        bool EnableMorphTargets = false;            // Future: Blend shapes
        uint32_t MaxSkinnedMeshes = MAX_SKINNED_MESHES;
    };

    //=========================================================================
    // GPU Skinning System
    //=========================================================================

    class GPUSkinningSystem {
    public:
        GPUSkinningSystem();
        ~GPUSkinningSystem();

        // Non-copyable
        GPUSkinningSystem(const GPUSkinningSystem&) = delete;
        GPUSkinningSystem& operator=(const GPUSkinningSystem&) = delete;

        //---------------------------------------------------------------------
        // Initialization
        //---------------------------------------------------------------------

        void Initialize(std::shared_ptr<RHI::RHIDevice> device);
        void Shutdown();
        bool IsInitialized() const { return m_Initialized; }

        //---------------------------------------------------------------------
        // Configuration
        //---------------------------------------------------------------------

        void SetConfig(const GPUSkinningConfig& config);
        const GPUSkinningConfig& GetConfig() const { return m_Config; }
        void SetSkinningMode(SkinningMode mode);
        SkinningMode GetSkinningMode() const { return m_Config.Mode; }

        //---------------------------------------------------------------------
        // Mesh Instance Management
        //---------------------------------------------------------------------

        // Register a skeletal mesh for GPU skinning
        uint32_t RegisterSkeletalMesh(const Mesh* mesh, uint32_t boneCount);

        // Unregister a skeletal mesh
        void UnregisterSkeletalMesh(uint32_t instanceId);

        // Update bone matrices for a mesh instance
        void UpdateBoneMatrices(uint32_t instanceId, 
                               const std::vector<Math::Mat4>& skinningMatrices);

        // Update from SkeletalMeshComponent directly
        void UpdateFromComponent(uint32_t instanceId, 
                                const ECS::SkeletalMeshComponent& component);

        // Check if instance exists
        bool HasInstance(uint32_t instanceId) const;

        // Get instance data
        const SkinnedMeshInstance* GetInstance(uint32_t instanceId) const;

        //---------------------------------------------------------------------
        // Frame Update
        //---------------------------------------------------------------------

        // Upload all dirty bone matrices to GPU
        void UploadBoneMatrices();

        // Execute GPU skinning (compute shader mode only)
        void ExecuteSkinning(std::shared_ptr<RHI::RHICommandList> commandList);

        // Prepare for rendering (call before draw calls)
        void PrepareForRendering();

        //---------------------------------------------------------------------
        // Buffer Access
        //---------------------------------------------------------------------

        // Get global bone matrix buffer (for vertex shader SSBO binding)
        std::shared_ptr<RHI::RHIBuffer> GetBoneMatrixBuffer() const { 
            return m_BoneMatrixBuffer; 
        }

        // Get bone offset buffer (for instanced rendering)
        std::shared_ptr<RHI::RHIBuffer> GetBoneOffsetBuffer() const { 
            return m_BoneOffsetBuffer; 
        }

        // Get skinned vertex output buffer (compute shader mode)
        std::shared_ptr<RHI::RHIBuffer> GetSkinnedVertexBuffer() const { 
            return m_SkinnedVertexBuffer; 
        }

        // Get push constants for a specific instance
        SkinningPushConstants GetPushConstants(uint32_t instanceId) const;

        //---------------------------------------------------------------------
        // Statistics
        //---------------------------------------------------------------------

        const SkinningStats& GetStats() const { return m_Stats; }

        //---------------------------------------------------------------------
        // Debug
        //---------------------------------------------------------------------

        // Get all registered instances
        const std::unordered_map<uint32_t, SkinnedMeshInstance>& GetInstances() const {
            return m_Instances;
        }

    private:
        void CreateBuffers();
        void ResizeBuffersIfNeeded();
        void UpdateBoneMatrixBuffer();
        void UpdateBoneOffsetBuffer();

        // Compute skeleton pose to final skinning matrices
        void ComputeSkinningMatrices(const Skeleton& skeleton,
                                     const ECS::SkeletonPose& pose,
                                     std::vector<Math::Mat4>& outMatrices);

    private:
        std::shared_ptr<RHI::RHIDevice> m_Device;
        GPUSkinningConfig m_Config;
        SkinningStats m_Stats;
        bool m_Initialized = false;

        // Instance management
        std::unordered_map<uint32_t, SkinnedMeshInstance> m_Instances;
        uint32_t m_NextInstanceId = 1;

        // Buffer allocation tracking
        uint32_t m_TotalBoneCount = 0;
        uint32_t m_TotalVertexCount = 0;
        bool m_BuffersDirty = true;

        // GPU Buffers
        std::shared_ptr<RHI::RHIBuffer> m_BoneMatrixBuffer;      // All bone matrices
        std::shared_ptr<RHI::RHIBuffer> m_BoneOffsetBuffer;      // Per-instance offsets
        std::shared_ptr<RHI::RHIBuffer> m_SkinnedVertexBuffer;   // Output (compute mode)
        std::shared_ptr<RHI::RHIBuffer> m_SkinningUniformBuffer; // Compute uniforms

        // Staging data
        std::vector<Math::Mat4> m_StagedBoneMatrices;
        std::vector<uint32_t> m_StagedBoneOffsets;
    };

    //=========================================================================
    // Utility Functions
    //=========================================================================

    // Compute final skinning matrix from animation pose
    inline Math::Mat4 ComputeSkinningMatrix(const Math::Mat4& globalBoneTransform,
                                            const Math::Mat4& inverseBindMatrix) {
        return globalBoneTransform * inverseBindMatrix;
    }

    // Convert skinning mode to string
    inline const char* SkinningModeToString(SkinningMode mode) {
        switch (mode) {
            case SkinningMode::ComputeShader:    return "Compute Shader";
            case SkinningMode::VertexShaderSSBO: return "Vertex Shader SSBO";
            case SkinningMode::CPU:              return "CPU";
            default: return "Unknown";
        }
    }

} // namespace Renderer
} // namespace Core
