#include "Core/Audit/FieldIssueLedger.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <map>
#include <set>
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

void SortAndDeduplicateEvidenceReferences(std::vector<FieldIssueEvidenceReference>& evidenceReferences) {
    std::sort(evidenceReferences.begin(),
              evidenceReferences.end(),
              [](const FieldIssueEvidenceReference& left, const FieldIssueEvidenceReference& right) {
                  if (left.RunScope != right.RunScope) {
                      return left.RunScope < right.RunScope;
                  }
                  if (left.PhaseId != right.PhaseId) {
                      return left.PhaseId < right.PhaseId;
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
                  if (left.LeftFieldId != right.LeftFieldId) {
                      return left.LeftFieldId < right.LeftFieldId;
                  }
                  return left.RightFieldId < right.RightFieldId;
              });

    evidenceReferences.erase(std::unique(evidenceReferences.begin(),
                                         evidenceReferences.end(),
                                         [](const FieldIssueEvidenceReference& left,
                                            const FieldIssueEvidenceReference& right) {
                                             return left.RunScope == right.RunScope && left.PhaseId == right.PhaseId &&
                                                    left.RuleId == right.RuleId &&
                                                    left.StableFieldKey == right.StableFieldKey &&
                                                    left.DomainPair == right.DomainPair &&
                                                    left.LeftFieldId == right.LeftFieldId &&
                                                    left.RightFieldId == right.RightFieldId &&
                                                    left.MigrationRecommendationPlaceholder ==
                                                        right.MigrationRecommendationPlaceholder;
                                         }),
                           evidenceReferences.end());
}

[[nodiscard]] std::string ComputeIssueDigest(const FieldAuditIssueRecord& issue) {
    std::string digestMaterial;
    digestMaterial.reserve((issue.EvidenceReferences.size() * 96u) + 128u);
    digestMaterial.append(issue.IssueId);
    digestMaterial.push_back('|');
    digestMaterial.append(issue.RuleId);
    digestMaterial.push_back('|');
    digestMaterial.append(issue.StableFieldKey);
    digestMaterial.push_back('|');
    digestMaterial.append(issue.DomainPair);
    digestMaterial.push_back('|');
    digestMaterial.append(issue.FirstSeenRevision);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(issue.OccurrenceCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(issue.Severity)));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(issue.BlastRadiusScore));
    digestMaterial.push_back('|');
    for (std::size_t index = 0; index < issue.ImpactedDomains.size(); ++index) {
        if (index > 0u) {
            digestMaterial.push_back(',');
        }
        digestMaterial.append(issue.ImpactedDomains[index]);
    }
    digestMaterial.push_back('|');
    digestMaterial.append(issue.SeverityRationale);
    digestMaterial.push_back('\n');

    for (const FieldIssueEvidenceReference& evidence : issue.EvidenceReferences) {
        digestMaterial.append(evidence.RunScope);
        digestMaterial.push_back('|');
        digestMaterial.append(evidence.PhaseId);
        digestMaterial.push_back('|');
        digestMaterial.append(evidence.RuleId);
        digestMaterial.push_back('|');
        digestMaterial.append(evidence.StableFieldKey);
        digestMaterial.push_back('|');
        digestMaterial.append(evidence.DomainPair);
        digestMaterial.push_back('|');
        digestMaterial.append(evidence.LeftFieldId);
        digestMaterial.push_back('|');
        digestMaterial.append(evidence.RightFieldId);
        digestMaterial.push_back('|');
        digestMaterial.append(evidence.MigrationRecommendationPlaceholder);
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputeLedgerDigest(const FieldIssueLedgerReport& report) {
    std::string digestMaterial;
    digestMaterial.reserve((report.Issues.size() * 64u) + 96u);
    digestMaterial.append(report.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(report.Revision);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.RawFindingCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.DeduplicatedIssueCount));
    digestMaterial.push_back('\n');

    for (const FieldAuditIssueRecord& issue : report.Issues) {
        digestMaterial.append(issue.IssueId);
        digestMaterial.push_back('|');
        digestMaterial.append(issue.DeterministicDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(issue.OccurrenceCount));
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(static_cast<uint32_t>(issue.Severity)));
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(issue.BlastRadiusScore));
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ToLowerCopy(const std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char symbol : value) {
        lowered.push_back(static_cast<char>(std::tolower(symbol)));
    }
    return lowered;
}

void ApplySignalFromText(const std::string& normalizedText,
                         uint32_t& runtimeImpact,
                         uint32_t& persistenceImpact,
                         uint32_t& networkImpact,
                         uint32_t& buildImpact,
                         uint32_t& toolingImpact,
                         std::set<std::string>& impactedDomains) {
    auto containsToken = [&normalizedText](const std::string_view token) {
        return normalizedText.find(token) != std::string::npos;
    };

    if (containsToken("runtime") || containsToken("ecs") || containsToken("gameplay")) {
        runtimeImpact = std::max(runtimeImpact, 3u);
        impactedDomains.insert("runtime");
    }
    if (containsToken("serialized") || containsToken("save") || containsToken("scene") || containsToken("prefab") ||
        containsToken("widget") || containsToken("localization")) {
        persistenceImpact = std::max(persistenceImpact, 2u);
        impactedDomains.insert("persistence");
    }
    if (containsToken("network") || containsToken("protocol") || containsToken("rpc") || containsToken("replay") ||
        containsToken("rollback") || containsToken("packets")) {
        networkImpact = std::max(networkImpact, 3u);
        impactedDomains.insert("network");
    }
    if (containsToken("build") || containsToken("bundle") || containsToken("artifact") || containsToken("store") ||
        containsToken("manifest") || containsToken("dedicated-server")) {
        buildImpact = std::max(buildImpact, 2u);
        impactedDomains.insert("build");
    }
    if (containsToken("tool") || containsToken("mcp") || containsToken("authoring") || containsToken("editor") ||
        containsToken("automation")) {
        toolingImpact = std::max(toolingImpact, 1u);
        impactedDomains.insert("tooling");
    }
}

void ComputeSeverityAndBlastRadius(FieldAuditIssueRecord& issue) {
    uint32_t runtimeImpact = 0;
    uint32_t persistenceImpact = 0;
    uint32_t networkImpact = 0;
    uint32_t buildImpact = 0;
    uint32_t toolingImpact = 0;
    std::set<std::string> impactedDomains;

    ApplySignalFromText(ToLowerCopy(issue.RuleId),
                        runtimeImpact,
                        persistenceImpact,
                        networkImpact,
                        buildImpact,
                        toolingImpact,
                        impactedDomains);
    ApplySignalFromText(ToLowerCopy(issue.StableFieldKey),
                        runtimeImpact,
                        persistenceImpact,
                        networkImpact,
                        buildImpact,
                        toolingImpact,
                        impactedDomains);
    ApplySignalFromText(ToLowerCopy(issue.DomainPair),
                        runtimeImpact,
                        persistenceImpact,
                        networkImpact,
                        buildImpact,
                        toolingImpact,
                        impactedDomains);

    for (const FieldIssueEvidenceReference& evidence : issue.EvidenceReferences) {
        ApplySignalFromText(ToLowerCopy(evidence.RunScope),
                            runtimeImpact,
                            persistenceImpact,
                            networkImpact,
                            buildImpact,
                            toolingImpact,
                            impactedDomains);
        ApplySignalFromText(ToLowerCopy(evidence.PhaseId),
                            runtimeImpact,
                            persistenceImpact,
                            networkImpact,
                            buildImpact,
                            toolingImpact,
                            impactedDomains);
        ApplySignalFromText(ToLowerCopy(evidence.DomainPair),
                            runtimeImpact,
                            persistenceImpact,
                            networkImpact,
                            buildImpact,
                            toolingImpact,
                            impactedDomains);
    }

    const uint32_t occurrenceBoost = issue.OccurrenceCount > 1u ? std::min(issue.OccurrenceCount - 1u, 3u) : 0u;
    issue.BlastRadiusScore =
        runtimeImpact + persistenceImpact + networkImpact + buildImpact + toolingImpact + occurrenceBoost;

    if (issue.BlastRadiusScore >= 9u) {
        issue.Severity = FieldIssueSeverityLevel::Critical;
    } else if (issue.BlastRadiusScore >= 7u) {
        issue.Severity = FieldIssueSeverityLevel::High;
    } else if (issue.BlastRadiusScore >= 5u) {
        issue.Severity = FieldIssueSeverityLevel::Medium;
    } else if (issue.BlastRadiusScore >= 2u) {
        issue.Severity = FieldIssueSeverityLevel::Low;
    } else {
        issue.Severity = FieldIssueSeverityLevel::Info;
    }

    issue.ImpactedDomains.assign(impactedDomains.begin(), impactedDomains.end());
    issue.SeverityRationale = "runtime=" + std::to_string(runtimeImpact) + ";persistence=" +
                              std::to_string(persistenceImpact) + ";network=" + std::to_string(networkImpact) +
                              ";build=" + std::to_string(buildImpact) + ";tooling=" +
                              std::to_string(toolingImpact) + ";occurrence_boost=" + std::to_string(occurrenceBoost);
}

} // namespace

Result<FieldIssueLedgerReport> GenerateFieldAuditIssueLedger(const FieldIssueLedgerRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.Revision.empty() || request.AuditRuns.empty()) {
        return Result<FieldIssueLedgerReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "issue-ledger") {
        return Result<FieldIssueLedgerReport>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldIssueLedgerReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::map<std::string, FieldAuditIssueRecord> issueMap;
    uint32_t rawFindingCount = 0;
    for (const FieldAuditRunReport& runReport : request.AuditRuns) {
        for (const FieldAuditPhaseStamp& phaseStamp : runReport.PhaseStamps) {
            for (const FieldValidationFinding& finding : phaseStamp.Findings) {
                ++rawFindingCount;
                const std::string issueKey = finding.RuleId + "|" + finding.StableFieldKey + "|" + finding.DomainPair;
                auto [issueIt, inserted] = issueMap.try_emplace(issueKey);
                FieldAuditIssueRecord& issue = issueIt->second;
                if (inserted) {
                    issue.IssueId = HashToHex(HashString("issue|" + issueKey));
                    issue.RuleId = finding.RuleId;
                    issue.StableFieldKey = finding.StableFieldKey;
                    issue.DomainPair = finding.DomainPair;
                    issue.FirstSeenRevision = request.Revision;
                }

                ++issue.OccurrenceCount;
                FieldIssueEvidenceReference evidence{};
                evidence.RunScope = runReport.Scope;
                evidence.PhaseId = phaseStamp.PhaseId;
                evidence.RuleId = finding.RuleId;
                evidence.StableFieldKey = finding.StableFieldKey;
                evidence.DomainPair = finding.DomainPair;
                evidence.LeftFieldId = finding.LeftEvidence.FieldId;
                evidence.RightFieldId = finding.RightEvidence.FieldId;
                evidence.MigrationRecommendationPlaceholder = finding.MigrationRecommendationPlaceholder;
                issue.EvidenceReferences.push_back(std::move(evidence));
            }
        }
    }

    std::vector<FieldAuditIssueRecord> issues;
    issues.reserve(issueMap.size());
    for (auto& [issueKey, issue] : issueMap) {
        (void)issueKey;
        SortAndDeduplicateEvidenceReferences(issue.EvidenceReferences);
        issue.DeterministicDigest = ComputeIssueDigest(issue);
        issues.push_back(std::move(issue));
    }

    std::sort(issues.begin(), issues.end(), [](const FieldAuditIssueRecord& left, const FieldAuditIssueRecord& right) {
        if (left.IssueId != right.IssueId) {
            return left.IssueId < right.IssueId;
        }
        if (left.RuleId != right.RuleId) {
            return left.RuleId < right.RuleId;
        }
        if (left.StableFieldKey != right.StableFieldKey) {
            return left.StableFieldKey < right.StableFieldKey;
        }
        return left.DomainPair < right.DomainPair;
    });

    FieldIssueLedgerReport report{};
    report.Scope = request.Scope;
    report.OutputDirectory = request.OutputDirectory;
    report.Revision = request.Revision;
    report.Issues = std::move(issues);
    report.Summary.RawFindingCount = rawFindingCount;
    report.Summary.DeduplicatedIssueCount = static_cast<uint32_t>(report.Issues.size());
    report.DeterministicDigest = ComputeLedgerDigest(report);
    return Result<FieldIssueLedgerReport>::Success(std::move(report));
}

Result<FieldIssueLedgerReport> ComputeFieldIssueSeverityAndBlastRadius(const FieldIssueLedgerRequest& request) {
    const Result<FieldIssueLedgerReport> ledgerResult = GenerateFieldAuditIssueLedger(request);
    if (!ledgerResult.Ok) {
        return Result<FieldIssueLedgerReport>::Failure(ledgerResult.Error);
    }

    FieldIssueLedgerReport report = ledgerResult.Value;
    for (FieldAuditIssueRecord& issue : report.Issues) {
        ComputeSeverityAndBlastRadius(issue);
        issue.DeterministicDigest = ComputeIssueDigest(issue);
    }

    std::sort(report.Issues.begin(), report.Issues.end(), [](const FieldAuditIssueRecord& left, const FieldAuditIssueRecord& right) {
        if (left.Severity != right.Severity) {
            return static_cast<uint32_t>(left.Severity) > static_cast<uint32_t>(right.Severity);
        }
        if (left.BlastRadiusScore != right.BlastRadiusScore) {
            return left.BlastRadiusScore > right.BlastRadiusScore;
        }
        return left.IssueId < right.IssueId;
    });

    report.DeterministicDigest = ComputeLedgerDigest(report);
    return Result<FieldIssueLedgerReport>::Success(std::move(report));
}

} // namespace Core::Audit
