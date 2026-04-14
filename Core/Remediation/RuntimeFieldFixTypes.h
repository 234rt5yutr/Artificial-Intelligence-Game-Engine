#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Core::Remediation {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class RuntimeFieldDomain : uint8_t {
    ECS = 0,
    UIBinding = 1,
    Animation = 2,
    Tooling = 3,
    Replication = 4,
    RPC = 5,
    ReplayRollback = 6,
    Store = 7,
    DedicatedServer = 8
};

enum class RuntimeFieldFixKind : uint8_t {
    BindingPathCorrection = 0,
    ReflectionRouteCorrection = 1,
    ReflectionInterfaceAliasCorrection = 2,
    ReplicationSchemaParityCorrection = 3,
    RPCPayloadParityCorrection = 4,
    ReplayRollbackSchemaParityCorrection = 5,
    FramePhaseOrderingCorrection = 6,
    JobBoundaryOrderingCorrection = 7,
    SerializationCheckpointOrderingCorrection = 8,
    StoreReleaseArtifactMetadataContractCorrection = 9,
    DedicatedServerDeploymentManifestContractCorrection = 10
};

struct RuntimeFieldFixProvenanceMetadata {
    std::string FindingId;
    std::string RuleId;
    std::string Owner;
    std::string RemediationBatchId;
};

struct RuntimeFieldFixRollbackCheckpointMetadata {
    std::string RollbackCheckpointId;
    std::string RollbackPropertyPath;
    std::string RollbackValue;
    bool RollbackRequired = true;
    std::string RollbackManifestPath;
};

struct RuntimeFieldFixEntry {
    RuntimeFieldDomain Domain = RuntimeFieldDomain::ECS;
    std::string StableFieldKey;
    std::string DomainPair;
    std::string TargetFieldId;
    std::string BindingPath;
    std::string ExpectedBindingPath;
    std::string ReflectionRoute;
    std::string ExpectedReflectionRoute;
    std::vector<std::string> InterfaceAliases;
    std::vector<std::string> ExpectedInterfaceAliases;
    std::string ReplicationAuthoritativeSchema;
    std::string ReplicationClientSchema;
    std::string RPCCanonicalPayloadSchema;
    std::string RPCRequestPayloadSchema;
    std::string RPCResponsePayloadSchema;
    std::string ReplaySchema;
    std::string RollbackSchema;
    std::string FramePhaseOrdering;
    std::string ExpectedFramePhaseOrdering;
    std::string JobBoundaryOrdering;
    std::string ExpectedJobBoundaryOrdering;
    std::string SerializationCheckpointOrdering;
    std::string ExpectedSerializationCheckpointOrdering;
    std::string StoreReleaseArtifactMetadataContract;
    std::string ExpectedStoreReleaseArtifactMetadataContract;
    std::string DedicatedServerDeploymentDescriptor;
    std::string ExpectedDedicatedServerDeploymentDescriptor;
    std::string DedicatedServerArtifactManifest;
    std::string ExpectedDedicatedServerArtifactManifest;
    RuntimeFieldFixProvenanceMetadata Provenance;
};

struct RuntimeFieldFixRequest {
    std::string Scope = "runtime-field-routes";
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::filesystem::path RollbackManifestPath;
    std::vector<RuntimeFieldFixEntry> Entries;
};

struct RuntimeFieldFixRecord {
    std::string FixId;
    RuntimeFieldDomain Domain = RuntimeFieldDomain::ECS;
    RuntimeFieldFixKind FixKind = RuntimeFieldFixKind::BindingPathCorrection;
    std::string StableFieldKey;
    std::string DomainPair;
    std::string TargetFieldId;
    std::string PropertyPath;
    std::string ExistingValue;
    std::string ReplacementValue;
    std::string Rationale;
    RuntimeFieldFixProvenanceMetadata Provenance;
    RuntimeFieldFixRollbackCheckpointMetadata Rollback;
    std::string DeterministicDigest;
};

struct RuntimeFieldFixSummary {
    uint32_t ECSFixCount = 0;
    uint32_t UIBindingFixCount = 0;
    uint32_t AnimationFixCount = 0;
    uint32_t ToolingFixCount = 0;
    uint32_t StoreFixCount = 0;
    uint32_t DedicatedServerFixCount = 0;
    uint32_t BindingPathFixCount = 0;
    uint32_t ReflectionRouteFixCount = 0;
    uint32_t ReflectionInterfaceAliasFixCount = 0;
    uint32_t ReplicationFixCount = 0;
    uint32_t RPCFixCount = 0;
    uint32_t ReplayRollbackFixCount = 0;
    uint32_t ReplicationSchemaParityFixCount = 0;
    uint32_t RPCPayloadParityFixCount = 0;
    uint32_t ReplayRollbackSchemaParityFixCount = 0;
    uint32_t FramePhaseOrderingFixCount = 0;
    uint32_t JobBoundaryOrderingFixCount = 0;
    uint32_t SerializationCheckpointOrderingFixCount = 0;
    uint32_t StoreReleaseArtifactMetadataContractFixCount = 0;
    uint32_t DedicatedServerDeploymentManifestContractFixCount = 0;
    uint32_t RollbackSafeFixCount = 0;
    uint32_t TotalFixCount = 0;
};

struct RuntimeFieldFixResult {
    std::string Scope;
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::filesystem::path RollbackManifestPath;
    std::vector<RuntimeFieldFixRecord> FixRecords;
    RuntimeFieldFixSummary Summary;
    std::string RollbackManifestDigest;
    std::string DeterministicDigest;
};

} // namespace Core::Remediation
