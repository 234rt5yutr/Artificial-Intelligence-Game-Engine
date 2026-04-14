#include "Core/Audit/FieldIssueLedger.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

Core::Audit::FieldValidationFinding BuildFinding(const std::string& ruleId,
                                                 const std::string& stableFieldKey,
                                                 const std::string& domainPair,
                                                 const std::string& leftFieldId,
                                                 const std::string& rightFieldId,
                                                 const std::string& migrationPlaceholder) {
    Core::Audit::FieldValidationFinding finding{};
    finding.RuleId = ruleId;
    finding.StableFieldKey = stableFieldKey;
    finding.DomainPair = domainPair;
    finding.MigrationRecommendationPlaceholder = migrationPlaceholder;
    finding.LeftEvidence.FieldId = leftFieldId;
    finding.RightEvidence.FieldId = rightFieldId;
    return finding;
}

Core::Audit::FieldAuditPhaseStamp BuildPhase(const std::string& phaseId,
                                             const uint32_t phaseOrdinal,
                                             std::vector<Core::Audit::FieldValidationFinding> findings) {
    Core::Audit::FieldAuditPhaseStamp phase{};
    phase.PhaseId = phaseId;
    phase.PhaseLabel = phaseId;
    phase.PhaseOrdinal = phaseOrdinal;
    phase.InventoryDigest = "inventory-" + phaseId;
    phase.ValidationDigest = "validation-" + phaseId;
    phase.TotalFindingCount = static_cast<uint32_t>(findings.size());
    phase.Findings = std::move(findings);
    phase.DeterministicPhaseDigest = "phase-digest-" + phaseId;
    return phase;
}

Core::Audit::FieldAuditRunReport BuildRun(const std::string& scope,
                                          std::vector<Core::Audit::FieldAuditPhaseStamp> phases) {
    Core::Audit::FieldAuditRunReport run{};
    run.Scope = scope;
    run.OutputDirectory = std::filesystem::path("build") / "field-issue-ledger-tests" / scope;
    run.PhaseStamps = std::move(phases);
    run.TotalPhases = static_cast<uint32_t>(run.PhaseStamps.size());
    for (const Core::Audit::FieldAuditPhaseStamp& phase : run.PhaseStamps) {
        run.TotalFindingCount += phase.TotalFindingCount;
    }
    run.DeterministicDigest = "run-digest-" + scope;
    return run;
}

} // namespace

int main() {
    using namespace Core::Audit;

    std::error_code errorCode;
    const std::filesystem::path root = std::filesystem::path("build") / "field-issue-ledger-tests";
    std::filesystem::remove_all(root, errorCode);

    {
        FieldIssueLedgerRequest invalidRequest{};
        const Result<FieldIssueLedgerReport> result = GenerateFieldAuditIssueLedger(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldIssueLedgerRequest unsupportedRequest{};
        unsupportedRequest.Scope = "issue-ledger-v2";
        unsupportedRequest.OutputDirectory = root / "unsupported";
        unsupportedRequest.Revision = "rev-001";
        unsupportedRequest.AuditRuns = {BuildRun("runtime-state", {BuildPhase("startup", 1u, {})})};
        const Result<FieldIssueLedgerReport> result = GenerateFieldAuditIssueLedger(unsupportedRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        const FieldValidationFinding findingTypeMismatch = BuildFinding(
            "FIELD_AUDIT_RULE_TYPE_CONTRACT_MISMATCH",
            "PlayerState::Health",
            "runtime:ecs<->serialized:save",
            "runtime::PlayerState::Health",
            "serialized::PlayerState::Health",
            "");
        const FieldValidationFinding findingTypeMismatchDuplicate = BuildFinding(
            "FIELD_AUDIT_RULE_TYPE_CONTRACT_MISMATCH",
            "PlayerState::Health",
            "runtime:ecs<->serialized:save",
            "runtime::PlayerState::Health",
            "protocol::PlayerState::Health",
            "");
        const FieldValidationFinding findingEvolutionMismatch = BuildFinding(
            "FIELD_AUDIT_RULE_EVOLUTION_BACKWARD_COMPATIBILITY_MISMATCH",
            "PlayerState::Rank",
            "runtime:network<->serialized:save",
            "runtime::PlayerState::Rank",
            "serialized::PlayerState::Rank",
            "MIGRATION_RECOMMENDATION_PLACEHOLDER:ADD_VERSIONED_TYPE_ADAPTER_OR_REWRITE_RULE");

        FieldIssueLedgerRequest request{};
        request.Scope = "issue-ledger";
        request.OutputDirectory = root / "ledger";
        request.Revision = "rev-002";
        request.AuditRuns = {
            BuildRun("runtime-state", {BuildPhase("startup", 1u, {findingTypeMismatch, findingEvolutionMismatch})}),
            BuildRun("network-replay", {BuildPhase("replication-parity", 1u, {findingTypeMismatchDuplicate})})};

        const Result<FieldIssueLedgerReport> first = GenerateFieldAuditIssueLedger(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.Revision == request.Revision);
        assert(first.Value.Summary.RawFindingCount == 3u);
        assert(first.Value.Summary.DeduplicatedIssueCount == 2u);
        assert(first.Value.Issues.size() == 2u);
        assert(!first.Value.DeterministicDigest.empty());

        uint32_t accumulatedOccurrenceCount = 0;
        uint32_t typeIssueCount = 0;
        uint32_t evolutionIssueCount = 0;
        for (const FieldAuditIssueRecord& issue : first.Value.Issues) {
            assert(!issue.IssueId.empty());
            assert(issue.FirstSeenRevision == request.Revision);
            assert(!issue.DeterministicDigest.empty());
            assert(!issue.EvidenceReferences.empty());
            accumulatedOccurrenceCount += issue.OccurrenceCount;

            if (issue.RuleId == "FIELD_AUDIT_RULE_TYPE_CONTRACT_MISMATCH") {
                ++typeIssueCount;
                assert(issue.OccurrenceCount == 2u);
                assert(issue.EvidenceReferences.size() == 2u);
            }
            if (issue.RuleId == "FIELD_AUDIT_RULE_EVOLUTION_BACKWARD_COMPATIBILITY_MISMATCH") {
                ++evolutionIssueCount;
                assert(issue.OccurrenceCount == 1u);
                assert(issue.EvidenceReferences.size() == 1u);
                assert(issue.EvidenceReferences[0].MigrationRecommendationPlaceholder ==
                       "MIGRATION_RECOMMENDATION_PLACEHOLDER:ADD_VERSIONED_TYPE_ADAPTER_OR_REWRITE_RULE");
            }
        }
        assert(accumulatedOccurrenceCount == first.Value.Summary.RawFindingCount);
        assert(typeIssueCount == 1u);
        assert(evolutionIssueCount == 1u);

        const Result<FieldIssueLedgerReport> second = GenerateFieldAuditIssueLedger(request);
        assert(second.Ok);
        assert(second.Value.Issues.size() == first.Value.Issues.size());
        assert(second.Value.Summary.RawFindingCount == first.Value.Summary.RawFindingCount);
        assert(second.Value.Summary.DeduplicatedIssueCount == first.Value.Summary.DeduplicatedIssueCount);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        for (std::size_t index = 0; index < first.Value.Issues.size(); ++index) {
            assert(second.Value.Issues[index].IssueId == first.Value.Issues[index].IssueId);
            assert(second.Value.Issues[index].OccurrenceCount == first.Value.Issues[index].OccurrenceCount);
            assert(second.Value.Issues[index].DeterministicDigest == first.Value.Issues[index].DeterministicDigest);
        }
    }

    {
        FieldIssueLedgerRequest invalidRequest{};
        const Result<FieldIssueLedgerReport> result = ComputeFieldIssueSeverityAndBlastRadius(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        const FieldValidationFinding findingTypeMismatch = BuildFinding(
            "FIELD_AUDIT_RULE_TYPE_CONTRACT_MISMATCH",
            "PlayerState::Health",
            "runtime:ecs<->serialized:save",
            "runtime::PlayerState::Health",
            "serialized::PlayerState::Health",
            "");
        const FieldValidationFinding findingTypeMismatchDuplicate = BuildFinding(
            "FIELD_AUDIT_RULE_TYPE_CONTRACT_MISMATCH",
            "PlayerState::Health",
            "runtime:ecs<->serialized:save",
            "runtime::PlayerState::Health",
            "protocol::PlayerState::Health",
            "");
        const FieldValidationFinding findingEvolutionMismatch = BuildFinding(
            "FIELD_AUDIT_RULE_EVOLUTION_BACKWARD_COMPATIBILITY_MISMATCH",
            "PlayerState::Rank",
            "runtime:network<->serialized:save",
            "runtime::PlayerState::Rank",
            "serialized::PlayerState::Rank",
            "MIGRATION_RECOMMENDATION_PLACEHOLDER:ADD_VERSIONED_TYPE_ADAPTER_OR_REWRITE_RULE");

        FieldIssueLedgerRequest request{};
        request.Scope = "issue-ledger";
        request.OutputDirectory = root / "ledger-severity";
        request.Revision = "rev-003";
        request.AuditRuns = {
            BuildRun("runtime-state", {BuildPhase("startup", 1u, {findingTypeMismatch, findingEvolutionMismatch})}),
            BuildRun("network-replay", {BuildPhase("replication-parity", 1u, {findingTypeMismatchDuplicate})})};

        const Result<FieldIssueLedgerReport> first = ComputeFieldIssueSeverityAndBlastRadius(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.Summary.RawFindingCount == 3u);
        assert(first.Value.Summary.DeduplicatedIssueCount == 2u);
        assert(first.Value.Issues.size() == 2u);
        assert(!first.Value.DeterministicDigest.empty());

        auto hasDomain = [](const std::vector<std::string>& domains, const std::string& domain) {
            return std::find(domains.begin(), domains.end(), domain) != domains.end();
        };

        uint32_t highOrCriticalCount = 0;
        for (const FieldAuditIssueRecord& issue : first.Value.Issues) {
            assert(issue.BlastRadiusScore > 0u);
            assert(!issue.SeverityRationale.empty());
            assert(!issue.ImpactedDomains.empty());
            assert(!issue.DeterministicDigest.empty());
            if (issue.Severity == FieldIssueSeverityLevel::High ||
                issue.Severity == FieldIssueSeverityLevel::Critical) {
                ++highOrCriticalCount;
            }

            if (issue.RuleId == "FIELD_AUDIT_RULE_TYPE_CONTRACT_MISMATCH") {
                assert(issue.Severity == FieldIssueSeverityLevel::High ||
                       issue.Severity == FieldIssueSeverityLevel::Critical);
                assert(hasDomain(issue.ImpactedDomains, "runtime"));
                assert(hasDomain(issue.ImpactedDomains, "network"));
            }

            if (issue.RuleId == "FIELD_AUDIT_RULE_EVOLUTION_BACKWARD_COMPATIBILITY_MISMATCH") {
                assert(issue.Severity == FieldIssueSeverityLevel::Medium ||
                       issue.Severity == FieldIssueSeverityLevel::High ||
                       issue.Severity == FieldIssueSeverityLevel::Critical);
                assert(hasDomain(issue.ImpactedDomains, "persistence"));
            }
        }
        assert(highOrCriticalCount >= 1u);

        const Result<FieldIssueLedgerReport> second = ComputeFieldIssueSeverityAndBlastRadius(request);
        assert(second.Ok);
        assert(second.Value.Issues.size() == first.Value.Issues.size());
        assert(second.Value.Summary.RawFindingCount == first.Value.Summary.RawFindingCount);
        assert(second.Value.Summary.DeduplicatedIssueCount == first.Value.Summary.DeduplicatedIssueCount);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        for (std::size_t index = 0; index < first.Value.Issues.size(); ++index) {
            assert(second.Value.Issues[index].IssueId == first.Value.Issues[index].IssueId);
            assert(second.Value.Issues[index].Severity == first.Value.Issues[index].Severity);
            assert(second.Value.Issues[index].BlastRadiusScore == first.Value.Issues[index].BlastRadiusScore);
            assert(second.Value.Issues[index].DeterministicDigest == first.Value.Issues[index].DeterministicDigest);
        }
    }

    return 0;
}
