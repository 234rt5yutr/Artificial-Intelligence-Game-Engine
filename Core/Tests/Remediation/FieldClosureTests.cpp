#include "Core/Remediation/FieldClosureService.h"

#include <cassert>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

Core::Remediation::FieldClosureEvidenceMetadata BuildEvidence(const std::string& evidenceId,
                                                              const std::string& evidencePath,
                                                              const std::string& evidenceDigest) {
    Core::Remediation::FieldClosureEvidenceMetadata evidence{};
    evidence.EvidenceId = evidenceId;
    evidence.EvidencePath = evidencePath;
    evidence.EvidenceDigest = evidenceDigest;
    return evidence;
}

Core::Remediation::FieldClosureFinding BuildFinding(const std::string& findingId,
                                                    const std::string& ruleId,
                                                    const std::string& stableFieldKey,
                                                    const std::string& domainPair,
                                                    const Core::Remediation::FieldClosureSeverity severity,
                                                    const std::string& ownerSubsystem,
                                                    const std::string& ownerTeam,
                                                    const std::vector<Core::Remediation::FieldClosureEvidenceMetadata>&
                                                        evidence) {
    Core::Remediation::FieldClosureFinding finding{};
    finding.FindingId = findingId;
    finding.RuleId = ruleId;
    finding.StableFieldKey = stableFieldKey;
    finding.DomainPair = domainPair;
    finding.Severity = severity;
    finding.Ownership.OwnerSubsystem = ownerSubsystem;
    finding.Ownership.OwnerTeam = ownerTeam;
    finding.Evidence = evidence;
    return finding;
}

} // namespace

int main() {
    using namespace Core::Remediation;

    std::error_code errorCode;
    const std::filesystem::path root = std::filesystem::path("build") / "field-closure-tests";
    std::filesystem::remove_all(root, errorCode);

    {
        FieldClosureRequest invalidRequest{};
        const Result<FieldClosureResult> result = ReRunFullFieldAuditAndDiffAgainstBaseline(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldClosureRequest unsupportedScope{};
        unsupportedScope.Scope = "rerun-diff-baseline-v2";
        unsupportedScope.OutputDirectory = root / "unsupported";
        unsupportedScope.RemediationBatchId = "batch-030501";
        unsupportedScope.BaselineRevision = "phase29-final";
        unsupportedScope.CurrentRevision = "phase30-rerun-001";
        unsupportedScope.BaselineCheckpointIds = {"phase29-final", "phase30-3"};
        unsupportedScope.BaselineFindings = {BuildFinding(
            "finding-baseline-unsupported",
            "FIELD_AUDIT_RULE_RUNTIME_CONTRACT",
            "Runtime::Player::Health",
            "runtime<->serialized",
            FieldClosureSeverity::High,
            "runtime-systems",
            "gameplay",
            {BuildEvidence("evidence-baseline", "reports/baseline-runtime-health.json", "cd9912ff13")})};
        unsupportedScope.RerunFindings = {BuildFinding(
            "finding-rerun-unsupported",
            "FIELD_AUDIT_RULE_RUNTIME_CONTRACT",
            "Runtime::Player::Health",
            "runtime<->serialized",
            FieldClosureSeverity::High,
            "runtime-systems",
            "gameplay",
            {BuildEvidence("evidence-rerun", "reports/rerun-runtime-health.json", "dd0022ab18")})};
        const Result<FieldClosureResult> result = ReRunFullFieldAuditAndDiffAgainstBaseline(unsupportedScope);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        FieldClosureRequest request{};
        request.Scope = "rerun-diff-baseline";
        request.OutputDirectory = root / "success";
        request.RemediationBatchId = "batch-030501";
        request.BaselineRevision = "phase29-final";
        request.CurrentRevision = "phase30-rerun-001";
        request.BaselineCheckpointIds = {"phase30-3", "phase29-final", "phase30-3"};

        request.BaselineFindings = {
            BuildFinding("finding-baseline-resolved",
                         "FIELD_AUDIT_RULE_RUNTIME_BINDING_PARITY",
                         "Runtime::Player::Health",
                         "runtime<->serialized",
                         FieldClosureSeverity::High,
                         "runtime-systems",
                         "gameplay",
                         {BuildEvidence("evidence-baseline-resolved",
                                        "reports/baseline-runtime-health.json",
                                        "cd9912ff13")}),
            BuildFinding("finding-baseline-regressed",
                         "FIELD_AUDIT_RULE_REPLICATION_SCHEMA_DRIFT",
                         "Runtime::Inventory::Version",
                         "runtime<->network",
                         FieldClosureSeverity::Medium,
                         "network-systems",
                         "multiplayer",
                         {BuildEvidence("evidence-baseline-regressed",
                                        "reports/baseline-runtime-inventory.json",
                                        "aa3211ee77")}),
            BuildFinding("finding-baseline-unchanged",
                         "FIELD_AUDIT_RULE_BUILD_MANIFEST_ALIAS_DRIFT",
                         "Build::Manifest::CatalogKey",
                         "build<->runtime",
                         FieldClosureSeverity::Low,
                         "build-release",
                         "release-engineering",
                         {BuildEvidence("evidence-baseline-unchanged",
                                        "reports/baseline-build-catalog.json",
                                        "e11833bcd1")}),
            BuildFinding("finding-baseline-regressed-duplicate",
                         "FIELD_AUDIT_RULE_REPLICATION_SCHEMA_DRIFT",
                         "Runtime::Inventory::Version",
                         "runtime<->network",
                         FieldClosureSeverity::Low,
                         "network-systems",
                         "multiplayer",
                         {BuildEvidence("evidence-baseline-regressed-duplicate",
                                        "reports/baseline-runtime-inventory-duplicate.json",
                                        "aa3211ee77")})};

        request.RerunFindings = {
            BuildFinding("finding-rerun-regressed",
                         "FIELD_AUDIT_RULE_REPLICATION_SCHEMA_DRIFT",
                         "Runtime::Inventory::Version",
                         "runtime<->network",
                         FieldClosureSeverity::Critical,
                         "network-systems",
                         "multiplayer",
                         {BuildEvidence("evidence-rerun-regressed",
                                        "reports/rerun-runtime-inventory.json",
                                        "af4577bb21")}),
            BuildFinding("finding-rerun-unchanged",
                         "FIELD_AUDIT_RULE_BUILD_MANIFEST_ALIAS_DRIFT",
                         "Build::Manifest::CatalogKey",
                         "build<->runtime",
                         FieldClosureSeverity::Low,
                         "build-release",
                         "release-engineering",
                         {BuildEvidence("evidence-rerun-unchanged", "reports/rerun-build-catalog.json", "1dd22119a0")}),
            BuildFinding("finding-rerun-new",
                         "FIELD_AUDIT_RULE_TOOLING_SCHEMA_DRIFT",
                         "Tooling::Editor::WidgetContract",
                         "tooling<->runtime",
                         FieldClosureSeverity::High,
                         "tooling-systems",
                         "editor-platform",
                         {BuildEvidence("evidence-rerun-new", "reports/rerun-tooling-widget.json", "9fd67b2cc5")}),
            BuildFinding("finding-rerun-new-duplicate",
                         "FIELD_AUDIT_RULE_TOOLING_SCHEMA_DRIFT",
                         "Tooling::Editor::WidgetContract",
                         "tooling<->runtime",
                         FieldClosureSeverity::Medium,
                         "tooling-systems",
                         "editor-platform",
                         {BuildEvidence("evidence-rerun-new-duplicate",
                                        "reports/rerun-tooling-widget-duplicate.json",
                                        "9fd67b2cc5")})};

        const Result<FieldClosureResult> first = ReRunFullFieldAuditAndDiffAgainstBaseline(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.RemediationBatchId == request.RemediationBatchId);
        assert(first.Value.BaselineRevision == request.BaselineRevision);
        assert(first.Value.CurrentRevision == request.CurrentRevision);
        assert(first.Value.BaselineCheckpointIds.size() == 2u);
        assert(first.Value.BaselineCheckpointIds[0] == "phase29-final");
        assert(first.Value.BaselineCheckpointIds[1] == "phase30-3");
        assert(first.Value.Summary.BaselineFindingCount == 3u);
        assert(first.Value.Summary.RerunFindingCount == 3u);
        assert(first.Value.Summary.ResolvedFindingCount == 1u);
        assert(first.Value.Summary.RegressedFindingCount == 1u);
        assert(first.Value.Summary.NewFindingCount == 1u);
        assert(first.Value.Summary.UnchangedFindingCount == 1u);
        assert(first.Value.Summary.TotalDiffCount == 4u);
        assert(first.Value.Summary.BaselineCheckpointCount == 2u);
        assert(first.Value.Summary.CriticalFindingCount == 1u);
        assert(!first.Value.DeterministicDigest.empty());
        assert(first.Value.DiffRecords.size() == 4u);

        bool sawResolved = false;
        bool sawRegressed = false;
        bool sawNew = false;
        bool sawUnchanged = false;
        for (const FieldClosureDiffRecord& record : first.Value.DiffRecords) {
            assert(!record.DiffId.empty());
            assert(!record.DeterministicDigest.empty());
            assert(!record.FindingId.empty());
            assert(!record.RuleId.empty());
            assert(!record.StableFieldKey.empty());
            assert(!record.DomainPair.empty());
            assert(!record.Ownership.OwnerSubsystem.empty());
            assert(!record.DiffSummary.empty());
            assert(record.ActionableDiff.size() >= 2u);
            assert(!record.EvidenceIndex.empty());
            if (record.DiffKind == FieldClosureDiffKind::Resolved) {
                sawResolved = true;
                assert(record.CurrentSeverity == FieldClosureSeverity::Info);
            } else if (record.DiffKind == FieldClosureDiffKind::Regressed) {
                sawRegressed = true;
                assert(record.BaselineSeverity == FieldClosureSeverity::Medium);
                assert(record.CurrentSeverity == FieldClosureSeverity::Critical);
                assert(record.Ownership.OwnerSubsystem == "network-systems");
            } else if (record.DiffKind == FieldClosureDiffKind::New) {
                sawNew = true;
                assert(record.BaselineSeverity == FieldClosureSeverity::Info);
                assert(record.CurrentSeverity == FieldClosureSeverity::High);
                assert(record.Ownership.OwnerSubsystem == "tooling-systems");
            } else if (record.DiffKind == FieldClosureDiffKind::Unchanged) {
                sawUnchanged = true;
                assert(record.BaselineSeverity == FieldClosureSeverity::Low);
                assert(record.CurrentSeverity == FieldClosureSeverity::Low);
                assert(record.Ownership.OwnerSubsystem == "build-release");
            }
        }
        assert(sawResolved);
        assert(sawRegressed);
        assert(sawNew);
        assert(sawUnchanged);

        const Result<FieldClosureResult> second = ReRunFullFieldAuditAndDiffAgainstBaseline(request);
        assert(second.Ok);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.DiffRecords.size() == first.Value.DiffRecords.size());
        for (std::size_t index = 0; index < first.Value.DiffRecords.size(); ++index) {
            assert(second.Value.DiffRecords[index].DiffId == first.Value.DiffRecords[index].DiffId);
            assert(second.Value.DiffRecords[index].DeterministicDigest ==
                   first.Value.DiffRecords[index].DeterministicDigest);
            assert(second.Value.DiffRecords[index].DiffKind == first.Value.DiffRecords[index].DiffKind);
            assert(second.Value.DiffRecords[index].FindingId == first.Value.DiffRecords[index].FindingId);
            assert(second.Value.DiffRecords[index].RuleId == first.Value.DiffRecords[index].RuleId);
            assert(second.Value.DiffRecords[index].StableFieldKey == first.Value.DiffRecords[index].StableFieldKey);
            assert(second.Value.DiffRecords[index].DomainPair == first.Value.DiffRecords[index].DomainPair);
            assert(second.Value.DiffRecords[index].BaselineSeverity == first.Value.DiffRecords[index].BaselineSeverity);
            assert(second.Value.DiffRecords[index].CurrentSeverity == first.Value.DiffRecords[index].CurrentSeverity);
            assert(second.Value.DiffRecords[index].DiffSummary == first.Value.DiffRecords[index].DiffSummary);
            assert(second.Value.DiffRecords[index].ActionableDiff == first.Value.DiffRecords[index].ActionableDiff);
            assert(second.Value.DiffRecords[index].EvidenceIndex.size() == first.Value.DiffRecords[index].EvidenceIndex.size());
            for (std::size_t evidenceIndex = 0; evidenceIndex < first.Value.DiffRecords[index].EvidenceIndex.size();
                 ++evidenceIndex) {
                assert(second.Value.DiffRecords[index].EvidenceIndex[evidenceIndex].EvidenceId ==
                       first.Value.DiffRecords[index].EvidenceIndex[evidenceIndex].EvidenceId);
                assert(second.Value.DiffRecords[index].EvidenceIndex[evidenceIndex].EvidencePath ==
                       first.Value.DiffRecords[index].EvidenceIndex[evidenceIndex].EvidencePath);
                assert(second.Value.DiffRecords[index].EvidenceIndex[evidenceIndex].EvidenceDigest ==
                       first.Value.DiffRecords[index].EvidenceIndex[evidenceIndex].EvidenceDigest);
            }
        }
    }

    return 0;
}
