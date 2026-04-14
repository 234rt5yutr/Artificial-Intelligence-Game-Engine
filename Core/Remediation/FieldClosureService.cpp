#include "Core/Remediation/FieldClosureService.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Core::Remediation {
namespace {

[[nodiscard]] uint64_t HashString(const std::string_view value) {
    constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;

    uint64_t hash = kFnvOffset;
    for (const unsigned char symbol : value) {
        hash ^= static_cast<uint64_t>(symbol);
        hash *= kFnvPrime;
    }
    return hash;
}

[[nodiscard]] std::string HashToHex(const uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

[[nodiscard]] bool EnsureOutputDirectory(const std::filesystem::path& outputDirectory) {
    std::error_code errorCode;
    const bool outputExists = std::filesystem::exists(outputDirectory, errorCode);
    if (errorCode) {
        return false;
    }

    if (outputExists) {
        const bool isDirectory = std::filesystem::is_directory(outputDirectory, errorCode);
        return !errorCode && isDirectory;
    }

    std::filesystem::create_directories(outputDirectory, errorCode);
    return !errorCode;
}

[[nodiscard]] std::vector<std::string> NormalizeTokenList(const std::vector<std::string>& values) {
    std::set<std::string> normalized;
    for (const std::string& value : values) {
        if (!value.empty()) {
            normalized.emplace(value);
        }
    }
    return {normalized.begin(), normalized.end()};
}

[[nodiscard]] bool IsValidEvidence(const FieldClosureEvidenceMetadata& evidence) {
    return !evidence.EvidenceId.empty() && !evidence.EvidencePath.empty() && !evidence.EvidenceDigest.empty();
}

[[nodiscard]] std::vector<FieldClosureEvidenceMetadata> NormalizeEvidence(
    const std::vector<FieldClosureEvidenceMetadata>& evidenceList) {
    std::vector<FieldClosureEvidenceMetadata> normalized;
    normalized.reserve(evidenceList.size());
    for (const FieldClosureEvidenceMetadata& evidence : evidenceList) {
        if (IsValidEvidence(evidence)) {
            normalized.push_back(evidence);
        }
    }

    std::sort(normalized.begin(),
              normalized.end(),
              [](const FieldClosureEvidenceMetadata& left, const FieldClosureEvidenceMetadata& right) {
                  if (left.EvidenceId != right.EvidenceId) {
                      return left.EvidenceId < right.EvidenceId;
                  }
                  if (left.EvidencePath != right.EvidencePath) {
                      return left.EvidencePath < right.EvidencePath;
                  }
                  return left.EvidenceDigest < right.EvidenceDigest;
              });

    normalized.erase(std::unique(normalized.begin(),
                                 normalized.end(),
                                 [](const FieldClosureEvidenceMetadata& left, const FieldClosureEvidenceMetadata& right) {
                                     return left.EvidenceId == right.EvidenceId &&
                                            left.EvidencePath == right.EvidencePath &&
                                            left.EvidenceDigest == right.EvidenceDigest;
                                 }),
                     normalized.end());
    return normalized;
}

[[nodiscard]] bool IsValidFinding(const FieldClosureFinding& finding) {
    if (finding.FindingId.empty() || finding.RuleId.empty() || finding.StableFieldKey.empty() ||
        finding.DomainPair.empty() || finding.Ownership.OwnerSubsystem.empty()) {
        return false;
    }

    const std::vector<FieldClosureEvidenceMetadata> normalizedEvidence = NormalizeEvidence(finding.Evidence);
    return !normalizedEvidence.empty();
}

[[nodiscard]] std::string BuildFindingKey(const FieldClosureFinding& finding) {
    std::string key;
    key.reserve(256u);
    key.append(finding.RuleId);
    key.push_back('|');
    key.append(finding.StableFieldKey);
    key.push_back('|');
    key.append(finding.DomainPair);
    return key;
}

void MergeEvidence(std::vector<FieldClosureEvidenceMetadata>& destination,
                   const std::vector<FieldClosureEvidenceMetadata>& source) {
    destination.insert(destination.end(), source.begin(), source.end());
    destination = NormalizeEvidence(destination);
}

[[nodiscard]] std::map<std::string, FieldClosureFinding> BuildFindingMap(
    const std::vector<FieldClosureFinding>& findings) {
    std::map<std::string, FieldClosureFinding> findingMap;
    for (FieldClosureFinding finding : findings) {
        finding.Evidence = NormalizeEvidence(finding.Evidence);
        if (finding.Ownership.OwnerTeam.empty()) {
            finding.Ownership.OwnerTeam = "<unspecified>";
        }

        const std::string findingKey = BuildFindingKey(finding);
        auto [it, inserted] = findingMap.try_emplace(findingKey, finding);
        if (inserted) {
            continue;
        }

        FieldClosureFinding& existing = it->second;
        std::vector<FieldClosureEvidenceMetadata> mergedEvidence = existing.Evidence;
        MergeEvidence(mergedEvidence, finding.Evidence);

        const bool preferIncoming = static_cast<uint32_t>(finding.Severity) > static_cast<uint32_t>(existing.Severity) ||
                                    (finding.Severity == existing.Severity && finding.FindingId < existing.FindingId);
        if (preferIncoming) {
            existing = finding;
        }

        existing.Evidence = std::move(mergedEvidence);
    }
    return findingMap;
}

[[nodiscard]] std::string SeverityToString(const FieldClosureSeverity severity) {
    switch (severity) {
    case FieldClosureSeverity::Info:
        return "info";
    case FieldClosureSeverity::Low:
        return "low";
    case FieldClosureSeverity::Medium:
        return "medium";
    case FieldClosureSeverity::High:
        return "high";
    case FieldClosureSeverity::Critical:
        return "critical";
    default:
        return "unknown";
    }
}

[[nodiscard]] std::vector<std::string> BuildActionableDiff(const FieldClosureDiffRecord& record) {
    std::vector<std::string> actionableDiff;
    if (record.DiffKind == FieldClosureDiffKind::Resolved) {
        actionableDiff.push_back("- finding removed from rerun baseline: " + record.FindingId);
        actionableDiff.push_back("verify closure evidence for owner subsystem: " + record.Ownership.OwnerSubsystem);
        return actionableDiff;
    }

    if (record.DiffKind == FieldClosureDiffKind::Regressed) {
        actionableDiff.push_back("~ severity increased from " + SeverityToString(record.BaselineSeverity) + " to " +
                                 SeverityToString(record.CurrentSeverity));
        actionableDiff.push_back("triage with owner subsystem: " + record.Ownership.OwnerSubsystem);
        return actionableDiff;
    }

    if (record.DiffKind == FieldClosureDiffKind::New) {
        actionableDiff.push_back("+ new finding introduced in rerun: " + record.FindingId);
        actionableDiff.push_back("assign remediation task to owner subsystem: " + record.Ownership.OwnerSubsystem);
        return actionableDiff;
    }

    actionableDiff.push_back("= finding remains open at severity: " + SeverityToString(record.CurrentSeverity));
    actionableDiff.push_back("maintain remediation ownership: " + record.Ownership.OwnerSubsystem);
    return actionableDiff;
}

[[nodiscard]] std::string BuildDiffSummary(const FieldClosureDiffRecord& record) {
    if (record.DiffKind == FieldClosureDiffKind::Resolved) {
        return "finding resolved compared to baseline checkpoint set";
    }
    if (record.DiffKind == FieldClosureDiffKind::Regressed) {
        return "finding regressed with increased severity compared to baseline";
    }
    if (record.DiffKind == FieldClosureDiffKind::New) {
        return "new finding introduced in rerun against baseline";
    }
    return "finding unchanged across baseline and rerun";
}

[[nodiscard]] std::string BuildDiffId(const FieldClosureDiffRecord& record) {
    std::string idMaterial;
    idMaterial.reserve(320u);
    idMaterial.append("field-closure-diff|");
    idMaterial.append(record.RuleId);
    idMaterial.push_back('|');
    idMaterial.append(record.StableFieldKey);
    idMaterial.push_back('|');
    idMaterial.append(record.DomainPair);
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.DiffKind)));
    idMaterial.push_back('|');
    idMaterial.append(record.FindingId);
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.BaselineSeverity)));
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.CurrentSeverity)));
    return HashToHex(HashString(idMaterial));
}

[[nodiscard]] std::string ComputeDiffDigest(const FieldClosureDiffRecord& record) {
    std::string digestMaterial;
    digestMaterial.reserve((record.EvidenceIndex.size() * 96u) + (record.ActionableDiff.size() * 64u) + 384u);
    digestMaterial.append(record.DiffId);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.DiffKind)));
    digestMaterial.push_back('|');
    digestMaterial.append(record.FindingId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.RuleId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.StableFieldKey);
    digestMaterial.push_back('|');
    digestMaterial.append(record.DomainPair);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.BaselineSeverity)));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.CurrentSeverity)));
    digestMaterial.push_back('|');
    digestMaterial.append(record.Ownership.OwnerSubsystem);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Ownership.OwnerTeam);
    digestMaterial.push_back('|');
    digestMaterial.append(record.DiffSummary);
    digestMaterial.push_back('\n');

    for (const std::string& diffLine : record.ActionableDiff) {
        digestMaterial.append(diffLine);
        digestMaterial.push_back('\n');
    }

    for (const FieldClosureEvidenceMetadata& evidence : record.EvidenceIndex) {
        digestMaterial.append(evidence.EvidenceId);
        digestMaterial.push_back('|');
        digestMaterial.append(evidence.EvidencePath);
        digestMaterial.push_back('|');
        digestMaterial.append(evidence.EvidenceDigest);
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputeResultDigest(const FieldClosureResult& result) {
    std::string digestMaterial;
    digestMaterial.reserve((result.DiffRecords.size() * 96u) + 320u);
    digestMaterial.append(result.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RemediationBatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(result.BaselineRevision);
    digestMaterial.push_back('|');
    digestMaterial.append(result.CurrentRevision);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.BaselineFindingCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.RerunFindingCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ResolvedFindingCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.RegressedFindingCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.NewFindingCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.UnchangedFindingCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.TotalDiffCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.BaselineCheckpointCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.CriticalFindingCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(result.Summary.GateDecision)));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ContractVersionFrozen ? 1u : 0u));
    digestMaterial.push_back('|');
    digestMaterial.append(result.SignoffApprover);
    digestMaterial.push_back('|');
    digestMaterial.append(result.ContractVersion);
    digestMaterial.push_back('|');
    digestMaterial.append(result.SignoffReportDigest);
    digestMaterial.push_back('|');
    digestMaterial.append(result.FreezeManifestDigest);
    digestMaterial.push_back('\n');

    for (const std::string& checkpointId : result.BaselineCheckpointIds) {
        digestMaterial.append(checkpointId);
        digestMaterial.push_back('\n');
    }

    for (const std::string& policyCheckpointId : result.PolicyCheckpointIds) {
        digestMaterial.append(policyCheckpointId);
        digestMaterial.push_back('\n');
    }

    for (const FieldClosureDiffRecord& record : result.DiffRecords) {
        digestMaterial.append(record.DiffId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.DeterministicDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(record.FindingId);
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputeSignoffReportDigest(const FieldClosureResult& result) {
    std::string digestMaterial;
    digestMaterial.reserve((result.DiffRecords.size() * 160u) + 320u);
    digestMaterial.append(result.RemediationBatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(result.BaselineRevision);
    digestMaterial.push_back('|');
    digestMaterial.append(result.CurrentRevision);
    digestMaterial.push_back('|');
    digestMaterial.append(result.SignoffApprover);
    digestMaterial.push_back('\n');

    for (const FieldClosureDiffRecord& record : result.DiffRecords) {
        digestMaterial.append(record.DiffId);
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(static_cast<uint32_t>(record.DiffKind)));
        digestMaterial.push_back('|');
        digestMaterial.append(record.FindingId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.RuleId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.Ownership.OwnerSubsystem);
        digestMaterial.push_back('|');
        digestMaterial.append(record.Ownership.OwnerTeam);
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(static_cast<uint32_t>(record.BaselineSeverity)));
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(static_cast<uint32_t>(record.CurrentSeverity)));
        digestMaterial.push_back('\n');

        for (const FieldClosureEvidenceMetadata& evidence : record.EvidenceIndex) {
            digestMaterial.append(evidence.EvidenceId);
            digestMaterial.push_back('|');
            digestMaterial.append(evidence.EvidencePath);
            digestMaterial.push_back('|');
            digestMaterial.append(evidence.EvidenceDigest);
            digestMaterial.push_back('\n');
        }
    }
    return HashToHex(HashString(digestMaterial));
}

void SortDiffRecords(std::vector<FieldClosureDiffRecord>& records) {
    std::sort(records.begin(), records.end(), [](const FieldClosureDiffRecord& left, const FieldClosureDiffRecord& right) {
        if (left.DiffKind != right.DiffKind) {
            return static_cast<uint32_t>(left.DiffKind) < static_cast<uint32_t>(right.DiffKind);
        }
        if (left.RuleId != right.RuleId) {
            return left.RuleId < right.RuleId;
        }
        if (left.StableFieldKey != right.StableFieldKey) {
            return left.StableFieldKey < right.StableFieldKey;
        }
        if (left.DomainPair != right.DomainPair) {
            return left.DomainPair < right.DomainPair;
        }
        return left.FindingId < right.FindingId;
    });
}

[[nodiscard]] Result<FieldClosureResult> BuildClosureDiffResult(const FieldClosureRequest& request,
                                                                const std::string_view expectedScope) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.BaselineRevision.empty() || request.CurrentRevision.empty() || request.BaselineCheckpointIds.empty()) {
        return Result<FieldClosureResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != expectedScope) {
        return Result<FieldClosureResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    if (request.BaselineFindings.empty() && request.RerunFindings.empty()) {
        return Result<FieldClosureResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    for (const FieldClosureFinding& finding : request.BaselineFindings) {
        if (!IsValidFinding(finding)) {
            return Result<FieldClosureResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
    }
    for (const FieldClosureFinding& finding : request.RerunFindings) {
        if (!IsValidFinding(finding)) {
            return Result<FieldClosureResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldClosureResult>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    const std::vector<std::string> baselineCheckpointIds = NormalizeTokenList(request.BaselineCheckpointIds);
    if (baselineCheckpointIds.empty()) {
        return Result<FieldClosureResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    const std::map<std::string, FieldClosureFinding> baselineMap = BuildFindingMap(request.BaselineFindings);
    const std::map<std::string, FieldClosureFinding> rerunMap = BuildFindingMap(request.RerunFindings);

    std::set<std::string> allFindingKeys;
    for (const auto& [findingKey, _] : baselineMap) {
        allFindingKeys.emplace(findingKey);
    }
    for (const auto& [findingKey, _] : rerunMap) {
        allFindingKeys.emplace(findingKey);
    }

    FieldClosureResult result{};
    result.Scope = request.Scope;
    result.OutputDirectory = request.OutputDirectory;
    result.RemediationBatchId = request.RemediationBatchId;
    result.BaselineRevision = request.BaselineRevision;
    result.CurrentRevision = request.CurrentRevision;
    result.BaselineCheckpointIds = baselineCheckpointIds;
    result.Summary.BaselineFindingCount = static_cast<uint32_t>(baselineMap.size());
    result.Summary.RerunFindingCount = static_cast<uint32_t>(rerunMap.size());
    result.Summary.BaselineCheckpointCount = static_cast<uint32_t>(baselineCheckpointIds.size());

    for (const std::string& findingKey : allFindingKeys) {
        const auto baselineIt = baselineMap.find(findingKey);
        const auto rerunIt = rerunMap.find(findingKey);
        const bool hasBaseline = baselineIt != baselineMap.end();
        const bool hasRerun = rerunIt != rerunMap.end();

        FieldClosureDiffRecord record{};
        if (hasBaseline && !hasRerun) {
            const FieldClosureFinding& baselineFinding = baselineIt->second;
            record.DiffKind = FieldClosureDiffKind::Resolved;
            record.FindingId = baselineFinding.FindingId;
            record.RuleId = baselineFinding.RuleId;
            record.StableFieldKey = baselineFinding.StableFieldKey;
            record.DomainPair = baselineFinding.DomainPair;
            record.BaselineSeverity = baselineFinding.Severity;
            record.CurrentSeverity = FieldClosureSeverity::Info;
            record.Ownership = baselineFinding.Ownership;
            record.EvidenceIndex = baselineFinding.Evidence;
            ++result.Summary.ResolvedFindingCount;
        } else if (!hasBaseline && hasRerun) {
            const FieldClosureFinding& rerunFinding = rerunIt->second;
            record.DiffKind = FieldClosureDiffKind::New;
            record.FindingId = rerunFinding.FindingId;
            record.RuleId = rerunFinding.RuleId;
            record.StableFieldKey = rerunFinding.StableFieldKey;
            record.DomainPair = rerunFinding.DomainPair;
            record.BaselineSeverity = FieldClosureSeverity::Info;
            record.CurrentSeverity = rerunFinding.Severity;
            record.Ownership = rerunFinding.Ownership;
            record.EvidenceIndex = rerunFinding.Evidence;
            ++result.Summary.NewFindingCount;
        } else {
            const FieldClosureFinding& baselineFinding = baselineIt->second;
            const FieldClosureFinding& rerunFinding = rerunIt->second;
            record.FindingId = rerunFinding.FindingId;
            record.RuleId = rerunFinding.RuleId;
            record.StableFieldKey = rerunFinding.StableFieldKey;
            record.DomainPair = rerunFinding.DomainPair;
            record.BaselineSeverity = baselineFinding.Severity;
            record.CurrentSeverity = rerunFinding.Severity;
            record.Ownership = rerunFinding.Ownership;
            record.EvidenceIndex = rerunFinding.Evidence;
            if (static_cast<uint32_t>(rerunFinding.Severity) > static_cast<uint32_t>(baselineFinding.Severity)) {
                record.DiffKind = FieldClosureDiffKind::Regressed;
                ++result.Summary.RegressedFindingCount;
            } else {
                record.DiffKind = FieldClosureDiffKind::Unchanged;
                ++result.Summary.UnchangedFindingCount;
            }
        }

        if (record.Ownership.OwnerTeam.empty()) {
            record.Ownership.OwnerTeam = "<unspecified>";
        }
        record.DiffSummary = BuildDiffSummary(record);
        record.ActionableDiff = BuildActionableDiff(record);
        record.DiffId = BuildDiffId(record);
        record.DeterministicDigest = ComputeDiffDigest(record);
        result.DiffRecords.push_back(std::move(record));
    }

    SortDiffRecords(result.DiffRecords);
    result.Summary.TotalDiffCount = static_cast<uint32_t>(result.DiffRecords.size());

    for (const FieldClosureDiffRecord& record : result.DiffRecords) {
        if (record.DiffKind != FieldClosureDiffKind::Resolved && record.CurrentSeverity == FieldClosureSeverity::Critical) {
            ++result.Summary.CriticalFindingCount;
        }
    }

    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<FieldClosureResult>::Success(std::move(result));
}

} // namespace

Result<FieldClosureResult> ReRunFullFieldAuditAndDiffAgainstBaseline(const FieldClosureRequest& request) {
    return BuildClosureDiffResult(request, "rerun-diff-baseline");
}

Result<FieldClosureResult> EnforceZeroCriticalFieldDefectGate(const FieldClosureRequest& request) {
    const Result<FieldClosureResult> rerunResult = BuildClosureDiffResult(request, "zero-critical-defect-gate");
    if (!rerunResult.Ok) {
        return rerunResult;
    }

    FieldClosureResult result = rerunResult.Value;
    if (result.Summary.CriticalFindingCount > 0u) {
        result.Summary.GateDecision = FieldClosureGateDecision::Block;
        return Result<FieldClosureResult>::Failure("FIELD_SIGNOFF_BLOCKED");
    }

    result.Summary.GateDecision = FieldClosureGateDecision::Pass;
    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<FieldClosureResult>::Success(std::move(result));
}

Result<FieldClosureResult> PublishFieldIntegritySignoffReport(const FieldClosureRequest& request) {
    if (request.SignoffApprover.empty()) {
        return Result<FieldClosureResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    const Result<FieldClosureResult> rerunResult =
        BuildClosureDiffResult(request, "publish-field-integrity-signoff-report");
    if (!rerunResult.Ok) {
        return rerunResult;
    }

    FieldClosureResult result = rerunResult.Value;
    result.SignoffApprover = request.SignoffApprover;
    result.SignoffReportDigest = ComputeSignoffReportDigest(result);
    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<FieldClosureResult>::Success(std::move(result));
}

} // namespace Core::Remediation
