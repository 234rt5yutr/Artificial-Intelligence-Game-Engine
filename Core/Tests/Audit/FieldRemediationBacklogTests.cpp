#include "Core/Audit/FieldRemediationBacklog.h"

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
    run.OutputDirectory = std::filesystem::path("build") / "field-remediation-backlog-tests" / scope;
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
    const std::filesystem::path root = std::filesystem::path("build") / "field-remediation-backlog-tests";
    std::filesystem::remove_all(root, errorCode);

    {
        FieldRemediationBacklogRequest invalidRequest{};
        const Result<FieldRemediationBacklogReport> result = CreateFieldRemediationBacklogFromAudit(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldRemediationBacklogRequest unsupportedRequest{};
        unsupportedRequest.Scope = "remediation-backlog-v2";
        unsupportedRequest.OutputDirectory = root / "unsupported";
        unsupportedRequest.Revision = "rev-001";
        unsupportedRequest.LedgerReport.Scope = "issue-ledger";
        const Result<FieldRemediationBacklogReport> result = CreateFieldRemediationBacklogFromAudit(unsupportedRequest);
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

        FieldIssueLedgerRequest ledgerRequest{};
        ledgerRequest.Scope = "issue-ledger";
        ledgerRequest.OutputDirectory = root / "ledger";
        ledgerRequest.Revision = "rev-005";
        ledgerRequest.AuditRuns = {
            BuildRun("runtime-state", {BuildPhase("startup", 1u, {findingTypeMismatch, findingEvolutionMismatch})}),
            BuildRun("network-replay", {BuildPhase("replication-parity", 1u, {findingTypeMismatchDuplicate})})};

        const Result<FieldIssueLedgerReport> severityLedger = ComputeFieldIssueSeverityAndBlastRadius(ledgerRequest);
        assert(severityLedger.Ok);
        assert(!severityLedger.Value.Issues.empty());

        FieldRemediationBacklogRequest backlogRequest{};
        backlogRequest.Scope = "remediation-backlog";
        backlogRequest.OutputDirectory = root / "backlog";
        backlogRequest.Revision = ledgerRequest.Revision;
        backlogRequest.LedgerReport = severityLedger.Value;

        const Result<FieldRemediationBacklogReport> first = CreateFieldRemediationBacklogFromAudit(backlogRequest);
        assert(first.Ok);
        assert(first.Value.Scope == backlogRequest.Scope);
        assert(first.Value.Revision == backlogRequest.Revision);
        assert(first.Value.TotalTaskCount == first.Value.Tasks.size());
        assert(first.Value.TotalTaskCount == severityLedger.Value.Issues.size());
        assert(!first.Value.DeterministicDigest.empty());

        bool foundNetworkOwner = false;
        bool foundMigrationCategory = false;
        uint32_t previousPriority = 0;
        bool isFirstTask = true;
        for (const FieldRemediationTask& task : first.Value.Tasks) {
            assert(!task.TaskId.empty());
            assert(!task.IssueId.empty());
            assert(!task.OwnerSubsystem.empty());
            assert(!task.FixCategory.empty());
            assert(!task.Title.empty());
            assert(!task.Description.empty());
            assert(!task.EvidencePointers.empty());
            assert(!task.DeterministicDigest.empty());
            if (task.OwnerSubsystem == "network-systems") {
                foundNetworkOwner = true;
            }
            if (task.FixCategory == "compatibility-migration") {
                foundMigrationCategory = true;
            }

            const uint32_t priorityValue = static_cast<uint32_t>(task.Priority);
            if (!isFirstTask) {
                assert(priorityValue >= previousPriority);
            }
            previousPriority = priorityValue;
            isFirstTask = false;
        }
        assert(foundNetworkOwner);
        assert(foundMigrationCategory);

        const Result<FieldRemediationBacklogReport> second = CreateFieldRemediationBacklogFromAudit(backlogRequest);
        assert(second.Ok);
        assert(second.Value.TotalTaskCount == first.Value.TotalTaskCount);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        for (std::size_t index = 0; index < first.Value.Tasks.size(); ++index) {
            assert(second.Value.Tasks[index].TaskId == first.Value.Tasks[index].TaskId);
            assert(second.Value.Tasks[index].IssueId == first.Value.Tasks[index].IssueId);
            assert(second.Value.Tasks[index].Priority == first.Value.Tasks[index].Priority);
            assert(second.Value.Tasks[index].DeterministicDigest == first.Value.Tasks[index].DeterministicDigest);
        }
    }

    return 0;
}
