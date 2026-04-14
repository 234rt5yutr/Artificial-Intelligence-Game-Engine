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

[[nodiscard]] bool IsValidRegressionEntry(const FieldGuardrailEntry& entry) {
    if (entry.RegressionSuite.SuiteId.empty()) {
        return false;
    }

    const std::vector<std::string> normalizedCoverageMap = NormalizeCoverageMap(entry.RegressionSuite.Stage30CoverageMap);
    return !normalizedCoverageMap.empty();
}

[[nodiscard]] std::string BuildAssertionId(const FieldGuardrailRecord& record) {
    std::string idMaterial;
    idMaterial.reserve(384u);
    idMaterial.append("field-invariant-assertion|");
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
                                         const bool includeRegressionSuite) {
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
    return dedupeKey;
}

[[nodiscard]] Result<FieldGuardrailResult> BuildGuardrailResult(const FieldGuardrailRequest& request,
                                                                const std::string_view expectedScope,
                                                                const bool requireRegressionSuite) {
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
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldGuardrailResult>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<FieldGuardrailRecord> assertionRecords;
    std::set<std::string> seenAssertionKeys;
    for (const FieldGuardrailEntry& entry : request.Entries) {
        const std::vector<std::string> normalizedCoverageMap = NormalizeCoverageMap(entry.RegressionSuite.Stage30CoverageMap);
        const std::string dedupeKey = BuildDedupeKey(entry, normalizedCoverageMap, requireRegressionSuite);
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
        record.Rationale = entry.Rationale.empty()
                               ? (requireRegressionSuite ? "register-field-contract-regression-suite"
                                                         : "register-field-invariant-assertion")
                               : entry.Rationale;
        record.Taxonomy = entry.Taxonomy;
        record.Taxonomy.Lineage.RemediationBatchId = request.RemediationBatchId;
        record.RegressionSuite.SuiteId = entry.RegressionSuite.SuiteId;
        record.RegressionSuite.Stage30CoverageMap = normalizedCoverageMap;
        record.AssertionId = BuildAssertionId(record);
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
    }
    result.Summary.RegressionSuiteCount = static_cast<uint32_t>(uniqueSuites.size());
    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<FieldGuardrailResult>::Success(std::move(result));
}

} // namespace

Result<FieldGuardrailResult> AddFieldInvariantAssertions(const FieldGuardrailRequest& request) {
    return BuildGuardrailResult(request, "field-invariant-assertions", false);
}

Result<FieldGuardrailResult> AddFieldContractRegressionSuites(const FieldGuardrailRequest& request) {
    return BuildGuardrailResult(request, "field-contract-regression-suites", true);
}

} // namespace Core::Remediation
