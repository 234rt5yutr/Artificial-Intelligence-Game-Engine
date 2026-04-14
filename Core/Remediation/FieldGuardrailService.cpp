#include "Core/Remediation/FieldGuardrailService.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iomanip>
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

[[nodiscard]] bool IsValidEntry(const FieldGuardrailEntry& entry) {
    if (entry.StableFieldKey.empty() || entry.DomainPair.empty() || entry.TargetFieldId.empty() ||
        entry.PropertyPath.empty() || entry.ExpectedAssertionExpression.empty() || entry.Taxonomy.TaxonomyId.empty() ||
        entry.Taxonomy.Category.empty() || entry.Taxonomy.Invariant.empty()) {
        return false;
    }

    return !entry.Taxonomy.Lineage.FindingId.empty() && !entry.Taxonomy.Lineage.RuleId.empty() &&
           !entry.Taxonomy.Lineage.Owner.empty();
}

[[nodiscard]] std::vector<std::string> NormalizeCoverageMap(const std::vector<std::string>& coverageMap) {
    std::set<std::string> normalized;
    for (const std::string& stageFixId : coverageMap) {
        if (!stageFixId.empty()) {
            normalized.emplace(stageFixId);
        }
    }
    return {normalized.begin(), normalized.end()};
}

[[nodiscard]] std::vector<std::string> NormalizeDriftHunks(const std::vector<std::string>& diffHunks) {
    std::set<std::string> normalized;
    for (const std::string& diffHunk : diffHunks) {
        if (!diffHunk.empty()) {
            normalized.emplace(diffHunk);
        }
    }
    return {normalized.begin(), normalized.end()};
}

[[nodiscard]] bool IsValidRegressionEntry(const FieldGuardrailEntry& entry) {
    if (entry.RegressionSuite.SuiteId.empty()) {
        return false;
    }

    const std::vector<std::string> normalizedCoverageMap = NormalizeCoverageMap(entry.RegressionSuite.Stage30CoverageMap);
    return !normalizedCoverageMap.empty();
}

[[nodiscard]] bool IsValidAuditGateEntry(const FieldGuardrailEntry& entry) {
    return !entry.AuditPolicy.PolicyId.empty();
}

[[nodiscard]] bool IsValidDriftEntry(const FieldGuardrailEntry& entry) {
    if (entry.Drift.BaselineCommitId.empty() || entry.Drift.CurrentCommitId.empty() ||
        entry.Drift.BaselineCommitId == entry.Drift.CurrentCommitId || entry.Drift.ArtifactId.empty() ||
        entry.Drift.ArtifactPath.empty() || entry.Drift.DiffSummary.empty() || entry.Drift.OwningSubsystem.empty() ||
        entry.Drift.AlertChannel.empty() || entry.Drift.AlertRoute.empty()) {
        return false;
    }

    const std::vector<std::string> normalizedDiffHunks = NormalizeDriftHunks(entry.Drift.DiffHunks);
    return !normalizedDiffHunks.empty();
}

[[nodiscard]] bool IsBlockingReleaseGateFinding(const FieldGuardrailAuditPolicyMetadata& auditPolicy) {
    if (!auditPolicy.ReleaseLane || auditPolicy.FindingStatus != FieldAuditFindingStatus::Unresolved) {
        return false;
    }

    return auditPolicy.Severity == FieldAuditSeverity::High || auditPolicy.Severity == FieldAuditSeverity::Critical;
}

[[nodiscard]] std::string BuildAssertionId(const std::string_view scope, const FieldGuardrailRecord& record) {
    std::string idMaterial;
    idMaterial.reserve(384u);
    idMaterial.append(scope);
    idMaterial.push_back('|');
    idMaterial.append(record.Taxonomy.Lineage.FindingId);
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.Domain)));
    idMaterial.push_back('|');
    idMaterial.append(record.TargetFieldId);
    idMaterial.push_back('|');
    idMaterial.append(record.PropertyPath);
    idMaterial.push_back('|');
    idMaterial.append(record.AssertionExpression);
    idMaterial.push_back('|');
    idMaterial.append(record.Taxonomy.TaxonomyId);
    idMaterial.push_back('|');
    idMaterial.append(record.RegressionSuite.SuiteId);
    idMaterial.push_back('|');
    for (const std::string& stageFixId : record.RegressionSuite.Stage30CoverageMap) {
        idMaterial.append(stageFixId);
        idMaterial.push_back(',');
    }
    idMaterial.push_back('|');
    idMaterial.append(record.AuditPolicy.PolicyId);
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(record.AuditPolicy.ReleaseLane ? 1u : 0u));
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.AuditPolicy.Severity)));
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.AuditPolicy.FindingStatus)));
    idMaterial.push_back('|');
    idMaterial.append(record.Drift.BaselineCommitId);
    idMaterial.push_back('|');
    idMaterial.append(record.Drift.CurrentCommitId);
    idMaterial.push_back('|');
    idMaterial.append(record.Drift.ArtifactId);
    idMaterial.push_back('|');
    idMaterial.append(record.Drift.ArtifactPath);
    idMaterial.push_back('|');
    idMaterial.append(record.Drift.DiffSummary);
    idMaterial.push_back('|');
    for (const std::string& diffHunk : record.Drift.DiffHunks) {
        idMaterial.append(diffHunk);
        idMaterial.push_back(',');
    }
    idMaterial.push_back('|');
    idMaterial.append(record.Drift.OwningSubsystem);
    idMaterial.push_back('|');
    idMaterial.append(record.Drift.AlertChannel);
    idMaterial.push_back('|');
    idMaterial.append(record.Drift.AlertRoute);
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.Drift.AlertSeverity)));
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.GateDecision)));
    return HashToHex(HashString(idMaterial));
}

[[nodiscard]] std::string ComputeRecordDigest(const FieldGuardrailRecord& record) {
    std::string digestMaterial;
    digestMaterial.reserve(768u);
    digestMaterial.append(record.AssertionId);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.Domain)));
    digestMaterial.push_back('|');
    digestMaterial.append(record.StableFieldKey);
    digestMaterial.push_back('|');
    digestMaterial.append(record.DomainPair);
    digestMaterial.push_back('|');
    digestMaterial.append(record.TargetFieldId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.PropertyPath);
    digestMaterial.push_back('|');
    digestMaterial.append(record.ExistingAssertionExpression);
    digestMaterial.push_back('|');
    digestMaterial.append(record.AssertionExpression);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rationale);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Taxonomy.TaxonomyId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Taxonomy.Category);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Taxonomy.Invariant);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Taxonomy.Lineage.FindingId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Taxonomy.Lineage.RuleId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Taxonomy.Lineage.Owner);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Taxonomy.Lineage.RemediationBatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.RegressionSuite.SuiteId);
    digestMaterial.push_back('|');
    for (const std::string& stageFixId : record.RegressionSuite.Stage30CoverageMap) {
        digestMaterial.append(stageFixId);
        digestMaterial.push_back(',');
    }
    digestMaterial.push_back('|');
    digestMaterial.append(record.AuditPolicy.PolicyId);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(record.AuditPolicy.ReleaseLane ? 1u : 0u));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.AuditPolicy.Severity)));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.AuditPolicy.FindingStatus)));
    digestMaterial.push_back('|');
    digestMaterial.append(record.Drift.BaselineCommitId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Drift.CurrentCommitId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Drift.ArtifactId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Drift.ArtifactPath);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Drift.DiffSummary);
    digestMaterial.push_back('|');
    for (const std::string& diffHunk : record.Drift.DiffHunks) {
        digestMaterial.append(diffHunk);
        digestMaterial.push_back(',');
    }
    digestMaterial.push_back('|');
    digestMaterial.append(record.Drift.OwningSubsystem);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Drift.AlertChannel);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Drift.AlertRoute);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.Drift.AlertSeverity)));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.GateDecision)));
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputeResultDigest(const FieldGuardrailResult& result) {
    std::string digestMaterial;
    digestMaterial.reserve((result.AssertionRecords.size() * 96u) + 224u);
    digestMaterial.append(result.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RemediationBatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.RuntimeAssertionCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.EditorAssertionCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.BuildAssertionCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.TotalAssertionCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.RegressionSuiteCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.RegressionCoverageSignalCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.RegressionCoverageCorrectionCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.DriftEventCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.DriftAlertCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.DriftOwningSubsystemCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.UnresolvedHighFindingCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.UnresolvedCriticalFindingCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ReleaseGateBlocked ? 1u : 0u));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(result.Summary.ReleaseGateDecision)));
    digestMaterial.push_back('\n');

    for (const FieldGuardrailRecord& record : result.AssertionRecords) {
        digestMaterial.append(record.AssertionId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.DeterministicDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(record.TargetFieldId);
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

void SortAssertionRecords(std::vector<FieldGuardrailRecord>& records) {
    std::sort(records.begin(), records.end(), [](const FieldGuardrailRecord& left, const FieldGuardrailRecord& right) {
        if (left.Domain != right.Domain) {
            return static_cast<uint32_t>(left.Domain) < static_cast<uint32_t>(right.Domain);
        }
        if (left.Taxonomy.TaxonomyId != right.Taxonomy.TaxonomyId) {
            return left.Taxonomy.TaxonomyId < right.Taxonomy.TaxonomyId;
        }
        if (left.StableFieldKey != right.StableFieldKey) {
            return left.StableFieldKey < right.StableFieldKey;
        }
        if (left.TargetFieldId != right.TargetFieldId) {
            return left.TargetFieldId < right.TargetFieldId;
        }
        if (left.PropertyPath != right.PropertyPath) {
            return left.PropertyPath < right.PropertyPath;
        }
        if (left.AssertionExpression != right.AssertionExpression) {
            return left.AssertionExpression < right.AssertionExpression;
        }
        if (left.RegressionSuite.SuiteId != right.RegressionSuite.SuiteId) {
            return left.RegressionSuite.SuiteId < right.RegressionSuite.SuiteId;
        }
        if (left.RegressionSuite.Stage30CoverageMap != right.RegressionSuite.Stage30CoverageMap) {
            return std::lexicographical_compare(left.RegressionSuite.Stage30CoverageMap.begin(),
                                                left.RegressionSuite.Stage30CoverageMap.end(),
                                                right.RegressionSuite.Stage30CoverageMap.begin(),
                                                right.RegressionSuite.Stage30CoverageMap.end());
        }
        if (left.AuditPolicy.PolicyId != right.AuditPolicy.PolicyId) {
            return left.AuditPolicy.PolicyId < right.AuditPolicy.PolicyId;
        }
        if (left.AuditPolicy.ReleaseLane != right.AuditPolicy.ReleaseLane) {
            return left.AuditPolicy.ReleaseLane < right.AuditPolicy.ReleaseLane;
        }
        if (left.AuditPolicy.Severity != right.AuditPolicy.Severity) {
            return static_cast<uint32_t>(left.AuditPolicy.Severity) < static_cast<uint32_t>(right.AuditPolicy.Severity);
        }
        if (left.AuditPolicy.FindingStatus != right.AuditPolicy.FindingStatus) {
            return static_cast<uint32_t>(left.AuditPolicy.FindingStatus) <
                   static_cast<uint32_t>(right.AuditPolicy.FindingStatus);
        }
        if (left.Drift.BaselineCommitId != right.Drift.BaselineCommitId) {
            return left.Drift.BaselineCommitId < right.Drift.BaselineCommitId;
        }
        if (left.Drift.CurrentCommitId != right.Drift.CurrentCommitId) {
            return left.Drift.CurrentCommitId < right.Drift.CurrentCommitId;
        }
        if (left.Drift.ArtifactId != right.Drift.ArtifactId) {
            return left.Drift.ArtifactId < right.Drift.ArtifactId;
        }
        if (left.Drift.ArtifactPath != right.Drift.ArtifactPath) {
            return left.Drift.ArtifactPath < right.Drift.ArtifactPath;
        }
        if (left.Drift.DiffSummary != right.Drift.DiffSummary) {
            return left.Drift.DiffSummary < right.Drift.DiffSummary;
        }
        if (left.Drift.DiffHunks != right.Drift.DiffHunks) {
            return std::lexicographical_compare(left.Drift.DiffHunks.begin(),
                                                left.Drift.DiffHunks.end(),
                                                right.Drift.DiffHunks.begin(),
                                                right.Drift.DiffHunks.end());
        }
        if (left.Drift.OwningSubsystem != right.Drift.OwningSubsystem) {
            return left.Drift.OwningSubsystem < right.Drift.OwningSubsystem;
        }
        if (left.Drift.AlertChannel != right.Drift.AlertChannel) {
            return left.Drift.AlertChannel < right.Drift.AlertChannel;
        }
        if (left.Drift.AlertRoute != right.Drift.AlertRoute) {
            return left.Drift.AlertRoute < right.Drift.AlertRoute;
        }
        if (left.Drift.AlertSeverity != right.Drift.AlertSeverity) {
            return static_cast<uint32_t>(left.Drift.AlertSeverity) <
                   static_cast<uint32_t>(right.Drift.AlertSeverity);
        }
        if (left.GateDecision != right.GateDecision) {
            return static_cast<uint32_t>(left.GateDecision) < static_cast<uint32_t>(right.GateDecision);
        }
        return left.Taxonomy.Lineage.FindingId < right.Taxonomy.Lineage.FindingId;
    });
}

void AddDomainCount(FieldGuardrailSummary& summary, const FieldGuardrailDomain domain) {
    if (domain == FieldGuardrailDomain::Runtime) {
        ++summary.RuntimeAssertionCount;
    } else if (domain == FieldGuardrailDomain::Editor) {
        ++summary.EditorAssertionCount;
    } else if (domain == FieldGuardrailDomain::Build) {
        ++summary.BuildAssertionCount;
    }
}

[[nodiscard]] std::string ExistingAssertionOrUnset(const std::string& assertionExpression) {
    if (assertionExpression.empty()) {
        return "<unset>";
    }
    return assertionExpression;
}

[[nodiscard]] std::string BuildDedupeKey(const FieldGuardrailEntry& entry,
                                         const std::vector<std::string>& normalizedCoverageMap,
                                         const bool includeRegressionSuite,
                                         const bool includeAuditPolicy,
                                         const bool includeDriftMetadata) {
    std::string dedupeKey;
    dedupeKey.reserve(640u);
    dedupeKey.append(entry.Taxonomy.Lineage.FindingId);
    dedupeKey.push_back('|');
    dedupeKey.append(entry.Taxonomy.Lineage.RuleId);
    dedupeKey.push_back('|');
    dedupeKey.append(entry.Taxonomy.Lineage.Owner);
    dedupeKey.push_back('|');
    dedupeKey.append(std::to_string(static_cast<uint32_t>(entry.Domain)));
    dedupeKey.push_back('|');
    dedupeKey.append(entry.TargetFieldId);
    dedupeKey.push_back('|');
    dedupeKey.append(entry.PropertyPath);
    dedupeKey.push_back('|');
    dedupeKey.append(entry.ExpectedAssertionExpression);
    dedupeKey.push_back('|');
    dedupeKey.append(entry.Taxonomy.TaxonomyId);
    if (includeRegressionSuite) {
        dedupeKey.push_back('|');
        dedupeKey.append(entry.RegressionSuite.SuiteId);
        dedupeKey.push_back('|');
        for (const std::string& stageFixId : normalizedCoverageMap) {
            dedupeKey.append(stageFixId);
            dedupeKey.push_back(',');
        }
    }
    if (includeAuditPolicy) {
        dedupeKey.push_back('|');
        dedupeKey.append(entry.AuditPolicy.PolicyId);
        dedupeKey.push_back('|');
        dedupeKey.append(std::to_string(entry.AuditPolicy.ReleaseLane ? 1u : 0u));
        dedupeKey.push_back('|');
        dedupeKey.append(std::to_string(static_cast<uint32_t>(entry.AuditPolicy.Severity)));
        dedupeKey.push_back('|');
        dedupeKey.append(std::to_string(static_cast<uint32_t>(entry.AuditPolicy.FindingStatus)));
    }
    if (includeDriftMetadata) {
        dedupeKey.push_back('|');
        dedupeKey.append(entry.Drift.BaselineCommitId);
        dedupeKey.push_back('|');
        dedupeKey.append(entry.Drift.CurrentCommitId);
        dedupeKey.push_back('|');
        dedupeKey.append(entry.Drift.ArtifactId);
        dedupeKey.push_back('|');
        dedupeKey.append(entry.Drift.ArtifactPath);
        dedupeKey.push_back('|');
        dedupeKey.append(entry.Drift.DiffSummary);
        dedupeKey.push_back('|');
        for (const std::string& diffHunk : NormalizeDriftHunks(entry.Drift.DiffHunks)) {
            dedupeKey.append(diffHunk);
            dedupeKey.push_back(',');
        }
        dedupeKey.push_back('|');
        dedupeKey.append(entry.Drift.OwningSubsystem);
        dedupeKey.push_back('|');
        dedupeKey.append(entry.Drift.AlertChannel);
        dedupeKey.push_back('|');
        dedupeKey.append(entry.Drift.AlertRoute);
        dedupeKey.push_back('|');
        dedupeKey.append(std::to_string(static_cast<uint32_t>(entry.Drift.AlertSeverity)));
    }
    return dedupeKey;
}

[[nodiscard]] Result<FieldGuardrailResult> BuildGuardrailResult(const FieldGuardrailRequest& request,
                                                                const std::string_view expectedScope,
                                                                const bool requireRegressionSuite,
                                                                const bool evaluateAuditGate,
                                                                const bool requireDriftMetadata) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.Entries.empty()) {
        return Result<FieldGuardrailResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != expectedScope) {
        return Result<FieldGuardrailResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    for (const FieldGuardrailEntry& entry : request.Entries) {
        if (!IsValidEntry(entry)) {
            return Result<FieldGuardrailResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
        if (requireRegressionSuite && !IsValidRegressionEntry(entry)) {
            return Result<FieldGuardrailResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
        if (evaluateAuditGate && !IsValidAuditGateEntry(entry)) {
            return Result<FieldGuardrailResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
        if (requireDriftMetadata && !IsValidDriftEntry(entry)) {
            return Result<FieldGuardrailResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldGuardrailResult>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<FieldGuardrailRecord> assertionRecords;
    std::set<std::string> seenAssertionKeys;
    for (const FieldGuardrailEntry& entry : request.Entries) {
        const std::vector<std::string> normalizedCoverageMap = NormalizeCoverageMap(entry.RegressionSuite.Stage30CoverageMap);
        const std::vector<std::string> normalizedDiffHunks = NormalizeDriftHunks(entry.Drift.DiffHunks);
        const std::string dedupeKey =
            BuildDedupeKey(entry, normalizedCoverageMap, requireRegressionSuite, evaluateAuditGate, requireDriftMetadata);
        if (seenAssertionKeys.contains(dedupeKey)) {
            continue;
        }
        seenAssertionKeys.emplace(dedupeKey);

        FieldGuardrailRecord record{};
        record.Domain = entry.Domain;
        record.StableFieldKey = entry.StableFieldKey;
        record.DomainPair = entry.DomainPair;
        record.TargetFieldId = entry.TargetFieldId;
        record.PropertyPath = entry.PropertyPath;
        record.ExistingAssertionExpression = ExistingAssertionOrUnset(entry.AssertionExpression);
        record.AssertionExpression = entry.ExpectedAssertionExpression;
        record.Rationale = entry.Rationale.empty() ? (requireDriftMetadata
                                                          ? "record-field-drift-event"
                                                          : (requireRegressionSuite
                                                                 ? "register-field-contract-regression-suite"
                                                                 : "register-field-invariant-assertion"))
                                                   : entry.Rationale;
        record.Taxonomy = entry.Taxonomy;
        record.Taxonomy.Lineage.RemediationBatchId = request.RemediationBatchId;
        record.RegressionSuite.SuiteId = entry.RegressionSuite.SuiteId;
        record.RegressionSuite.Stage30CoverageMap = normalizedCoverageMap;
        record.AuditPolicy = entry.AuditPolicy;
        record.Drift.BaselineCommitId = entry.Drift.BaselineCommitId;
        record.Drift.CurrentCommitId = entry.Drift.CurrentCommitId;
        record.Drift.ArtifactId = entry.Drift.ArtifactId;
        record.Drift.ArtifactPath = entry.Drift.ArtifactPath;
        record.Drift.DiffSummary = entry.Drift.DiffSummary;
        record.Drift.DiffHunks = normalizedDiffHunks;
        record.Drift.OwningSubsystem = entry.Drift.OwningSubsystem;
        record.Drift.AlertChannel = entry.Drift.AlertChannel;
        record.Drift.AlertRoute = entry.Drift.AlertRoute;
        record.Drift.AlertSeverity = entry.Drift.AlertSeverity;
        if (evaluateAuditGate) {
            record.GateDecision = IsBlockingReleaseGateFinding(record.AuditPolicy) ? FieldAuditGateDecision::Block
                                                                                   : FieldAuditGateDecision::Pass;
        }
        record.AssertionId = BuildAssertionId(expectedScope, record);
        record.DeterministicDigest = ComputeRecordDigest(record);
        assertionRecords.push_back(std::move(record));
    }

    SortAssertionRecords(assertionRecords);

    FieldGuardrailResult result{};
    result.Scope = request.Scope;
    result.OutputDirectory = request.OutputDirectory;
    result.RemediationBatchId = request.RemediationBatchId;
    result.AssertionRecords = std::move(assertionRecords);

    std::set<std::string> uniqueSuites;
    std::set<std::string> uniqueDriftAlerts;
    std::set<std::string> uniqueDriftOwningSubsystems;
    for (const FieldGuardrailRecord& record : result.AssertionRecords) {
        AddDomainCount(result.Summary, record.Domain);
        ++result.Summary.TotalAssertionCount;
        if (!record.RegressionSuite.SuiteId.empty()) {
            uniqueSuites.emplace(record.RegressionSuite.SuiteId);
            result.Summary.RegressionCoverageSignalCount +=
                static_cast<uint32_t>(record.RegressionSuite.Stage30CoverageMap.size());
            if (record.ExistingAssertionExpression != record.AssertionExpression) {
                ++result.Summary.RegressionCoverageCorrectionCount;
            }
        }
        if (evaluateAuditGate && IsBlockingReleaseGateFinding(record.AuditPolicy)) {
            if (record.AuditPolicy.Severity == FieldAuditSeverity::Critical) {
                ++result.Summary.UnresolvedCriticalFindingCount;
            } else if (record.AuditPolicy.Severity == FieldAuditSeverity::High) {
                ++result.Summary.UnresolvedHighFindingCount;
            }
        }
        if (requireDriftMetadata) {
            ++result.Summary.DriftEventCount;
            uniqueDriftAlerts.emplace(record.Drift.AlertChannel + "|" + record.Drift.AlertRoute);
            uniqueDriftOwningSubsystems.emplace(record.Drift.OwningSubsystem);
        }
    }
    result.Summary.RegressionSuiteCount = static_cast<uint32_t>(uniqueSuites.size());
    if (requireDriftMetadata) {
        result.Summary.DriftAlertCount = static_cast<uint32_t>(uniqueDriftAlerts.size());
        result.Summary.DriftOwningSubsystemCount = static_cast<uint32_t>(uniqueDriftOwningSubsystems.size());
    }
    if (evaluateAuditGate) {
        result.Summary.ReleaseGateBlocked =
            result.Summary.UnresolvedCriticalFindingCount > 0u || result.Summary.UnresolvedHighFindingCount > 0u;
        result.Summary.ReleaseGateDecision =
            result.Summary.ReleaseGateBlocked ? FieldAuditGateDecision::Block : FieldAuditGateDecision::Pass;
    }
    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<FieldGuardrailResult>::Success(std::move(result));
}

} // namespace

Result<FieldGuardrailResult> AddFieldInvariantAssertions(const FieldGuardrailRequest& request) {
    return BuildGuardrailResult(request, "field-invariant-assertions", false, false, false);
}

Result<FieldGuardrailResult> AddFieldContractRegressionSuites(const FieldGuardrailRequest& request) {
    return BuildGuardrailResult(request, "field-contract-regression-suites", true, false, false);
}

Result<FieldGuardrailResult> AddFieldAuditGateToBuildPipeline(const FieldGuardrailRequest& request) {
    return BuildGuardrailResult(request, "field-audit-gate-build-pipeline", false, true, false);
}

Result<FieldGuardrailResult> AddFieldDriftMonitoringAndAlerting(const FieldGuardrailRequest& request) {
    return BuildGuardrailResult(request, "field-drift-monitoring-alerting", false, false, true);
}

} // namespace Core::Remediation
