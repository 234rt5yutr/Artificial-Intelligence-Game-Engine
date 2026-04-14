#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Core::Remediation {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class FieldClosureSeverity : uint8_t {
    Info = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Critical = 4
};

enum class FieldClosureDiffKind : uint8_t {
    Resolved = 0,
    Regressed = 1,
    New = 2,
    Unchanged = 3
};

enum class FieldClosureGateDecision : uint8_t {
    NotEvaluated = 0,
    Pass = 1,
    Block = 2
};

struct FieldClosureEvidenceMetadata {
    std::string EvidenceId;
    std::string EvidencePath;
    std::string EvidenceDigest;
};

struct FieldClosureOwnershipMetadata {
    std::string OwnerSubsystem;
    std::string OwnerTeam;
};

struct FieldClosureFinding {
    std::string FindingId;
    std::string RuleId;
    std::string StableFieldKey;
    std::string DomainPair;
    FieldClosureSeverity Severity = FieldClosureSeverity::Medium;
    FieldClosureOwnershipMetadata Ownership;
    std::vector<FieldClosureEvidenceMetadata> Evidence;
};

struct FieldClosureRequest {
    std::string Scope = "rerun-diff-baseline";
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::string BaselineRevision;
    std::string CurrentRevision;
    std::vector<std::string> BaselineCheckpointIds;
    std::vector<FieldClosureFinding> BaselineFindings;
    std::vector<FieldClosureFinding> RerunFindings;
    std::string SignoffApprover;
    std::string TargetContractVersion;
    std::vector<std::string> PolicyCheckpointIds;
};

struct FieldClosureDiffRecord {
    std::string DiffId;
    FieldClosureDiffKind DiffKind = FieldClosureDiffKind::Unchanged;
    std::string FindingId;
    std::string RuleId;
    std::string StableFieldKey;
    std::string DomainPair;
    FieldClosureSeverity BaselineSeverity = FieldClosureSeverity::Info;
    FieldClosureSeverity CurrentSeverity = FieldClosureSeverity::Info;
    FieldClosureOwnershipMetadata Ownership;
    std::string DiffSummary;
    std::vector<std::string> ActionableDiff;
    std::vector<FieldClosureEvidenceMetadata> EvidenceIndex;
    std::string DeterministicDigest;
};

struct FieldClosureSummary {
    uint32_t BaselineFindingCount = 0;
    uint32_t RerunFindingCount = 0;
    uint32_t ResolvedFindingCount = 0;
    uint32_t RegressedFindingCount = 0;
    uint32_t NewFindingCount = 0;
    uint32_t UnchangedFindingCount = 0;
    uint32_t TotalDiffCount = 0;
    uint32_t BaselineCheckpointCount = 0;
    uint32_t CriticalFindingCount = 0;
    FieldClosureGateDecision GateDecision = FieldClosureGateDecision::NotEvaluated;
    bool ContractVersionFrozen = false;
};

struct FieldClosureResult {
    std::string Scope;
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::string BaselineRevision;
    std::string CurrentRevision;
    std::vector<std::string> BaselineCheckpointIds;
    std::string SignoffApprover;
    std::string ContractVersion;
    std::vector<std::string> PolicyCheckpointIds;
    std::vector<FieldClosureDiffRecord> DiffRecords;
    FieldClosureSummary Summary;
    std::string SignoffReportDigest;
    std::string FreezeManifestDigest;
    std::string DeterministicDigest;
};

} // namespace Core::Remediation
