#include "Core/Remediation/FieldGuardrailService.h"

#include <cassert>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

Core::Remediation::FieldGuardrailEntry BuildEntry(const Core::Remediation::FieldGuardrailDomain domain,
                                                  const std::string& stableFieldKey,
                                                  const std::string& domainPair,
                                                  const std::string& targetFieldId,
                                                  const std::string& findingId,
                                                  const std::string& ruleId,
                                                  const std::string& owner) {
    Core::Remediation::FieldGuardrailEntry entry{};
    entry.Domain = domain;
    entry.StableFieldKey = stableFieldKey;
    entry.DomainPair = domainPair;
    entry.TargetFieldId = targetFieldId;
    entry.PropertyPath = "guardrail.invariant";
    entry.AssertionExpression = "assert(field.contract == canonical)";
    entry.ExpectedAssertionExpression = "assert(field.contract == canonical)";
    entry.Rationale = "Ensure invariant ownership and lineage remain attached to findings.";
    entry.Taxonomy.TaxonomyId = "taxonomy.field.invariant";
    entry.Taxonomy.Category = "field-integrity";
    entry.Taxonomy.Invariant = "ownership-lineage";
    entry.Taxonomy.Lineage.FindingId = findingId;
    entry.Taxonomy.Lineage.RuleId = ruleId;
    entry.Taxonomy.Lineage.Owner = owner;
    return entry;
}

Core::Remediation::FieldGuardrailEntry BuildRegressionEntry(const Core::Remediation::FieldGuardrailDomain domain,
                                                            const std::string& stableFieldKey,
                                                            const std::string& domainPair,
                                                            const std::string& targetFieldId,
                                                            const std::string& findingId,
                                                            const std::string& ruleId,
                                                            const std::string& owner,
                                                            const std::string& suiteId,
                                                            const std::vector<std::string>& coverageMap) {
    Core::Remediation::FieldGuardrailEntry entry =
        BuildEntry(domain, stableFieldKey, domainPair, targetFieldId, findingId, ruleId, owner);
    entry.RegressionSuite.SuiteId = suiteId;
    entry.RegressionSuite.Stage30CoverageMap = coverageMap;
    return entry;
}

Core::Remediation::FieldGuardrailEntry BuildAuditGateEntry(const Core::Remediation::FieldGuardrailDomain domain,
                                                           const std::string& stableFieldKey,
                                                           const std::string& domainPair,
                                                           const std::string& targetFieldId,
                                                           const std::string& findingId,
                                                           const std::string& ruleId,
                                                           const std::string& owner,
                                                           const std::string& policyId,
                                                           const bool isReleaseLane,
                                                           const Core::Remediation::FieldAuditSeverity severity,
                                                           const Core::Remediation::FieldAuditFindingStatus status) {
    Core::Remediation::FieldGuardrailEntry entry =
        BuildEntry(domain, stableFieldKey, domainPair, targetFieldId, findingId, ruleId, owner);
    entry.AuditPolicy.PolicyId = policyId;
    entry.AuditPolicy.ReleaseLane = isReleaseLane;
    entry.AuditPolicy.Severity = severity;
    entry.AuditPolicy.FindingStatus = status;
    return entry;
}

} // namespace

int main() {
    using namespace Core::Remediation;

    std::error_code errorCode;
    const std::filesystem::path root = std::filesystem::path("build") / "field-guardrail-tests";
    std::filesystem::remove_all(root, errorCode);

    {
        FieldGuardrailRequest invalidRequest{};
        const Result<FieldGuardrailResult> result = AddFieldInvariantAssertions(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldGuardrailRequest unsupportedScope{};
        unsupportedScope.Scope = "field-invariant-assertions-v2";
        unsupportedScope.OutputDirectory = root / "unsupported";
        unsupportedScope.RemediationBatchId = "batch-030401";
        unsupportedScope.Entries = {
            BuildEntry(FieldGuardrailDomain::Runtime,
                       "Runtime::Player::Health",
                       "runtime<->editor",
                       "runtime::Player::Health",
                       "guardrail-finding-runtime",
                       "FIELD_AUDIT_RULE_RUNTIME_INVARIANT_DRIFT",
                       "runtime-systems")};
        const Result<FieldGuardrailResult> result = AddFieldInvariantAssertions(unsupportedScope);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        FieldGuardrailRequest request{};
        request.Scope = "field-invariant-assertions";
        request.OutputDirectory = root / "success";
        request.RemediationBatchId = "batch-030401";

        const FieldGuardrailEntry runtimeEntry = BuildEntry(FieldGuardrailDomain::Runtime,
                                                            "Runtime::Player::Health",
                                                            "runtime<->editor",
                                                            "runtime::Player::Health",
                                                            "guardrail-finding-runtime",
                                                            "FIELD_AUDIT_RULE_RUNTIME_INVARIANT_DRIFT",
                                                            "runtime-systems");

        FieldGuardrailEntry editorEntry = BuildEntry(FieldGuardrailDomain::Editor,
                                                     "Editor::Player::Health",
                                                     "editor<->runtime",
                                                     "editor::Player::Health",
                                                     "guardrail-finding-editor",
                                                     "FIELD_AUDIT_RULE_EDITOR_INVARIANT_DRIFT",
                                                     "editor-systems");
        editorEntry.PropertyPath = "guardrail.editorInvariant";

        FieldGuardrailEntry buildEntry = BuildEntry(FieldGuardrailDomain::Build,
                                                    "Build::Manifest::Player::Health",
                                                    "build<->runtime",
                                                    "build::Manifest::Player::Health",
                                                    "guardrail-finding-build",
                                                    "FIELD_AUDIT_RULE_BUILD_INVARIANT_DRIFT",
                                                    "build-systems");
        buildEntry.PropertyPath = "guardrail.buildInvariant";

        request.Entries = {buildEntry, runtimeEntry, editorEntry, runtimeEntry};

        const Result<FieldGuardrailResult> first = AddFieldInvariantAssertions(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.RemediationBatchId == request.RemediationBatchId);
        assert(first.Value.Summary.RuntimeAssertionCount == 1u);
        assert(first.Value.Summary.EditorAssertionCount == 1u);
        assert(first.Value.Summary.BuildAssertionCount == 1u);
        assert(first.Value.Summary.TotalAssertionCount == 3u);
        assert(first.Value.AssertionRecords.size() == 3u);
        assert(!first.Value.DeterministicDigest.empty());

        bool sawRuntime = false;
        bool sawEditor = false;
        bool sawBuild = false;
        for (const FieldGuardrailRecord& record : first.Value.AssertionRecords) {
            assert(!record.AssertionId.empty());
            assert(!record.DeterministicDigest.empty());
            assert(!record.Taxonomy.TaxonomyId.empty());
            assert(!record.Taxonomy.Category.empty());
            assert(!record.Taxonomy.Invariant.empty());
            assert(!record.Taxonomy.Lineage.FindingId.empty());
            assert(!record.Taxonomy.Lineage.RuleId.empty());
            assert(!record.Taxonomy.Lineage.Owner.empty());
            assert(record.Taxonomy.Lineage.RemediationBatchId == request.RemediationBatchId);

            if (record.Domain == FieldGuardrailDomain::Runtime) {
                sawRuntime = true;
                assert(record.Taxonomy.Lineage.Owner == "runtime-systems");
                assert(record.Taxonomy.Lineage.FindingId == "guardrail-finding-runtime");
                assert(record.Taxonomy.Lineage.RuleId == "FIELD_AUDIT_RULE_RUNTIME_INVARIANT_DRIFT");
            } else if (record.Domain == FieldGuardrailDomain::Editor) {
                sawEditor = true;
                assert(record.Taxonomy.Lineage.Owner == "editor-systems");
                assert(record.Taxonomy.Lineage.FindingId == "guardrail-finding-editor");
                assert(record.Taxonomy.Lineage.RuleId == "FIELD_AUDIT_RULE_EDITOR_INVARIANT_DRIFT");
            } else if (record.Domain == FieldGuardrailDomain::Build) {
                sawBuild = true;
                assert(record.Taxonomy.Lineage.Owner == "build-systems");
                assert(record.Taxonomy.Lineage.FindingId == "guardrail-finding-build");
                assert(record.Taxonomy.Lineage.RuleId == "FIELD_AUDIT_RULE_BUILD_INVARIANT_DRIFT");
            }
        }
        assert(sawRuntime);
        assert(sawEditor);
        assert(sawBuild);

        const Result<FieldGuardrailResult> second = AddFieldInvariantAssertions(request);
        assert(second.Ok);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.AssertionRecords.size() == first.Value.AssertionRecords.size());
        for (std::size_t index = 0; index < first.Value.AssertionRecords.size(); ++index) {
            assert(second.Value.AssertionRecords[index].AssertionId == first.Value.AssertionRecords[index].AssertionId);
            assert(second.Value.AssertionRecords[index].DeterministicDigest ==
                   first.Value.AssertionRecords[index].DeterministicDigest);
            assert(second.Value.AssertionRecords[index].Taxonomy.Lineage.FindingId ==
                   first.Value.AssertionRecords[index].Taxonomy.Lineage.FindingId);
            assert(second.Value.AssertionRecords[index].Taxonomy.Lineage.RuleId ==
                   first.Value.AssertionRecords[index].Taxonomy.Lineage.RuleId);
            assert(second.Value.AssertionRecords[index].Taxonomy.Lineage.Owner ==
                   first.Value.AssertionRecords[index].Taxonomy.Lineage.Owner);
        }
    }

    {
        FieldGuardrailRequest invalidRequest{};
        const Result<FieldGuardrailResult> result = AddFieldContractRegressionSuites(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldGuardrailRequest unsupportedScope{};
        unsupportedScope.Scope = "field-contract-regression-suites-v2";
        unsupportedScope.OutputDirectory = root / "regression-unsupported";
        unsupportedScope.RemediationBatchId = "batch-030402";
        unsupportedScope.Entries = {BuildRegressionEntry(FieldGuardrailDomain::Runtime,
                                                         "Runtime::Inventory::Version",
                                                         "runtime<->persistence",
                                                         "runtime::Inventory::Version",
                                                         "guardrail-finding-regression-runtime",
                                                         "FIELD_AUDIT_RULE_RUNTIME_REGRESSION_COVERAGE",
                                                         "runtime-systems",
                                                         "suite.stage30.contracts",
                                                         {"30.3.1", "30.3.3"})};
        const Result<FieldGuardrailResult> result = AddFieldContractRegressionSuites(unsupportedScope);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        FieldGuardrailRequest request{};
        request.Scope = "field-contract-regression-suites";
        request.OutputDirectory = root / "regression-success";
        request.RemediationBatchId = "batch-030402";

        FieldGuardrailEntry correctionEntry = BuildRegressionEntry(FieldGuardrailDomain::Runtime,
                                                                   "Runtime::Inventory::Version",
                                                                   "runtime<->persistence",
                                                                   "runtime::Inventory::Version",
                                                                   "guardrail-finding-regression-runtime",
                                                                   "FIELD_AUDIT_RULE_RUNTIME_REGRESSION_COVERAGE",
                                                                   "runtime-systems",
                                                                   "suite.stage30.contracts",
                                                                   {"30.3.1", "30.3.3"});
        correctionEntry.PropertyPath = "guardrail.regression.runtime";
        correctionEntry.AssertionExpression = "assert(contract.version == stale)";
        correctionEntry.ExpectedAssertionExpression = "assert(contract.version == canonical)";
        correctionEntry.Rationale = "correct-stage30-coverage-map";

        FieldGuardrailEntry registerEntry = BuildRegressionEntry(FieldGuardrailDomain::Build,
                                                                 "Build::Manifest::Inventory::Version",
                                                                 "build<->packaging",
                                                                 "build::Manifest::Inventory::Version",
                                                                 "guardrail-finding-regression-build",
                                                                 "FIELD_AUDIT_RULE_BUILD_REGRESSION_COVERAGE",
                                                                 "build-systems",
                                                                 "suite.stage30.contracts",
                                                                 {"30.3.2", "30.3.4"});
        registerEntry.PropertyPath = "guardrail.regression.build";
        registerEntry.AssertionExpression = "assert(manifest.contractVersion == canonical)";
        registerEntry.ExpectedAssertionExpression = "assert(manifest.contractVersion == canonical)";
        registerEntry.Rationale = "register-stage30-coverage-map";

        request.Entries = {registerEntry, correctionEntry, correctionEntry};

        const Result<FieldGuardrailResult> first = AddFieldContractRegressionSuites(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.RemediationBatchId == request.RemediationBatchId);
        assert(first.Value.AssertionRecords.size() == 2u);
        assert(first.Value.Summary.TotalAssertionCount == 2u);
        assert(first.Value.Summary.RegressionSuiteCount == 1u);
        assert(first.Value.Summary.RegressionCoverageSignalCount == 4u);
        assert(first.Value.Summary.RegressionCoverageCorrectionCount == 1u);
        assert(!first.Value.DeterministicDigest.empty());

        bool sawCorrection = false;
        bool sawRegistration = false;
        for (const FieldGuardrailRecord& record : first.Value.AssertionRecords) {
            assert(!record.RegressionSuite.SuiteId.empty());
            assert(!record.RegressionSuite.Stage30CoverageMap.empty());
            assert(record.Taxonomy.Lineage.RemediationBatchId == request.RemediationBatchId);
            assert(!record.DeterministicDigest.empty());
            if (record.Rationale == "correct-stage30-coverage-map") {
                sawCorrection = true;
                assert(record.ExistingAssertionExpression == "assert(contract.version == stale)");
                assert(record.AssertionExpression == "assert(contract.version == canonical)");
                assert(record.RegressionSuite.Stage30CoverageMap.size() == 2u);
                assert(record.RegressionSuite.Stage30CoverageMap[0] == "30.3.1");
                assert(record.RegressionSuite.Stage30CoverageMap[1] == "30.3.3");
            } else if (record.Rationale == "register-stage30-coverage-map") {
                sawRegistration = true;
                assert(record.ExistingAssertionExpression == "assert(manifest.contractVersion == canonical)");
                assert(record.AssertionExpression == "assert(manifest.contractVersion == canonical)");
                assert(record.RegressionSuite.Stage30CoverageMap.size() == 2u);
                assert(record.RegressionSuite.Stage30CoverageMap[0] == "30.3.2");
                assert(record.RegressionSuite.Stage30CoverageMap[1] == "30.3.4");
            }
        }
        assert(sawCorrection);
        assert(sawRegistration);

        const Result<FieldGuardrailResult> second = AddFieldContractRegressionSuites(request);
        assert(second.Ok);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.AssertionRecords.size() == first.Value.AssertionRecords.size());
        for (std::size_t index = 0; index < first.Value.AssertionRecords.size(); ++index) {
            assert(second.Value.AssertionRecords[index].AssertionId == first.Value.AssertionRecords[index].AssertionId);
            assert(second.Value.AssertionRecords[index].DeterministicDigest ==
                   first.Value.AssertionRecords[index].DeterministicDigest);
            assert(second.Value.AssertionRecords[index].RegressionSuite.SuiteId ==
                   first.Value.AssertionRecords[index].RegressionSuite.SuiteId);
            assert(second.Value.AssertionRecords[index].RegressionSuite.Stage30CoverageMap ==
                   first.Value.AssertionRecords[index].RegressionSuite.Stage30CoverageMap);
            assert(second.Value.AssertionRecords[index].Taxonomy.Lineage.FindingId ==
                   first.Value.AssertionRecords[index].Taxonomy.Lineage.FindingId);
            assert(second.Value.AssertionRecords[index].Taxonomy.Lineage.RuleId ==
                   first.Value.AssertionRecords[index].Taxonomy.Lineage.RuleId);
            assert(second.Value.AssertionRecords[index].Taxonomy.Lineage.Owner ==
                   first.Value.AssertionRecords[index].Taxonomy.Lineage.Owner);
        }
    }

    {
        FieldGuardrailRequest invalidRequest{};
        const Result<FieldGuardrailResult> result = AddFieldAuditGateToBuildPipeline(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldGuardrailRequest unsupportedScope{};
        unsupportedScope.Scope = "field-audit-gate-build-pipeline-v2";
        unsupportedScope.OutputDirectory = root / "audit-gate-unsupported";
        unsupportedScope.RemediationBatchId = "batch-030403";
        unsupportedScope.Entries = {BuildAuditGateEntry(FieldGuardrailDomain::Build,
                                                        "Build::Manifest::Release::Version",
                                                        "build<->release",
                                                        "build::Manifest::Release::Version",
                                                        "guardrail-finding-audit-build-unsupported",
                                                        "FIELD_AUDIT_RULE_RELEASE_GATE_POLICY",
                                                        "build-release",
                                                        "field-release-lane-critical-high",
                                                        true,
                                                        FieldAuditSeverity::Critical,
                                                        FieldAuditFindingStatus::Unresolved)};
        const Result<FieldGuardrailResult> result = AddFieldAuditGateToBuildPipeline(unsupportedScope);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    {
        FieldGuardrailRequest request{};
        request.Scope = "field-audit-gate-build-pipeline";
        request.OutputDirectory = root / "audit-gate-success";
        request.RemediationBatchId = "batch-030403";
        request.Entries = {
            BuildAuditGateEntry(FieldGuardrailDomain::Build,
                                "Build::Manifest::Release::Version",
                                "build<->release",
                                "build::Manifest::Release::Version",
                                "guardrail-finding-audit-critical",
                                "FIELD_AUDIT_RULE_RELEASE_GATE_POLICY",
                                "build-release",
                                "field-release-lane-critical-high",
                                true,
                                FieldAuditSeverity::Critical,
                                FieldAuditFindingStatus::Unresolved),
            BuildAuditGateEntry(FieldGuardrailDomain::Runtime,
                                "Runtime::Inventory::Version",
                                "runtime<->release",
                                "runtime::Inventory::Version",
                                "guardrail-finding-audit-high",
                                "FIELD_AUDIT_RULE_RELEASE_GATE_POLICY",
                                "runtime-release",
                                "field-release-lane-critical-high",
                                true,
                                FieldAuditSeverity::High,
                                FieldAuditFindingStatus::Unresolved),
            BuildAuditGateEntry(FieldGuardrailDomain::Runtime,
                                "Runtime::Inventory::Version",
                                "runtime<->release",
                                "runtime::Inventory::Version",
                                "guardrail-finding-audit-high",
                                "FIELD_AUDIT_RULE_RELEASE_GATE_POLICY",
                                "runtime-release",
                                "field-release-lane-critical-high",
                                true,
                                FieldAuditSeverity::High,
                                FieldAuditFindingStatus::Unresolved),
            BuildAuditGateEntry(FieldGuardrailDomain::Editor,
                                "Editor::Inventory::Version",
                                "editor<->release",
                                "editor::Inventory::Version",
                                "guardrail-finding-audit-medium",
                                "FIELD_AUDIT_RULE_RELEASE_GATE_POLICY",
                                "editor-release",
                                "field-release-lane-critical-high",
                                true,
                                FieldAuditSeverity::Medium,
                                FieldAuditFindingStatus::Unresolved),
            BuildAuditGateEntry(FieldGuardrailDomain::Build,
                                "Build::Manifest::Sandbox::Version",
                                "build<->sandbox",
                                "build::Manifest::Sandbox::Version",
                                "guardrail-finding-audit-non-release",
                                "FIELD_AUDIT_RULE_RELEASE_GATE_POLICY",
                                "build-release",
                                "field-release-lane-critical-high",
                                false,
                                FieldAuditSeverity::Critical,
                                FieldAuditFindingStatus::Unresolved),
            BuildAuditGateEntry(FieldGuardrailDomain::Build,
                                "Build::Manifest::Resolved::Version",
                                "build<->release",
                                "build::Manifest::Resolved::Version",
                                "guardrail-finding-audit-resolved",
                                "FIELD_AUDIT_RULE_RELEASE_GATE_POLICY",
                                "build-release",
                                "field-release-lane-critical-high",
                                true,
                                FieldAuditSeverity::Critical,
                                FieldAuditFindingStatus::Resolved)};

        const Result<FieldGuardrailResult> first = AddFieldAuditGateToBuildPipeline(request);
        assert(first.Ok);
        assert(first.Value.Scope == request.Scope);
        assert(first.Value.RemediationBatchId == request.RemediationBatchId);
        assert(first.Value.Summary.TotalAssertionCount == 5u);
        assert(first.Value.Summary.UnresolvedHighFindingCount == 1u);
        assert(first.Value.Summary.UnresolvedCriticalFindingCount == 1u);
        assert(first.Value.Summary.ReleaseGateDecision == FieldAuditGateDecision::Block);
        assert(first.Value.Summary.ReleaseGateBlocked);
        assert(!first.Value.DeterministicDigest.empty());

        bool sawBlockingCritical = false;
        bool sawBlockingHigh = false;
        bool sawNonBlocking = false;
        for (const FieldGuardrailRecord& record : first.Value.AssertionRecords) {
            assert(!record.DeterministicDigest.empty());
            assert(record.Taxonomy.Lineage.RemediationBatchId == request.RemediationBatchId);
            if (record.Taxonomy.Lineage.FindingId == "guardrail-finding-audit-critical") {
                sawBlockingCritical = true;
                assert(record.GateDecision == FieldAuditGateDecision::Block);
            } else if (record.Taxonomy.Lineage.FindingId == "guardrail-finding-audit-high") {
                sawBlockingHigh = true;
                assert(record.GateDecision == FieldAuditGateDecision::Block);
            } else {
                sawNonBlocking = true;
                assert(record.GateDecision == FieldAuditGateDecision::Pass);
            }
        }
        assert(sawBlockingCritical);
        assert(sawBlockingHigh);
        assert(sawNonBlocking);

        const Result<FieldGuardrailResult> second = AddFieldAuditGateToBuildPipeline(request);
        assert(second.Ok);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);
        assert(second.Value.AssertionRecords.size() == first.Value.AssertionRecords.size());
        for (std::size_t index = 0; index < first.Value.AssertionRecords.size(); ++index) {
            assert(second.Value.AssertionRecords[index].AssertionId == first.Value.AssertionRecords[index].AssertionId);
            assert(second.Value.AssertionRecords[index].DeterministicDigest ==
                   first.Value.AssertionRecords[index].DeterministicDigest);
            assert(second.Value.AssertionRecords[index].GateDecision == first.Value.AssertionRecords[index].GateDecision);
            assert(second.Value.AssertionRecords[index].Taxonomy.Lineage.FindingId ==
                   first.Value.AssertionRecords[index].Taxonomy.Lineage.FindingId);
            assert(second.Value.AssertionRecords[index].Taxonomy.Lineage.RuleId ==
                   first.Value.AssertionRecords[index].Taxonomy.Lineage.RuleId);
            assert(second.Value.AssertionRecords[index].Taxonomy.Lineage.Owner ==
                   first.Value.AssertionRecords[index].Taxonomy.Lineage.Owner);
        }
    }

    return 0;
}
