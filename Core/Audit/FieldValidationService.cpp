#include "Core/Audit/FieldValidationService.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Core::Audit {
namespace {

struct FieldObservation {
    std::string SnapshotScope;
    const FieldInventoryEntry* Entry = nullptr;
};

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

[[nodiscard]] std::string BuildStableMergeKey(const FieldInventoryEntry& entry) {
    return entry.TypeName + "::" + entry.FieldPath;
}

[[nodiscard]] std::string BuildEquivalentFieldKey(const FieldInventoryEntry& entry) {
    return entry.FieldPath;
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

[[nodiscard]] bool ValidateEntry(const FieldInventoryEntry& entry) {
    const std::string canonicalFieldId = entry.Domain + "::" + entry.TypeName + "::" + entry.FieldPath;
    return !entry.Domain.empty() && !entry.OwnerSubsystem.empty() && !entry.TypeName.empty() && !entry.FieldPath.empty() &&
           !entry.FieldId.empty() && entry.FieldId == canonicalFieldId && !entry.SourceTrace.SourceFile.empty() &&
           !entry.SourceTrace.SourceSymbol.empty() && !entry.SourceTrace.CollectorId.empty() &&
           entry.SourceTrace.SourceLine > 0u;
}

[[nodiscard]] bool ValidateSnapshot(const FieldInventorySnapshot& snapshot, const std::string_view expectedScope) {
    if (snapshot.Scope != expectedScope || snapshot.Entries.empty()) {
        return false;
    }

    for (const FieldInventoryEntry& entry : snapshot.Entries) {
        if (!ValidateEntry(entry)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string BuildDomainPair(const FieldObservation& left, const FieldObservation& right) {
    const std::string leftDomain = left.SnapshotScope + ":" + left.Entry->Domain;
    const std::string rightDomain = right.SnapshotScope + ":" + right.Entry->Domain;
    if (leftDomain <= rightDomain) {
        return leftDomain + "<->" + rightDomain;
    }
    return rightDomain + "<->" + leftDomain;
}

[[nodiscard]] FieldValidationEvidence BuildEvidence(const FieldObservation& observation) {
    FieldValidationEvidence evidence{};
    evidence.SnapshotScope = observation.SnapshotScope;
    evidence.Domain = observation.Entry->Domain;
    evidence.FieldId = observation.Entry->FieldId;
    evidence.TypeName = observation.Entry->TypeName;
    evidence.FieldPath = observation.Entry->FieldPath;
    evidence.Required = observation.Entry->Required;
    evidence.HasNumericMinimum = observation.Entry->HasNumericMinimum;
    evidence.NumericMinimumInclusive = observation.Entry->NumericMinimumInclusive;
    evidence.HasNumericMaximum = observation.Entry->HasNumericMaximum;
    evidence.NumericMaximumInclusive = observation.Entry->NumericMaximumInclusive;
    evidence.EnumDomainValues = observation.Entry->EnumDomainValues;
    evidence.StringPattern = observation.Entry->StringPattern;
    evidence.NormalizedIdentifier = observation.Entry->NormalizedIdentifier;
    evidence.SourceTrace = observation.Entry->SourceTrace;
    return evidence;
}

[[nodiscard]] FieldValidationFinding BuildTypeMismatchFinding(const std::string& stableFieldKey,
                                                              const FieldObservation& left,
                                                              const FieldObservation& right) {
    FieldValidationFinding finding{};
    finding.RuleId = "FIELD_AUDIT_RULE_TYPE_CONTRACT_MISMATCH";
    finding.MismatchKind = FieldValidationMismatchKind::TypeMismatch;
    finding.StableFieldKey = stableFieldKey;
    finding.DomainPair = BuildDomainPair(left, right);
    finding.LeftEvidence = BuildEvidence(left);
    finding.RightEvidence = BuildEvidence(right);
    return finding;
}

[[nodiscard]] FieldValidationFinding BuildNullabilityMismatchFinding(const std::string& stableFieldKey,
                                                                      const FieldObservation& left,
                                                                      const FieldObservation& right) {
    FieldValidationFinding finding{};
    finding.RuleId = "FIELD_AUDIT_RULE_NULLABILITY_CONTRACT_MISMATCH";
    finding.MismatchKind = FieldValidationMismatchKind::NullabilityMismatch;
    finding.StableFieldKey = stableFieldKey;
    finding.DomainPair = BuildDomainPair(left, right);
    finding.LeftEvidence = BuildEvidence(left);
    finding.RightEvidence = BuildEvidence(right);
    return finding;
}

[[nodiscard]] FieldValidationFinding BuildDomainMismatchFinding(const std::string& ruleId,
                                                                const FieldValidationMismatchKind mismatchKind,
                                                                const std::string& stableFieldKey,
                                                                const FieldObservation& left,
                                                                const FieldObservation& right) {
    FieldValidationFinding finding{};
    finding.RuleId = ruleId;
    finding.MismatchKind = mismatchKind;
    finding.StableFieldKey = stableFieldKey;
    finding.DomainPair = BuildDomainPair(left, right);
    finding.LeftEvidence = BuildEvidence(left);
    finding.RightEvidence = BuildEvidence(right);
    return finding;
}

void SortFindings(std::vector<FieldValidationFinding>& findings) {
    std::sort(findings.begin(), findings.end(), [](const FieldValidationFinding& left, const FieldValidationFinding& right) {
        if (left.StableFieldKey != right.StableFieldKey) {
            return left.StableFieldKey < right.StableFieldKey;
        }
        if (left.RuleId != right.RuleId) {
            return left.RuleId < right.RuleId;
        }
        if (left.DomainPair != right.DomainPair) {
            return left.DomainPair < right.DomainPair;
        }
        if (left.LeftEvidence.FieldId != right.LeftEvidence.FieldId) {
            return left.LeftEvidence.FieldId < right.LeftEvidence.FieldId;
        }
        return left.RightEvidence.FieldId < right.RightEvidence.FieldId;
    });
}

[[nodiscard]] std::string ComputeValidationDigest(const FieldValidationReport& report) {
    std::string digestMaterial;
    digestMaterial.reserve((report.Findings.size() * 160u) + 128u);
    digestMaterial.append(report.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.ComparedFieldCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.TypeMismatchCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.NullabilityMismatchCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.RangeDomainMismatchCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.EnumDomainMismatchCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.PatternDomainMismatchCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.IdentifierNormalizationMismatchCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.ConditionalRequiredInvariantMismatchCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.DependencyOrderingInvariantMismatchCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.RelatedFieldConsistencyInvariantMismatchCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(report.Summary.TotalFindingCount));
    digestMaterial.push_back('\n');

    for (const FieldValidationFinding& finding : report.Findings) {
        digestMaterial.append(finding.RuleId);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.StableFieldKey);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.DomainPair);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.LeftEvidence.FieldId);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.LeftEvidence.TypeName);
        digestMaterial.push_back('|');
        digestMaterial.push_back(finding.LeftEvidence.Required ? '1' : '0');
        digestMaterial.push_back('|');
        digestMaterial.push_back(finding.LeftEvidence.HasNumericMinimum ? '1' : '0');
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(finding.LeftEvidence.NumericMinimumInclusive));
        digestMaterial.push_back('|');
        digestMaterial.push_back(finding.LeftEvidence.HasNumericMaximum ? '1' : '0');
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(finding.LeftEvidence.NumericMaximumInclusive));
        digestMaterial.push_back('|');
        for (std::size_t index = 0; index < finding.LeftEvidence.EnumDomainValues.size(); ++index) {
            if (index > 0u) {
                digestMaterial.push_back(',');
            }
            digestMaterial.append(finding.LeftEvidence.EnumDomainValues[index]);
        }
        digestMaterial.push_back('|');
        digestMaterial.append(finding.LeftEvidence.StringPattern);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.LeftEvidence.NormalizedIdentifier);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.RightEvidence.FieldId);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.RightEvidence.TypeName);
        digestMaterial.push_back('|');
        digestMaterial.push_back(finding.RightEvidence.Required ? '1' : '0');
        digestMaterial.push_back('|');
        digestMaterial.push_back(finding.RightEvidence.HasNumericMinimum ? '1' : '0');
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(finding.RightEvidence.NumericMinimumInclusive));
        digestMaterial.push_back('|');
        digestMaterial.push_back(finding.RightEvidence.HasNumericMaximum ? '1' : '0');
        digestMaterial.push_back('|');
        digestMaterial.append(std::to_string(finding.RightEvidence.NumericMaximumInclusive));
        digestMaterial.push_back('|');
        for (std::size_t index = 0; index < finding.RightEvidence.EnumDomainValues.size(); ++index) {
            if (index > 0u) {
                digestMaterial.push_back(',');
            }
            digestMaterial.append(finding.RightEvidence.EnumDomainValues[index]);
        }
        digestMaterial.push_back('|');
        digestMaterial.append(finding.RightEvidence.StringPattern);
        digestMaterial.push_back('|');
        digestMaterial.append(finding.RightEvidence.NormalizedIdentifier);
        digestMaterial.push_back('\n');
    }

    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string BuildNumericConstraintSignature(const FieldInventoryEntry& entry) {
    std::string signature;
    signature.reserve(64u);
    signature.push_back(entry.HasNumericMinimum ? '1' : '0');
    signature.push_back('|');
    signature.append(std::to_string(entry.NumericMinimumInclusive));
    signature.push_back('|');
    signature.push_back(entry.HasNumericMaximum ? '1' : '0');
    signature.push_back('|');
    signature.append(std::to_string(entry.NumericMaximumInclusive));
    return signature;
}

[[nodiscard]] std::string BuildEnumConstraintSignature(const FieldInventoryEntry& entry) {
    std::vector<std::string> values = entry.EnumDomainValues;
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());

    std::string signature;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0u) {
            signature.push_back(',');
        }
        signature.append(values[index]);
    }
    return signature;
}

[[nodiscard]] std::string NormalizeIdentifier(const std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    bool previousWasSeparator = false;
    for (const unsigned char symbol : value) {
        if (std::isalnum(symbol) != 0) {
            normalized.push_back(static_cast<char>(std::tolower(symbol)));
            previousWasSeparator = false;
            continue;
        }

        if (symbol == '.' || symbol == '_' || symbol == '-') {
            if (!normalized.empty() && !previousWasSeparator) {
                normalized.push_back('.');
                previousWasSeparator = true;
            }
        }
    }

    while (!normalized.empty() && normalized.back() == '.') {
        normalized.pop_back();
    }
    return normalized;
}

[[nodiscard]] bool ContainsDuplicateOrEmptyEnumValues(const FieldInventoryEntry& entry) {
    if (entry.EnumDomainValues.empty()) {
        return false;
    }

    std::set<std::string> uniqueValues;
    for (const std::string& value : entry.EnumDomainValues) {
        if (value.empty() || !uniqueValues.insert(value).second) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::vector<FieldObservation> BuildObservations(const FieldValidationRequest& request) {
    std::vector<FieldObservation> observations;
    observations.reserve(request.RuntimeSnapshot.Entries.size() + request.SerializedSnapshot.Entries.size() +
                         request.ProtocolSnapshot.Entries.size());

    const std::array<std::pair<std::string_view, const FieldInventorySnapshot*>, 3> snapshots = {
        std::pair<std::string_view, const FieldInventorySnapshot*>("runtime", &request.RuntimeSnapshot),
        std::pair<std::string_view, const FieldInventorySnapshot*>("serialized", &request.SerializedSnapshot),
        std::pair<std::string_view, const FieldInventorySnapshot*>("protocol", &request.ProtocolSnapshot)};

    for (const auto& [scope, snapshot] : snapshots) {
        for (const FieldInventoryEntry& entry : snapshot->Entries) {
            FieldObservation observation{};
            observation.SnapshotScope = std::string(scope);
            observation.Entry = &entry;
            observations.push_back(observation);
        }
    }

    std::sort(observations.begin(),
              observations.end(),
              [](const FieldObservation& left, const FieldObservation& right) {
                  const std::string leftEquivalent = BuildEquivalentFieldKey(*left.Entry);
                  const std::string rightEquivalent = BuildEquivalentFieldKey(*right.Entry);
                  if (leftEquivalent != rightEquivalent) {
                      return leftEquivalent < rightEquivalent;
                  }
                  if (left.Entry->TypeName != right.Entry->TypeName) {
                      return left.Entry->TypeName < right.Entry->TypeName;
                  }
                  if (left.SnapshotScope != right.SnapshotScope) {
                      return left.SnapshotScope < right.SnapshotScope;
                  }
                  if (left.Entry->Domain != right.Entry->Domain) {
                      return left.Entry->Domain < right.Entry->Domain;
                  }
                  return left.Entry->FieldId < right.Entry->FieldId;
              });

    return observations;
}

[[nodiscard]] std::optional<uint32_t> GetScopeRank(const std::string_view scope) {
    if (scope == "runtime") {
        return 0u;
    }
    if (scope == "serialized") {
        return 1u;
    }
    if (scope == "protocol") {
        return 2u;
    }
    return std::nullopt;
}

[[nodiscard]] bool IsOrderedVersionLineage(const std::vector<std::string>& versionLineage) {
    if (versionLineage.empty()) {
        return true;
    }

    std::optional<uint32_t> previousRank;
    for (const std::string& scope : versionLineage) {
        const std::optional<uint32_t> currentRank = GetScopeRank(scope);
        if (!currentRank.has_value()) {
            return false;
        }
        if (previousRank.has_value() && *currentRank < *previousRank) {
            return false;
        }
        previousRank = currentRank;
    }
    return true;
}

[[nodiscard]] std::vector<std::string> CollectDependencyFieldIds(const FieldInventoryEntry& entry) {
    std::vector<std::string> dependencyFieldIds;
    dependencyFieldIds.reserve(entry.AliasFieldIds.size());
    for (const std::string& aliasFieldId : entry.AliasFieldIds) {
        if (!aliasFieldId.empty() && aliasFieldId != entry.FieldId) {
            dependencyFieldIds.push_back(aliasFieldId);
        }
    }
    std::sort(dependencyFieldIds.begin(), dependencyFieldIds.end());
    dependencyFieldIds.erase(std::unique(dependencyFieldIds.begin(), dependencyFieldIds.end()), dependencyFieldIds.end());
    return dependencyFieldIds;
}

} // namespace

Result<FieldValidationReport> ValidateFieldTypeAndNullabilityContracts(const FieldValidationRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty()) {
        return Result<FieldValidationReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "type-nullability") {
        return Result<FieldValidationReport>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    if (!ValidateSnapshot(request.RuntimeSnapshot, "runtime") || !ValidateSnapshot(request.SerializedSnapshot, "serialized") ||
        !ValidateSnapshot(request.ProtocolSnapshot, "protocol")) {
        return Result<FieldValidationReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldValidationReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    const std::vector<FieldObservation> observations = BuildObservations(request);
    std::map<std::string, std::vector<std::size_t>> equivalentKeyToIndices;
    for (std::size_t index = 0; index < observations.size(); ++index) {
        equivalentKeyToIndices[BuildEquivalentFieldKey(*observations[index].Entry)].push_back(index);
    }

    std::vector<FieldValidationFinding> findings;
    std::set<std::string> comparedFields;
    for (const auto& [equivalentFieldKey, indices] : equivalentKeyToIndices) {
        if (indices.size() < 2u) {
            comparedFields.insert(equivalentFieldKey);
            continue;
        }

        comparedFields.insert(equivalentFieldKey);
        for (std::size_t leftIndex = 0; leftIndex < indices.size(); ++leftIndex) {
            for (std::size_t rightIndex = leftIndex + 1u; rightIndex < indices.size(); ++rightIndex) {
                const FieldObservation& leftObservation = observations[indices[leftIndex]];
                const FieldObservation& rightObservation = observations[indices[rightIndex]];
                if (leftObservation.SnapshotScope == rightObservation.SnapshotScope) {
                    continue;
                }

                if (leftObservation.Entry->TypeName != rightObservation.Entry->TypeName) {
                    findings.push_back(BuildTypeMismatchFinding("field-path::" + equivalentFieldKey,
                                                                leftObservation,
                                                                rightObservation));
                    continue;
                }

                if (leftObservation.Entry->Required != rightObservation.Entry->Required) {
                    findings.push_back(BuildNullabilityMismatchFinding(BuildStableMergeKey(*leftObservation.Entry),
                                                                       leftObservation,
                                                                       rightObservation));
                }
            }
        }
    }

    SortFindings(findings);

    FieldValidationReport report{};
    report.Scope = request.Scope;
    report.OutputDirectory = request.OutputDirectory;
    report.Findings = std::move(findings);
    report.Summary.ComparedFieldCount = static_cast<uint32_t>(comparedFields.size());
    for (const FieldValidationFinding& finding : report.Findings) {
        if (finding.MismatchKind == FieldValidationMismatchKind::TypeMismatch) {
            ++report.Summary.TypeMismatchCount;
        } else if (finding.MismatchKind == FieldValidationMismatchKind::NullabilityMismatch) {
            ++report.Summary.NullabilityMismatchCount;
        }
    }
    report.Summary.TotalFindingCount = static_cast<uint32_t>(report.Findings.size());
    report.DeterministicDigest = ComputeValidationDigest(report);

    return Result<FieldValidationReport>::Success(std::move(report));
}

Result<FieldValidationReport> ValidateFieldRangeEnumAndPatternDomains(const FieldValidationRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty()) {
        return Result<FieldValidationReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "range-enum-pattern-domains") {
        return Result<FieldValidationReport>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    if (!ValidateSnapshot(request.RuntimeSnapshot, "runtime") || !ValidateSnapshot(request.SerializedSnapshot, "serialized") ||
        !ValidateSnapshot(request.ProtocolSnapshot, "protocol")) {
        return Result<FieldValidationReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldValidationReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    const std::vector<FieldObservation> observations = BuildObservations(request);
    std::map<std::string, std::vector<std::size_t>> equivalentKeyToIndices;
    for (std::size_t index = 0; index < observations.size(); ++index) {
        equivalentKeyToIndices[BuildEquivalentFieldKey(*observations[index].Entry)].push_back(index);
    }

    std::vector<FieldValidationFinding> findings;
    std::set<std::string> comparedFields;
    for (const auto& [equivalentFieldKey, indices] : equivalentKeyToIndices) {
        comparedFields.insert(equivalentFieldKey);
        if (indices.size() < 2u) {
            continue;
        }

        const auto baselineIt = std::find_if(indices.begin(), indices.end(), [&observations](const std::size_t index) {
            return observations[index].SnapshotScope == "serialized";
        });
        if (baselineIt == indices.end()) {
            continue;
        }

        const FieldObservation& baseline = observations[*baselineIt];
        for (const std::size_t index : indices) {
            if (index == *baselineIt) {
                continue;
            }

            const FieldObservation& observation = observations[index];
            const bool observationHasNumericConstraint = observation.Entry->HasNumericMinimum || observation.Entry->HasNumericMaximum;
            const bool baselineHasNumericConstraint = baseline.Entry->HasNumericMinimum || baseline.Entry->HasNumericMaximum;
            if ((observationHasNumericConstraint || baselineHasNumericConstraint) &&
                BuildNumericConstraintSignature(*observation.Entry) != BuildNumericConstraintSignature(*baseline.Entry)) {
                findings.push_back(BuildDomainMismatchFinding("FIELD_AUDIT_RULE_RANGE_DOMAIN_MISMATCH",
                                                              FieldValidationMismatchKind::RangeDomainMismatch,
                                                              BuildStableMergeKey(*observation.Entry),
                                                              observation,
                                                              baseline));
            }

            const bool observationHasEnumConstraint = !observation.Entry->EnumDomainValues.empty();
            const bool baselineHasEnumConstraint = !baseline.Entry->EnumDomainValues.empty();
            if ((observationHasEnumConstraint || baselineHasEnumConstraint) &&
                BuildEnumConstraintSignature(*observation.Entry) != BuildEnumConstraintSignature(*baseline.Entry)) {
                findings.push_back(BuildDomainMismatchFinding("FIELD_AUDIT_RULE_ENUM_DOMAIN_MISMATCH",
                                                              FieldValidationMismatchKind::EnumDomainMismatch,
                                                              BuildStableMergeKey(*observation.Entry),
                                                              observation,
                                                              baseline));
            }

            const bool observationHasPatternConstraint = !observation.Entry->StringPattern.empty();
            const bool baselineHasPatternConstraint = !baseline.Entry->StringPattern.empty();
            if ((observationHasPatternConstraint || baselineHasPatternConstraint) &&
                observation.Entry->StringPattern != baseline.Entry->StringPattern) {
                findings.push_back(BuildDomainMismatchFinding("FIELD_AUDIT_RULE_PATTERN_DOMAIN_MISMATCH",
                                                              FieldValidationMismatchKind::PatternDomainMismatch,
                                                              BuildStableMergeKey(*observation.Entry),
                                                              observation,
                                                              baseline));
            }

            const std::string observationNormalizedIdentifier =
                observation.Entry->NormalizedIdentifier.empty() ? NormalizeIdentifier(observation.Entry->FieldPath)
                                                                : observation.Entry->NormalizedIdentifier;
            const std::string baselineNormalizedIdentifier =
                baseline.Entry->NormalizedIdentifier.empty() ? NormalizeIdentifier(baseline.Entry->FieldPath)
                                                             : baseline.Entry->NormalizedIdentifier;
            if (observationNormalizedIdentifier != baselineNormalizedIdentifier) {
                findings.push_back(BuildDomainMismatchFinding("FIELD_AUDIT_RULE_IDENTIFIER_NORMALIZATION_MISMATCH",
                                                              FieldValidationMismatchKind::IdentifierNormalizationMismatch,
                                                              BuildStableMergeKey(*observation.Entry),
                                                              observation,
                                                              baseline));
            }

            if (ContainsDuplicateOrEmptyEnumValues(*observation.Entry)) {
                findings.push_back(BuildDomainMismatchFinding("FIELD_AUDIT_RULE_ENUM_DOMAIN_MISMATCH",
                                                              FieldValidationMismatchKind::EnumDomainMismatch,
                                                              BuildStableMergeKey(*observation.Entry),
                                                              observation,
                                                              observation));
            }
        }
    }

    SortFindings(findings);
    findings.erase(std::unique(findings.begin(),
                               findings.end(),
                               [](const FieldValidationFinding& left, const FieldValidationFinding& right) {
                                   return left.RuleId == right.RuleId && left.StableFieldKey == right.StableFieldKey &&
                                          left.DomainPair == right.DomainPair &&
                                          left.LeftEvidence.FieldId == right.LeftEvidence.FieldId &&
                                          left.RightEvidence.FieldId == right.RightEvidence.FieldId;
                               }),
                   findings.end());

    FieldValidationReport report{};
    report.Scope = request.Scope;
    report.OutputDirectory = request.OutputDirectory;
    report.Findings = std::move(findings);
    report.Summary.ComparedFieldCount = static_cast<uint32_t>(comparedFields.size());
    for (const FieldValidationFinding& finding : report.Findings) {
        if (finding.MismatchKind == FieldValidationMismatchKind::RangeDomainMismatch) {
            ++report.Summary.RangeDomainMismatchCount;
        } else if (finding.MismatchKind == FieldValidationMismatchKind::EnumDomainMismatch) {
            ++report.Summary.EnumDomainMismatchCount;
        } else if (finding.MismatchKind == FieldValidationMismatchKind::PatternDomainMismatch) {
            ++report.Summary.PatternDomainMismatchCount;
        } else if (finding.MismatchKind == FieldValidationMismatchKind::IdentifierNormalizationMismatch) {
            ++report.Summary.IdentifierNormalizationMismatchCount;
        }
    }
    report.Summary.TotalFindingCount = static_cast<uint32_t>(report.Findings.size());
    report.DeterministicDigest = ComputeValidationDigest(report);

    return Result<FieldValidationReport>::Success(std::move(report));
}

Result<FieldValidationReport> ValidateCrossFieldInvariantRules(const FieldValidationRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty()) {
        return Result<FieldValidationReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "cross-field-invariant-rules") {
        return Result<FieldValidationReport>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    if (!ValidateSnapshot(request.RuntimeSnapshot, "runtime") || !ValidateSnapshot(request.SerializedSnapshot, "serialized") ||
        !ValidateSnapshot(request.ProtocolSnapshot, "protocol")) {
        return Result<FieldValidationReport>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldValidationReport>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    const std::vector<FieldObservation> observations = BuildObservations(request);
    std::map<std::string, std::size_t> fieldIdToObservationIndex;
    std::map<std::string, std::vector<std::size_t>> equivalentKeyToIndices;
    for (std::size_t index = 0; index < observations.size(); ++index) {
        fieldIdToObservationIndex.emplace(observations[index].Entry->FieldId, index);
        equivalentKeyToIndices[BuildEquivalentFieldKey(*observations[index].Entry)].push_back(index);
    }

    std::vector<FieldValidationFinding> findings;
    std::set<std::string> comparedFields;

    for (const FieldObservation& observation : observations) {
        const FieldInventoryEntry& entry = *observation.Entry;
        comparedFields.insert(BuildEquivalentFieldKey(entry));
        if (!entry.Required) {
            continue;
        }

        const std::vector<std::string> dependencyFieldIds = CollectDependencyFieldIds(entry);
        for (const std::string& dependencyFieldId : dependencyFieldIds) {
            const std::string stableDependencyKey = "dependency::" + entry.FieldId + "->" + dependencyFieldId;
            const auto dependencyIt = fieldIdToObservationIndex.find(dependencyFieldId);
            if (dependencyIt == fieldIdToObservationIndex.end()) {
                findings.push_back(BuildDomainMismatchFinding(
                    "FIELD_AUDIT_RULE_CONDITIONAL_REQUIRED_INVARIANT_MISMATCH",
                    FieldValidationMismatchKind::ConditionalRequiredInvariantMismatch,
                    stableDependencyKey,
                    observation,
                    observation));
                continue;
            }

            const FieldObservation& dependencyObservation = observations[dependencyIt->second];
            if (!dependencyObservation.Entry->Required) {
                findings.push_back(BuildDomainMismatchFinding(
                    "FIELD_AUDIT_RULE_CONDITIONAL_REQUIRED_INVARIANT_MISMATCH",
                    FieldValidationMismatchKind::ConditionalRequiredInvariantMismatch,
                    stableDependencyKey,
                    observation,
                    dependencyObservation));
            }
        }

        if (!IsOrderedVersionLineage(entry.VersionLineage)) {
            findings.push_back(BuildDomainMismatchFinding("FIELD_AUDIT_RULE_DEPENDENCY_ORDERING_INVARIANT_MISMATCH",
                                                          FieldValidationMismatchKind::DependencyOrderingInvariantMismatch,
                                                          "lineage::" + entry.FieldId,
                                                          observation,
                                                          observation));
        }
    }

    for (const auto& [equivalentFieldKey, indices] : equivalentKeyToIndices) {
        comparedFields.insert(equivalentFieldKey);
        if (indices.size() < 2u) {
            continue;
        }

        auto baselineIt = std::find_if(indices.begin(), indices.end(), [&observations](const std::size_t index) {
            return observations[index].SnapshotScope == "serialized";
        });
        if (baselineIt == indices.end()) {
            baselineIt = indices.begin();
        }

        const FieldObservation& baselineObservation = observations[*baselineIt];
        for (const std::size_t index : indices) {
            if (index == *baselineIt) {
                continue;
            }

            const FieldObservation& observation = observations[index];
            if (observation.Entry->OwnerSubsystem != baselineObservation.Entry->OwnerSubsystem) {
                findings.push_back(BuildDomainMismatchFinding(
                    "FIELD_AUDIT_RULE_RELATED_FIELD_CONSISTENCY_INVARIANT_MISMATCH",
                    FieldValidationMismatchKind::RelatedFieldConsistencyInvariantMismatch,
                    BuildStableMergeKey(*observation.Entry),
                    observation,
                    baselineObservation));
            }
        }
    }

    SortFindings(findings);
    findings.erase(std::unique(findings.begin(),
                               findings.end(),
                               [](const FieldValidationFinding& left, const FieldValidationFinding& right) {
                                   return left.RuleId == right.RuleId && left.StableFieldKey == right.StableFieldKey &&
                                          left.DomainPair == right.DomainPair &&
                                          left.LeftEvidence.FieldId == right.LeftEvidence.FieldId &&
                                          left.RightEvidence.FieldId == right.RightEvidence.FieldId;
                               }),
                   findings.end());

    FieldValidationReport report{};
    report.Scope = request.Scope;
    report.OutputDirectory = request.OutputDirectory;
    report.Findings = std::move(findings);
    report.Summary.ComparedFieldCount = static_cast<uint32_t>(comparedFields.size());
    for (const FieldValidationFinding& finding : report.Findings) {
        if (finding.MismatchKind == FieldValidationMismatchKind::ConditionalRequiredInvariantMismatch) {
            ++report.Summary.ConditionalRequiredInvariantMismatchCount;
        } else if (finding.MismatchKind == FieldValidationMismatchKind::DependencyOrderingInvariantMismatch) {
            ++report.Summary.DependencyOrderingInvariantMismatchCount;
        } else if (finding.MismatchKind == FieldValidationMismatchKind::RelatedFieldConsistencyInvariantMismatch) {
            ++report.Summary.RelatedFieldConsistencyInvariantMismatchCount;
        }
    }
    report.Summary.TotalFindingCount = static_cast<uint32_t>(report.Findings.size());
    report.DeterministicDigest = ComputeValidationDigest(report);

    return Result<FieldValidationReport>::Success(std::move(report));
}

} // namespace Core::Audit
