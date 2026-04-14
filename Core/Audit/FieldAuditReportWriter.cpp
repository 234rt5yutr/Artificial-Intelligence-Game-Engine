#include "Core/Audit/FieldAuditReportWriter.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Core::Audit {
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

[[nodiscard]] std::string SeverityToString(const FieldIssueSeverityLevel severity) {
    switch (severity) {
    case FieldIssueSeverityLevel::Critical:
        return "critical";
    case FieldIssueSeverityLevel::High:
        return "high";
    case FieldIssueSeverityLevel::Medium:
        return "medium";
    case FieldIssueSeverityLevel::Low:
        return "low";
    case FieldIssueSeverityLevel::Info:
    default:
        return "info";
    }
}

[[nodiscard]] std::string BuildMarkdownReport(const FieldAuditComplianceReportRequest& request,
                                              const std::vector<FieldAuditIssueRecord>& issues,
                                              const std::string& reportDigest,
                                              const std::filesystem::path& jsonPath) {
    std::ostringstream markdown;
    markdown << "# Field Audit Compliance Report\n\n";
    markdown << "- Scope: `" << request.Scope << "`\n";
    markdown << "- Revision: `" << request.Revision << "`\n";
    markdown << "- Raw findings: `" << request.LedgerReport.Summary.RawFindingCount << "`\n";
    markdown << "- Deduplicated issues: `" << request.LedgerReport.Summary.DeduplicatedIssueCount << "`\n";
    markdown << "- Deterministic digest: `" << reportDigest << "`\n";
    markdown << "- Machine-readable report: `" << jsonPath.string() << "`\n\n";

    markdown << "| Issue ID | Severity | Blast Radius | Rule | Occurrences | Domains |\n";
    markdown << "| --- | --- | --- | --- | --- | --- |\n";
    for (const FieldAuditIssueRecord& issue : issues) {
        markdown << "| `" << issue.IssueId << "` | `" << SeverityToString(issue.Severity) << "` | `"
                 << issue.BlastRadiusScore << "` | `" << issue.RuleId << "` | `" << issue.OccurrenceCount << "` | `";
        for (std::size_t index = 0; index < issue.ImpactedDomains.size(); ++index) {
            if (index > 0u) {
                markdown << ",";
            }
            markdown << issue.ImpactedDomains[index];
        }
        markdown << "` |\n";
    }
    markdown << "\n";

    for (const FieldAuditIssueRecord& issue : issues) {
        markdown << "## Issue `" << issue.IssueId << "`\n\n";
        markdown << "- Rule: `" << issue.RuleId << "`\n";
        markdown << "- Stable key: `" << issue.StableFieldKey << "`\n";
        markdown << "- Domain pair: `" << issue.DomainPair << "`\n";
        markdown << "- First seen revision: `" << issue.FirstSeenRevision << "`\n";
        markdown << "- Severity: `" << SeverityToString(issue.Severity) << "`\n";
        markdown << "- Blast radius score: `" << issue.BlastRadiusScore << "`\n";
        markdown << "- Severity rationale: `" << issue.SeverityRationale << "`\n";
        markdown << "- Deterministic issue digest: `" << issue.DeterministicDigest << "`\n\n";

        markdown << "### Evidence References\n\n";
        markdown << "| Pointer | Run | Phase | Left Field | Right Field |\n";
        markdown << "| --- | --- | --- | --- | --- |\n";
        for (std::size_t evidenceIndex = 0; evidenceIndex < issue.EvidenceReferences.size(); ++evidenceIndex) {
            const FieldIssueEvidenceReference& evidence = issue.EvidenceReferences[evidenceIndex];
            markdown << "| `evidence://" << issue.IssueId << "/" << evidenceIndex << "` | `" << evidence.RunScope
                     << "` | `" << evidence.PhaseId << "` | `" << evidence.LeftFieldId << "` | `"
                     << evidence.RightFieldId << "` |\n";
        }
        markdown << "\n";
    }

    return markdown.str();
}

} // namespace

Result<FieldAuditComplianceReport> ExportFieldAuditComplianceReport(const FieldAuditComplianceReportRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.Revision.empty()) {
        return Result<FieldAuditComplianceReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "compliance-report") {
        return Result<FieldAuditComplianceReport>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    if (request.LedgerReport.Scope.empty()) {
        return Result<FieldAuditComplianceReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldAuditComplianceReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<FieldAuditIssueRecord> issues = request.LedgerReport.Issues;
    std::sort(issues.begin(), issues.end(), [](const FieldAuditIssueRecord& left, const FieldAuditIssueRecord& right) {
        if (left.Severity != right.Severity) {
            return static_cast<uint32_t>(left.Severity) > static_cast<uint32_t>(right.Severity);
        }
        if (left.BlastRadiusScore != right.BlastRadiusScore) {
            return left.BlastRadiusScore > right.BlastRadiusScore;
        }
        return left.IssueId < right.IssueId;
    });

    nlohmann::json jsonReport;
    jsonReport["scope"] = request.Scope;
    jsonReport["revision"] = request.Revision;
    jsonReport["summary"] = {
        {"raw_finding_count", request.LedgerReport.Summary.RawFindingCount},
        {"deduplicated_issue_count", request.LedgerReport.Summary.DeduplicatedIssueCount},
        {"source_ledger_digest", request.LedgerReport.DeterministicDigest}};
    jsonReport["issues"] = nlohmann::json::array();

    for (const FieldAuditIssueRecord& issue : issues) {
        nlohmann::json issueJson;
        issueJson["issue_id"] = issue.IssueId;
        issueJson["rule_id"] = issue.RuleId;
        issueJson["stable_field_key"] = issue.StableFieldKey;
        issueJson["domain_pair"] = issue.DomainPair;
        issueJson["first_seen_revision"] = issue.FirstSeenRevision;
        issueJson["occurrence_count"] = issue.OccurrenceCount;
        issueJson["severity"] = SeverityToString(issue.Severity);
        issueJson["blast_radius_score"] = issue.BlastRadiusScore;
        issueJson["impacted_domains"] = issue.ImpactedDomains;
        issueJson["severity_rationale"] = issue.SeverityRationale;
        issueJson["deterministic_issue_digest"] = issue.DeterministicDigest;

        nlohmann::json evidenceJson = nlohmann::json::array();
        for (std::size_t evidenceIndex = 0; evidenceIndex < issue.EvidenceReferences.size(); ++evidenceIndex) {
            const FieldIssueEvidenceReference& evidence = issue.EvidenceReferences[evidenceIndex];
            evidenceJson.push_back({
                {"pointer", "evidence://" + issue.IssueId + "/" + std::to_string(evidenceIndex)},
                {"run_scope", evidence.RunScope},
                {"phase_id", evidence.PhaseId},
                {"rule_id", evidence.RuleId},
                {"stable_field_key", evidence.StableFieldKey},
                {"domain_pair", evidence.DomainPair},
                {"left_field_id", evidence.LeftFieldId},
                {"right_field_id", evidence.RightFieldId},
                {"migration_placeholder", evidence.MigrationRecommendationPlaceholder}});
        }

        issueJson["evidence"] = std::move(evidenceJson);
        jsonReport["issues"].push_back(std::move(issueJson));
    }

    const std::filesystem::path jsonPath = request.OutputDirectory / "field-audit-compliance-report.json";
    const std::filesystem::path markdownPath = request.OutputDirectory / "field-audit-compliance-report.md";
    const std::string jsonContent = jsonReport.dump(2);
    const std::string reportDigest = HashToHex(HashString(request.Scope + "|" + request.Revision + "|" + jsonContent));
    const std::string markdownContent = BuildMarkdownReport(request, issues, reportDigest, jsonPath);

    std::ofstream jsonFile(jsonPath, std::ios::binary | std::ios::trunc);
    if (!jsonFile) {
        return Result<FieldAuditComplianceReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }
    jsonFile << jsonContent;
    jsonFile.flush();
    if (!jsonFile) {
        return Result<FieldAuditComplianceReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::ofstream markdownFile(markdownPath, std::ios::binary | std::ios::trunc);
    if (!markdownFile) {
        return Result<FieldAuditComplianceReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }
    markdownFile << markdownContent;
    markdownFile.flush();
    if (!markdownFile) {
        return Result<FieldAuditComplianceReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    FieldAuditComplianceReport report{};
    report.Scope = request.Scope;
    report.OutputDirectory = request.OutputDirectory;
    report.JsonReportPath = jsonPath;
    report.MarkdownReportPath = markdownPath;
    report.DeterministicDigest = HashToHex(HashString(reportDigest + "|" + markdownContent));
    return Result<FieldAuditComplianceReport>::Success(std::move(report));
}

} // namespace Core::Audit
