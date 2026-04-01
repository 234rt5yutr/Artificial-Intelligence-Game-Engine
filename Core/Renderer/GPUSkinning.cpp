#include "GPUSkinning.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/RHI/RHICommandList.h"

namespace Core {
namespace Renderer {

//=============================================================================
// Constructor / Destructor
//=============================================================================

GPUSkinningSystem::GPUSkinningSystem() = default;

GPUSkinningSystem::~GPUSkinningSystem() {
    if (m_Initialized) {
        Shutdown();
    }
}

//=============================================================================
// Initialization
//=============================================================================

void GPUSkinningSystem::Initialize(std::shared_ptr<RHI::RHIDevice> device) {
    if (m_Initialized) {
        ENGINE_CORE_WARN("GPUSkinningSystem already initialized");
        return;
    }

    m_Device = device;
    
    if (!m_Device) {
        ENGINE_CORE_ERROR("GPUSkinningSystem::Initialize - Invalid device");
        return;
    }

    CreateBuffers();
    m_Initialized = true;

    ENGINE_CORE_INFO("GPUSkinningSystem initialized (Mode: {})", 
                     SkinningModeToString(m_Config.Mode));
}

void GPUSkinningSystem::Shutdown() {
    if (!m_Initialized) {
        return;
    }

    m_Instances.clear();
    m_StagedBoneMatrices.clear();
    m_StagedBoneOffsets.clear();

    m_BoneMatrixBuffer.reset();
    m_BoneOffsetBuffer.reset();
    m_SkinnedVertexBuffer.reset();
    m_SkinningUniformBuffer.reset();

    m_Device.reset();
    m_Initialized = false;

    ENGINE_CORE_INFO("GPUSkinningSystem shutdown");
}

//=============================================================================
// Configuration
//=============================================================================

void GPUSkinningSystem::SetConfig(const GPUSkinningConfig& config) {
    m_Config = config;
    
    if (m_Initialized) {
        // May need to recreate buffers if max sizes changed
        ResizeBuffersIfNeeded();
    }
}

void GPUSkinningSystem::SetSkinningMode(SkinningMode mode) {
    if (m_Config.Mode != mode) {
        m_Config.Mode = mode;
        ENGINE_CORE_INFO("Skinning mode changed to: {}", SkinningModeToString(mode));
    }
}

//=============================================================================
// Mesh Instance Management
//=============================================================================

uint32_t GPUSkinningSystem::RegisterSkeletalMesh(const Mesh* mesh, uint32_t boneCount) {
    if (!mesh || !mesh->IsSkeletal()) {
        ENGINE_CORE_ERROR("RegisterSkeletalMesh: Invalid or non-skeletal mesh");
        return 0;
    }

    if (m_Instances.size() >= m_Config.MaxSkinnedMeshes) {
        ENGINE_CORE_ERROR("RegisterSkeletalMesh: Maximum skinned mesh count reached");
        return 0;
    }

    uint32_t instanceId = m_NextInstanceId++;

    SkinnedMeshInstance instance;
    instance.InstanceId = instanceId;
    instance.BoneOffset = m_TotalBoneCount;
    instance.VertexOffset = m_TotalVertexCount;
    instance.VertexCount = static_cast<uint32_t>(mesh->skinnedVertices.size());
    instance.BoneCount = boneCount;
    instance.Dirty = true;
    instance.SkinningMatrices.resize(boneCount, Math::Mat4(1.0f));

    m_Instances[instanceId] = std::move(instance);

    // Update totals
    m_TotalBoneCount += boneCount;
    m_TotalVertexCount += instance.VertexCount;
    m_BuffersDirty = true;

    ENGINE_CORE_TRACE("Registered skeletal mesh (ID: {}, Bones: {}, Vertices: {})",
                      instanceId, boneCount, instance.VertexCount);

    return instanceId;
}

void GPUSkinningSystem::UnregisterSkeletalMesh(uint32_t instanceId) {
    auto it = m_Instances.find(instanceId);
    if (it == m_Instances.end()) {
        ENGINE_CORE_WARN("UnregisterSkeletalMesh: Instance {} not found", instanceId);
        return;
    }

    // Note: In a production system, we'd compact the buffers here
    // For simplicity, we just remove the instance and mark buffers dirty
    m_Instances.erase(it);
    m_BuffersDirty = true;

    ENGINE_CORE_TRACE("Unregistered skeletal mesh (ID: {})", instanceId);
}

void GPUSkinningSystem::UpdateBoneMatrices(uint32_t instanceId,
                                           const std::vector<Math::Mat4>& skinningMatrices) {
    auto it = m_Instances.find(instanceId);
    if (it == m_Instances.end()) {
        ENGINE_CORE_WARN("UpdateBoneMatrices: Instance {} not found", instanceId);
        return;
    }

    SkinnedMeshInstance& instance = it->second;

    if (skinningMatrices.size() != instance.BoneCount) {
        ENGINE_CORE_WARN("UpdateBoneMatrices: Matrix count mismatch ({} vs {})",
                         skinningMatrices.size(), instance.BoneCount);
        return;
    }

    instance.SkinningMatrices = skinningMatrices;
    instance.Dirty = true;
}

void GPUSkinningSystem::UpdateFromComponent(uint32_t instanceId,
                                            const ECS::SkeletalMeshComponent& component) {
    auto it = m_Instances.find(instanceId);
    if (it == m_Instances.end()) {
        ENGINE_CORE_WARN("UpdateFromComponent: Instance {} not found", instanceId);
        return;
    }

    if (!component.IsValid() || !component.HasSkeleton()) {
        return;
    }

    SkinnedMeshInstance& instance = it->second;
    const auto& skeleton = component.MeshData->GetSkeleton();
    const auto& pose = component.CurrentPose;

    // Compute skinning matrices from pose
    ComputeSkinningMatrices(skeleton, pose, instance.SkinningMatrices);
    instance.Dirty = true;
}

bool GPUSkinningSystem::HasInstance(uint32_t instanceId) const {
    return m_Instances.find(instanceId) != m_Instances.end();
}

const SkinnedMeshInstance* GPUSkinningSystem::GetInstance(uint32_t instanceId) const {
    auto it = m_Instances.find(instanceId);
    return (it != m_Instances.end()) ? &it->second : nullptr;
}

//=============================================================================
// Frame Update
//=============================================================================

void GPUSkinningSystem::UploadBoneMatrices() {
    PROFILE_FUNCTION();

    if (!m_Initialized || m_Instances.empty()) {
        return;
    }

    // Resize buffers if needed
    ResizeBuffersIfNeeded();

    // Check if any instance is dirty
    bool anyDirty = false;
    for (const auto& [id, instance] : m_Instances) {
        if (instance.Dirty) {
            anyDirty = true;
            break;
        }
    }

    if (!anyDirty) {
        return;
    }

    // Update staged data
    UpdateBoneMatrixBuffer();
    UpdateBoneOffsetBuffer();

    // Upload to GPU
    if (m_BoneMatrixBuffer && !m_StagedBoneMatrices.empty()) {
        void* mappedData = nullptr;
        m_BoneMatrixBuffer->Map(&mappedData);
        if (mappedData) {
            std::memcpy(mappedData, m_StagedBoneMatrices.data(),
                       m_StagedBoneMatrices.size() * sizeof(Math::Mat4));
            m_BoneMatrixBuffer->Unmap();
        }
    }

    if (m_BoneOffsetBuffer && !m_StagedBoneOffsets.empty()) {
        void* mappedData = nullptr;
        m_BoneOffsetBuffer->Map(&mappedData);
        if (mappedData) {
            std::memcpy(mappedData, m_StagedBoneOffsets.data(),
                       m_StagedBoneOffsets.size() * sizeof(uint32_t));
            m_BoneOffsetBuffer->Unmap();
        }
    }

    // Clear dirty flags
    for (auto& [id, instance] : m_Instances) {
        instance.Dirty = false;
    }

    // Update stats
    m_Stats.TotalSkinnedMeshes = static_cast<uint32_t>(m_Instances.size());
    m_Stats.TotalBoneMatrices = static_cast<uint32_t>(m_StagedBoneMatrices.size());
}

void GPUSkinningSystem::ExecuteSkinning(std::shared_ptr<RHI::RHICommandList> commandList) {
    PROFILE_FUNCTION();

    if (!m_Initialized || m_Config.Mode != SkinningMode::ComputeShader) {
        return;
    }

    if (!commandList) {
        ENGINE_CORE_ERROR("ExecuteSkinning: Invalid command list");
        return;
    }

    // Dispatch compute shader for each mesh instance
    for (const auto& [id, instance] : m_Instances) {
        uint32_t workgroupCount = (instance.VertexCount + SKINNING_WORKGROUP_SIZE - 1) 
                                  / SKINNING_WORKGROUP_SIZE;
        
        // In a full implementation, we would:
        // 1. Update uniforms with vertex count, bone offset
        // 2. Bind descriptor sets
        // 3. Dispatch compute shader
        commandList->Dispatch(workgroupCount, 1, 1);
    }
}

void GPUSkinningSystem::PrepareForRendering() {
    PROFILE_FUNCTION();

    // Called before rendering skeletal meshes
    // Upload bone matrices if needed
    UploadBoneMatrices();
}

//=============================================================================
// Buffer Access
//=============================================================================

SkinningPushConstants GPUSkinningSystem::GetPushConstants(uint32_t instanceId) const {
    SkinningPushConstants pc;

    auto it = m_Instances.find(instanceId);
    if (it != m_Instances.end()) {
        pc.BoneOffset = it->second.BoneOffset;
        pc.InstanceId = instanceId;
        pc.Flags = 0;
    }

    return pc;
}

//=============================================================================
// Private Methods
//=============================================================================

void GPUSkinningSystem::CreateBuffers() {
    if (!m_Device) {
        return;
    }

    // Initial buffer sizes (will grow as needed)
    uint32_t initialBoneCount = MAX_BONES * 16;  // Space for 16 meshes initially
    uint32_t initialVertexCount = 100000;

    // Bone matrix buffer (SSBO for shader access)
    RHI::BufferDescriptor boneBufferDesc;
    boneBufferDesc.size = initialBoneCount * sizeof(Math::Mat4);
    boneBufferDesc.usage = RHI::BufferUsage::Storage;
    m_BoneMatrixBuffer = m_Device->CreateBuffer(boneBufferDesc);

    // Bone offset buffer (for instanced rendering)
    RHI::BufferDescriptor offsetBufferDesc;
    offsetBufferDesc.size = m_Config.MaxSkinnedMeshes * sizeof(uint32_t);
    offsetBufferDesc.usage = RHI::BufferUsage::Storage;
    m_BoneOffsetBuffer = m_Device->CreateBuffer(offsetBufferDesc);

    // Skinned vertex output buffer (compute shader mode)
    if (m_Config.Mode == SkinningMode::ComputeShader) {
        // Size for output vertices (position, normal, texcoord, tangent)
        RHI::BufferDescriptor vertexBufferDesc;
        vertexBufferDesc.size = initialVertexCount * sizeof(Vertex);
        vertexBufferDesc.usage = RHI::BufferUsage::Storage;
        m_SkinnedVertexBuffer = m_Device->CreateBuffer(vertexBufferDesc);
    }

    // Uniform buffer for compute shader
    RHI::BufferDescriptor uniformBufferDesc;
    uniformBufferDesc.size = sizeof(SkinningUniforms);
    uniformBufferDesc.usage = RHI::BufferUsage::Uniform;
    m_SkinningUniformBuffer = m_Device->CreateBuffer(uniformBufferDesc);

    // Reserve staging vectors
    m_StagedBoneMatrices.reserve(initialBoneCount);
    m_StagedBoneOffsets.reserve(m_Config.MaxSkinnedMeshes);

    ENGINE_CORE_TRACE("Created GPU skinning buffers (Bones: {}, Vertices: {})",
                      initialBoneCount, initialVertexCount);
}

void GPUSkinningSystem::ResizeBuffersIfNeeded() {
    if (!m_Device || !m_BuffersDirty) {
        return;
    }

    // Check if bone buffer needs resizing
    size_t requiredBoneSize = m_TotalBoneCount * sizeof(Math::Mat4);
    if (m_BoneMatrixBuffer && m_BoneMatrixBuffer->GetSize() < requiredBoneSize) {
        // Resize with some headroom
        RHI::BufferDescriptor desc;
        desc.size = requiredBoneSize * 2;
        desc.usage = RHI::BufferUsage::Storage;
        m_BoneMatrixBuffer = m_Device->CreateBuffer(desc);
        
        ENGINE_CORE_TRACE("Resized bone matrix buffer to {} bytes", desc.size);
    }

    m_BuffersDirty = false;
}

void GPUSkinningSystem::UpdateBoneMatrixBuffer() {
    // Rebuild the complete bone matrix buffer
    m_StagedBoneMatrices.clear();
    m_StagedBoneMatrices.reserve(m_TotalBoneCount);

    for (auto& [id, instance] : m_Instances) {
        // Update the instance's bone offset
        instance.BoneOffset = static_cast<uint32_t>(m_StagedBoneMatrices.size());

        // Add all bone matrices for this instance
        for (const auto& matrix : instance.SkinningMatrices) {
            m_StagedBoneMatrices.push_back(matrix);
        }
    }

    m_Stats.TotalBones = static_cast<uint32_t>(m_StagedBoneMatrices.size());
}

void GPUSkinningSystem::UpdateBoneOffsetBuffer() {
    m_StagedBoneOffsets.clear();
    m_StagedBoneOffsets.reserve(m_Instances.size());

    for (const auto& [id, instance] : m_Instances) {
        m_StagedBoneOffsets.push_back(instance.BoneOffset);
    }
}

void GPUSkinningSystem::ComputeSkinningMatrices(const Skeleton& skeleton,
                                                const ECS::SkeletonPose& pose,
                                                std::vector<Math::Mat4>& outMatrices) {
    uint32_t boneCount = skeleton.GetBoneCount();
    
    if (outMatrices.size() != boneCount) {
        outMatrices.resize(boneCount);
    }

    if (pose.GlobalPoses.size() != boneCount || pose.SkinningMatrices.size() != boneCount) {
        // Pose not properly initialized - use identity
        std::fill(outMatrices.begin(), outMatrices.end(), Math::Mat4(1.0f));
        return;
    }

    // The skinning matrix is: globalBoneTransform * inverseBindMatrix
    // This transforms vertices from bind pose to current pose
    for (uint32_t i = 0; i < boneCount; ++i) {
        outMatrices[i] = pose.GlobalPoses[i] * skeleton.Bones[i].InverseBindMatrix;
    }
}

} // namespace Renderer
} // namespace Core
