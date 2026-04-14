#include "Core/Audit/FieldRemediationBacklog.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
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

[[nodiscard]] std::string ToLowerCopy(const std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char symbol : value) {
        lowered.push_back(static_cast<char>(std::tolower(symbol)));
    }
    return lowered;
}

[[nodiscard]] FieldRemediationPriority SeverityToPriority(const FieldIssueSeverityLevel severity) {
    switch (severity) {
    case FieldIssueSeverityLevel::Critical:
        return FieldRemediationPriority::P0Critical;
    case FieldIssueSeverityLevel::High:
        return FieldRemediationPriority::P1High;
    case FieldIssueSeverityLevel::Medium:
        return FieldRemediationPriority::P2Medium;
    case FieldIssueSeverityLevel::Low:
        return FieldRemediationPriority::P3Low;
    case FieldIssueSeverityLevel::Info:
    default:
        return FieldRemediationPriority::P4Info;
    }
}

[[nodiscard]] std::string DetermineOwnerSubsystem(const FieldAuditIssueRecord& issue) {
    auto hasDomain = [&issue](const std::string_view domain) {
        return std::find(issue.ImpactedDomains.begin(), issue.ImpactedDomains.end(), domain) != issue.ImpactedDomains.end();
    };

    if (hasDomain("network")) {
        return "network-systems";
    }
    if (hasDomain("runtime")) {
        return "runtime-systems";
    }
    if (hasDomain("persistence")) {
        return "persistence-pipeline";
    }
    if (hasDomain("build")) {
        return "build-pipeline";
    }
    if (hasDomain("tooling")) {
        return "tooling-authoring";
    }
    return "core-audit";
}

[[nodiscard]] std::string DetermineFixCategory(const std::string_view ruleId) {
    const std::string loweredRuleId = ToLowerCopy(ruleId);
    if (loweredRuleId.find("type") != std::string::npos) {
        return "schema-type-alignment";
    }
    if (loweredRuleId.find("nullability") != std::string::npos) {
        return "nullability-contract";
    }
    if (loweredRuleId.find("range") != std::string::npos || loweredRuleId.find("enum") != std::string::npos ||
        loweredRuleId.find("pattern") != std::string::npos || loweredRuleId.find("identifier") != std::string::npos) {
        return "constraint-domain-alignment";
    }
    if (loweredRuleId.find("invariant") != std::string::npos) {
        return "cross-field-invariant";
    }
    if (loweredRuleId.find("evolution") != std::string::npos || loweredRuleId.find("compatibility") != std::string::npos) {
        return "compatibility-migration";
    }
    return "contract-remediation";
}

[[nodiscard]] std::string ComputeTaskDigest(const FieldRemediationTask& task) {
    std::string digestMaterial;
    digestMaterial.reserve((task.EvidencePointers.size() * 40u) + 160u);
    digestMaterial.append(task.TaskId);
    digestMaterial.push_back('|');
    digestMaterial.append(task.IssueId);
    digestMaterial.push_back('|');
    digestMaterial.append(task.OwnerSubsystem);
    digestMaterial.push_back('|');
    digestMaterial.append(task.FixCategory);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(task.Priority)));
    digestMaterial.push_back('|');
    digestMaterial.append(task.Title);
    digestMaterial.push_back('|');
    digestMaterial.append(task.Description);
    digestMaterial.push_back('\n');
    for (const std::string& pointer : task.EvidencePointers) {
        digestMaterial.append(pointer);
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputeBacklogDigest(const FieldRemediationBacklogReport& report) {
    std::string digestMaterial;
    digestMaterial.reserve((report.Tasks.size() * 56u) + 96u);
    digestMaterial.append(report.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(report.Revision);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.TotalTaskCount));
    digestMaterial.push_back('\n');
    for (const FieldRemediationTask& task : report.Tasks) {
        digestMaterial.append(task.TaskId);
        digestMaterial.push_back('|');
        digestMaterial.append(task.DeterministicDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(static_cast<uint32_t>(task.Priority)));
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

} // namespace

Result<FieldRemediationBacklogReport> CreateFieldRemediationBacklogFromAudit(const FieldRemediationBacklogRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.Revision.empty()) {
        return Result<FieldRemediationBacklogReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "remediation-backlog") {
        return Result<FieldRemediationBacklogReport>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    if (request.LedgerReport.Scope.empty()) {
        return Result<FieldRemediationBacklogReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldRemediationBacklogReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<FieldRemediationTask> tasks;
    tasks.reserve(request.LedgerReport.Issues.size());
    for (const FieldAuditIssueRecord& issue : request.LedgerReport.Issues) {
        FieldRemediationTask task{};
        task.IssueId = issue.IssueId;
        task.OwnerSubsystem = DetermineOwnerSubsystem(issue);
        task.FixCategory = DetermineFixCategory(issue.RuleId);
        task.Priority = SeverityToPriority(issue.Severity);
        task.TaskId = HashToHex(HashString("task|" + issue.IssueId + "|" + task.OwnerSubsystem + "|" + task.FixCategory));
        task.Title = "Remediate " + issue.RuleId + " on " + issue.StableFieldKey;
        task.Description = "Fix category: " + task.FixCategory + "; owner: " + task.OwnerSubsystem +
                           "; severity rationale: " + issue.SeverityRationale + "; revision: " + request.Revision;

        task.EvidencePointers.reserve(issue.EvidenceReferences.size());
        for (std::size_t evidenceIndex = 0; evidenceIndex < issue.EvidenceReferences.size(); ++evidenceIndex) {
            task.EvidencePointers.push_back("evidence://" + issue.IssueId + "/" + std::to_string(evidenceIndex));
        }
        std::sort(task.EvidencePointers.begin(), task.EvidencePointers.end());
        task.DeterministicDigest = ComputeTaskDigest(task);
        tasks.push_back(std::move(task));
    }

    std::sort(tasks.begin(), tasks.end(), [](const FieldRemediationTask& left, const FieldRemediationTask& right) {
        if (left.Priority != right.Priority) {
            return static_cast<uint32_t>(left.Priority) < static_cast<uint32_t>(right.Priority);
        }
        if (left.OwnerSubsystem != right.OwnerSubsystem) {
            return left.OwnerSubsystem < right.OwnerSubsystem;
        }
        if (left.FixCategory != right.FixCategory) {
            return left.FixCategory < right.FixCategory;
        }
        return left.TaskId < right.TaskId;
    });

    FieldRemediationBacklogReport report{};
    report.Scope = request.Scope;
    report.OutputDirectory = request.OutputDirectory;
    report.Revision = request.Revision;
    report.Tasks = std::move(tasks);
    report.TotalTaskCount = static_cast<uint32_t>(report.Tasks.size());
    report.DeterministicDigest = ComputeBacklogDigest(report);
    return Result<FieldRemediationBacklogReport>::Success(std::move(report));
}

} // namespace Core::Audit
