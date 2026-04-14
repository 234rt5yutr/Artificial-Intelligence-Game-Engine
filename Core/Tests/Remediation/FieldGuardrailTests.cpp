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

    return 0;
}
