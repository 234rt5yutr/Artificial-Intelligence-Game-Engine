#include "Core/Audit/FieldAuditReportWriter.h"

#include "Core/Audit/FieldIssueLedger.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
    run.OutputDirectory = std::filesystem::path("build") / "field-audit-report-writer-tests" / scope;
    run.PhaseStamps = std::move(phases);
    run.TotalPhases = static_cast<uint32_t>(run.PhaseStamps.size());
    for (const Core::Audit::FieldAuditPhaseStamp& phase : run.PhaseStamps) {
        run.TotalFindingCount += phase.TotalFindingCount;
    }
    run.DeterministicDigest = "run-digest-" + scope;
    return run;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::string content;
    if (!stream) {
        return content;
    }
    stream.seekg(0, std::ios::end);
    content.resize(static_cast<std::size_t>(stream.tellg()));
    stream.seekg(0, std::ios::beg);
    stream.read(content.data(), static_cast<std::streamsize>(content.size()));
    return content;
}

} // namespace

int main() {
    using namespace Core::Audit;

    std::error_code errorCode;
    const std::filesystem::path root = std::filesystem::path("build") / "field-audit-report-writer-tests";
    std::filesystem::remove_all(root, errorCode);

    {
        FieldAuditComplianceReportRequest invalidRequest{};
        const Result<FieldAuditComplianceReport> result = ExportFieldAuditComplianceReport(invalidRequest);
        assert(!result.Ok);
        assert(result.Error == "FIELD_AUDIT_ARGUMENT_INVALID");
    }

    {
        FieldAuditComplianceReportRequest unsupportedRequest{};
        unsupportedRequest.Scope = "compliance-report-v2";
        unsupportedRequest.OutputDirectory = root / "unsupported";
        unsupportedRequest.Revision = "rev-001";
        unsupportedRequest.LedgerReport.Scope = "issue-ledger";
        const Result<FieldAuditComplianceReport> result = ExportFieldAuditComplianceReport(unsupportedRequest);
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
        ledgerRequest.Revision = "rev-004";
        ledgerRequest.AuditRuns = {
            BuildRun("runtime-state", {BuildPhase("startup", 1u, {findingTypeMismatch, findingEvolutionMismatch})}),
            BuildRun("network-replay", {BuildPhase("replication-parity", 1u, {findingTypeMismatchDuplicate})})};

        const Result<FieldIssueLedgerReport> severityLedger = ComputeFieldIssueSeverityAndBlastRadius(ledgerRequest);
        assert(severityLedger.Ok);
        assert(!severityLedger.Value.DeterministicDigest.empty());
        assert(!severityLedger.Value.Issues.empty());

        FieldAuditComplianceReportRequest reportRequest{};
        reportRequest.Scope = "compliance-report";
        reportRequest.OutputDirectory = root / "report";
        reportRequest.Revision = ledgerRequest.Revision;
        reportRequest.LedgerReport = severityLedger.Value;

        const Result<FieldAuditComplianceReport> first = ExportFieldAuditComplianceReport(reportRequest);
        assert(first.Ok);
        assert(first.Value.Scope == reportRequest.Scope);
        assert(first.Value.OutputDirectory == reportRequest.OutputDirectory);
        assert(!first.Value.DeterministicDigest.empty());
        assert(std::filesystem::exists(first.Value.JsonReportPath));
        assert(std::filesystem::exists(first.Value.MarkdownReportPath));

        const std::string firstJson = ReadFile(first.Value.JsonReportPath);
        const std::string firstMarkdown = ReadFile(first.Value.MarkdownReportPath);
        assert(!firstJson.empty());
        assert(!firstMarkdown.empty());
        assert(firstJson.find("\"issues\"") != std::string::npos);
        assert(firstMarkdown.find("Field Audit Compliance Report") != std::string::npos);

        const Result<FieldAuditComplianceReport> second = ExportFieldAuditComplianceReport(reportRequest);
        assert(second.Ok);
        assert(second.Value.JsonReportPath == first.Value.JsonReportPath);
        assert(second.Value.MarkdownReportPath == first.Value.MarkdownReportPath);
        assert(second.Value.DeterministicDigest == first.Value.DeterministicDigest);

        const std::string secondJson = ReadFile(second.Value.JsonReportPath);
        const std::string secondMarkdown = ReadFile(second.Value.MarkdownReportPath);
        assert(secondJson == firstJson);
        assert(secondMarkdown == firstMarkdown);
    }

    return 0;
}
