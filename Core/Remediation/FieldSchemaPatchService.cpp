#include "Core/Remediation/FieldSchemaPatchService.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
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

[[nodiscard]] std::optional<uint32_t> GetScopeRank(const std::string_view scope) {
    if (scope == "runtime") {
        return 0u;
    }
    if (scope == "editor") {
        return 1u;
    }
    if (scope == "cooked") {
        return 2u;
    }
    if (scope == "serialized") {
        return 3u;
    }
    if (scope == "protocol") {
        return 4u;
    }
    return std::nullopt;
}

[[nodiscard]] const Core::Audit::FieldValidationEvidence& SelectCanonicalEvidence(
    const Core::Audit::FieldValidationEvidence& left,
    const Core::Audit::FieldValidationEvidence& right) {
    const std::optional<uint32_t> leftRank = GetScopeRank(left.SnapshotScope);
    const std::optional<uint32_t> rightRank = GetScopeRank(right.SnapshotScope);
    if (leftRank.has_value() && rightRank.has_value() && *leftRank != *rightRank) {
        return *leftRank < *rightRank ? left : right;
    }
    if (leftRank.has_value() != rightRank.has_value()) {
        return leftRank.has_value() ? left : right;
    }
    if (left.FieldId != right.FieldId) {
        return left.FieldId < right.FieldId ? left : right;
    }
    if (left.TypeName != right.TypeName) {
        return left.TypeName < right.TypeName ? left : right;
    }
    return left.SnapshotScope <= right.SnapshotScope ? left : right;
}

[[nodiscard]] std::string BoolToString(const bool value) {
    return value ? "true" : "false";
}

[[nodiscard]] std::string BuildPatchId(const FieldSchemaPatchRecord& record) {
    std::string idMaterial;
    idMaterial.reserve(192u);
    idMaterial.append("patch|");
    idMaterial.append(record.Provenance.FindingId);
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.PatchKind)));
    idMaterial.push_back('|');
    idMaterial.append(record.StableFieldKey);
    idMaterial.push_back('|');
    idMaterial.append(record.TargetFieldId);
    idMaterial.push_back('|');
    idMaterial.append(record.PropertyPath);
    idMaterial.push_back('|');
    idMaterial.append(record.ReplacementValue);
    return HashToHex(HashString(idMaterial));
}

[[nodiscard]] std::string ComputePatchRecordDigest(const FieldSchemaPatchRecord& record) {
    std::string digestMaterial;
    digestMaterial.reserve(320u);
    digestMaterial.append(record.PatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.PatchKind)));
    digestMaterial.push_back('|');
    digestMaterial.append(record.StableFieldKey);
    digestMaterial.push_back('|');
    digestMaterial.append(record.DomainPair);
    digestMaterial.push_back('|');
    digestMaterial.append(record.TargetFieldId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.PropertyPath);
    digestMaterial.push_back('|');
    digestMaterial.append(record.ExistingValue);
    digestMaterial.push_back('|');
    digestMaterial.append(record.ReplacementValue);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.FindingId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.RuleId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.Owner);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.RemediationBatchId);
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputePatchResultDigest(const FieldSchemaPatchResult& result) {
    std::string digestMaterial;
    digestMaterial.reserve((result.PatchRecords.size() * 64u) + 128u);
    digestMaterial.append(result.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RemediationBatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.TypePatchCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.NullabilityPatchCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.RequiredPatchCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.TotalPatchCount));
    digestMaterial.push_back('\n');

    for (const FieldSchemaPatchRecord& record : result.PatchRecords) {
        digestMaterial.append(record.PatchId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.DeterministicDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(record.TargetFieldId);
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] bool IsValidSourceFinding(const FieldSchemaPatchSourceFinding& sourceFinding) {
    if (sourceFinding.FindingId.empty() || sourceFinding.Owner.empty()) {
        return false;
    }

    const Core::Audit::FieldValidationFinding& finding = sourceFinding.Finding;
    return !finding.RuleId.empty() && !finding.StableFieldKey.empty() && !finding.DomainPair.empty() &&
           !finding.LeftEvidence.FieldId.empty() && !finding.RightEvidence.FieldId.empty();
}

[[nodiscard]] bool IsValidDefaultFallbackFinding(const FieldDefaultFallbackPolicyFinding& finding) {
    if (finding.FindingId.empty() || finding.Owner.empty() || finding.RuleId.empty() || finding.StableFieldKey.empty() ||
        finding.DomainPair.empty()) {
        return false;
    }

    if (finding.LeftEvidence.SnapshotScope.empty() || finding.RightEvidence.SnapshotScope.empty() ||
        finding.LeftEvidence.FieldId.empty() || finding.RightEvidence.FieldId.empty()) {
        return false;
    }

    if (!finding.LeftEvidence.HasDefaultValue && !finding.LeftEvidence.DefaultValue.empty()) {
        return false;
    }
    if (!finding.RightEvidence.HasDefaultValue && !finding.RightEvidence.DefaultValue.empty()) {
        return false;
    }
    if (!finding.LeftEvidence.HasFallbackPath && !finding.LeftEvidence.FallbackPath.empty()) {
        return false;
    }
    if (!finding.RightEvidence.HasFallbackPath && !finding.RightEvidence.FallbackPath.empty()) {
        return false;
    }
    return true;
}

[[nodiscard]] const FieldDefaultFallbackPolicyEvidence& SelectCanonicalDefaultFallbackEvidence(
    const FieldDefaultFallbackPolicyEvidence& left,
    const FieldDefaultFallbackPolicyEvidence& right) {
    const std::optional<uint32_t> leftRank = GetScopeRank(left.SnapshotScope);
    const std::optional<uint32_t> rightRank = GetScopeRank(right.SnapshotScope);
    if (leftRank.has_value() && rightRank.has_value() && *leftRank != *rightRank) {
        return *leftRank < *rightRank ? left : right;
    }
    if (leftRank.has_value() != rightRank.has_value()) {
        return leftRank.has_value() ? left : right;
    }
    if (left.FieldId != right.FieldId) {
        return left.FieldId < right.FieldId ? left : right;
    }
    if (left.HasDefaultValue != right.HasDefaultValue) {
        return left.HasDefaultValue ? left : right;
    }
    if (left.DefaultValue != right.DefaultValue) {
        return left.DefaultValue < right.DefaultValue ? left : right;
    }
    if (left.HasFallbackPath != right.HasFallbackPath) {
        return left.HasFallbackPath ? left : right;
    }
    if (left.FallbackPath != right.FallbackPath) {
        return left.FallbackPath < right.FallbackPath ? left : right;
    }
    return left.SnapshotScope <= right.SnapshotScope ? left : right;
}

[[nodiscard]] std::string OptionalValueToString(const bool hasValue, const std::string& value) {
    return hasValue ? value : "<unset>";
}

[[nodiscard]] std::vector<std::string> NormalizeAliasNames(const std::vector<std::string>& aliasNames) {
    std::vector<std::string> normalized = aliasNames;
    normalized.erase(std::remove_if(normalized.begin(),
                                    normalized.end(),
                                    [](const std::string& alias) { return alias.empty(); }),
                     normalized.end());
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}

[[nodiscard]] std::string JoinAliasNames(const std::vector<std::string>& aliasNames) {
    if (aliasNames.empty()) {
        return "<none>";
    }

    std::string joined;
    for (std::size_t index = 0; index < aliasNames.size(); ++index) {
        if (index > 0u) {
            joined.push_back(',');
        }
        joined.append(aliasNames[index]);
    }
    return joined;
}

[[nodiscard]] bool IsValidSerializationMappingEvidence(const FieldSerializationMappingEvidence& evidence) {
    if (evidence.SnapshotScope.empty() || evidence.FieldId.empty() || evidence.SerializedName.empty() ||
        evidence.SerializedPath.empty()) {
        return false;
    }
    for (const std::string& aliasName : evidence.AliasNames) {
        if (aliasName.empty()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsValidSerializationMappingFinding(const FieldSerializationMappingFinding& finding) {
    if (finding.FindingId.empty() || finding.Owner.empty() || finding.RuleId.empty() || finding.StableFieldKey.empty() ||
        finding.DomainPair.empty()) {
        return false;
    }

    if (!IsValidSerializationMappingEvidence(finding.RuntimeEvidence) ||
        !IsValidSerializationMappingEvidence(finding.EditorEvidence) ||
        !IsValidSerializationMappingEvidence(finding.CookedEvidence)) {
        return false;
    }

    if (finding.RuntimeEvidence.SnapshotScope != "runtime" || finding.EditorEvidence.SnapshotScope != "editor" ||
        finding.CookedEvidence.SnapshotScope != "cooked") {
        return false;
    }
    return true;
}

[[nodiscard]] const FieldSerializationMappingEvidence& SelectCanonicalSerializationMappingEvidence(
    const std::vector<const FieldSerializationMappingEvidence*>& evidences) {
    assert(!evidences.empty());

    const FieldSerializationMappingEvidence* canonical = evidences.front();
    for (std::size_t index = 1u; index < evidences.size(); ++index) {
        const FieldSerializationMappingEvidence* candidate = evidences[index];
        const std::optional<uint32_t> canonicalRank = GetScopeRank(canonical->SnapshotScope);
        const std::optional<uint32_t> candidateRank = GetScopeRank(candidate->SnapshotScope);
        if (canonicalRank.has_value() && candidateRank.has_value() && *canonicalRank != *candidateRank) {
            canonical = *candidateRank < *canonicalRank ? candidate : canonical;
            continue;
        }
        if (canonicalRank.has_value() != candidateRank.has_value()) {
            canonical = candidateRank.has_value() ? candidate : canonical;
            continue;
        }
        if (canonical->FieldId != candidate->FieldId) {
            canonical = candidate->FieldId < canonical->FieldId ? candidate : canonical;
            continue;
        }
        if (canonical->SerializedName != candidate->SerializedName) {
            canonical = candidate->SerializedName < canonical->SerializedName ? candidate : canonical;
            continue;
        }
        if (canonical->SerializedPath != candidate->SerializedPath) {
            canonical = candidate->SerializedPath < canonical->SerializedPath ? candidate : canonical;
            continue;
        }
        const std::string canonicalAliases = JoinAliasNames(NormalizeAliasNames(canonical->AliasNames));
        const std::string candidateAliases = JoinAliasNames(NormalizeAliasNames(candidate->AliasNames));
        if (canonicalAliases != candidateAliases) {
            canonical = candidateAliases < canonicalAliases ? candidate : canonical;
            continue;
        }
        canonical = candidate->SnapshotScope < canonical->SnapshotScope ? candidate : canonical;
    }
    return *canonical;
}

[[nodiscard]] std::string BuildSerializationMappingFixId(const FieldSerializationMappingFixRecord& record) {
    std::string idMaterial;
    idMaterial.reserve(224u);
    idMaterial.append("serialization-fix|");
    idMaterial.append(record.Provenance.FindingId);
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.FixKind)));
    idMaterial.push_back('|');
    idMaterial.append(record.StableFieldKey);
    idMaterial.push_back('|');
    idMaterial.append(record.TargetFieldId);
    idMaterial.push_back('|');
    idMaterial.append(record.PropertyPath);
    idMaterial.push_back('|');
    idMaterial.append(record.ReplacementValue);
    return HashToHex(HashString(idMaterial));
}

[[nodiscard]] std::string ComputeSerializationMappingFixRecordDigest(const FieldSerializationMappingFixRecord& record) {
    std::string digestMaterial;
    digestMaterial.reserve(448u);
    digestMaterial.append(record.FixId);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.FixKind)));
    digestMaterial.push_back('|');
    digestMaterial.append(record.StableFieldKey);
    digestMaterial.push_back('|');
    digestMaterial.append(record.DomainPair);
    digestMaterial.push_back('|');
    digestMaterial.append(record.TargetFieldId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.PropertyPath);
    digestMaterial.push_back('|');
    digestMaterial.append(record.ExistingValue);
    digestMaterial.push_back('|');
    digestMaterial.append(record.ReplacementValue);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rationale);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.FindingId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.RuleId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.Owner);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.RemediationBatchId);
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputeSerializationMappingFixResultDigest(const FieldSerializationMappingFixResult& result) {
    std::string digestMaterial;
    digestMaterial.reserve((result.FixRecords.size() * 72u) + 176u);
    digestMaterial.append(result.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RemediationBatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.SerializedNameFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.AliasSetFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.SerializedPathFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.TotalFixCount));
    digestMaterial.push_back('\n');

    for (const FieldSerializationMappingFixRecord& record : result.FixRecords) {
        digestMaterial.append(record.FixId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.DeterministicDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(record.TargetFieldId);
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string BuildNormalizationId(const FieldDefaultFallbackNormalizationRecord& record) {
    std::string idMaterial;
    idMaterial.reserve(224u);
    idMaterial.append("normalize|");
    idMaterial.append(record.Provenance.FindingId);
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.NormalizationKind)));
    idMaterial.push_back('|');
    idMaterial.append(record.StableFieldKey);
    idMaterial.push_back('|');
    idMaterial.append(record.TargetFieldId);
    idMaterial.push_back('|');
    idMaterial.append(record.PropertyPath);
    idMaterial.push_back('|');
    idMaterial.append(record.NormalizedValue);
    return HashToHex(HashString(idMaterial));
}

[[nodiscard]] std::string ComputeNormalizationRecordDigest(const FieldDefaultFallbackNormalizationRecord& record) {
    std::string digestMaterial;
    digestMaterial.reserve(352u);
    digestMaterial.append(record.NormalizationId);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.NormalizationKind)));
    digestMaterial.push_back('|');
    digestMaterial.append(record.StableFieldKey);
    digestMaterial.push_back('|');
    digestMaterial.append(record.DomainPair);
    digestMaterial.push_back('|');
    digestMaterial.append(record.TargetFieldId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.PropertyPath);
    digestMaterial.push_back('|');
    digestMaterial.append(record.ExistingValue);
    digestMaterial.push_back('|');
    digestMaterial.append(record.NormalizedValue);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rationale);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.FindingId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.RuleId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.Owner);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Provenance.RemediationBatchId);
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputeNormalizationResultDigest(const FieldDefaultFallbackNormalizationResult& result) {
    std::string digestMaterial;
    digestMaterial.reserve((result.NormalizationRecords.size() * 72u) + 160u);
    digestMaterial.append(result.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RemediationBatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.DefaultPolicyNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.FallbackPolicyNormalizationCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.TotalNormalizationCount));
    digestMaterial.push_back('\n');

    for (const FieldDefaultFallbackNormalizationRecord& record : result.NormalizationRecords) {
        digestMaterial.append(record.NormalizationId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.DeterministicDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(record.TargetFieldId);
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

void SortNormalizationRecords(std::vector<FieldDefaultFallbackNormalizationRecord>& normalizationRecords) {
    std::sort(normalizationRecords.begin(),
              normalizationRecords.end(),
              [](const FieldDefaultFallbackNormalizationRecord& left,
                 const FieldDefaultFallbackNormalizationRecord& right) {
                  if (left.Provenance.FindingId != right.Provenance.FindingId) {
                      return left.Provenance.FindingId < right.Provenance.FindingId;
                  }
                  if (left.StableFieldKey != right.StableFieldKey) {
                      return left.StableFieldKey < right.StableFieldKey;
                  }
                  if (left.TargetFieldId != right.TargetFieldId) {
                      return left.TargetFieldId < right.TargetFieldId;
                  }
                  if (left.NormalizationKind != right.NormalizationKind) {
                      return static_cast<uint32_t>(left.NormalizationKind) <
                             static_cast<uint32_t>(right.NormalizationKind);
                  }
                  if (left.PropertyPath != right.PropertyPath) {
                      return left.PropertyPath < right.PropertyPath;
                  }
                  return left.NormalizationId < right.NormalizationId;
              });
}

void SortPatchRecords(std::vector<FieldSchemaPatchRecord>& patchRecords) {
    std::sort(patchRecords.begin(),
              patchRecords.end(),
              [](const FieldSchemaPatchRecord& left, const FieldSchemaPatchRecord& right) {
                  if (left.Provenance.FindingId != right.Provenance.FindingId) {
                      return left.Provenance.FindingId < right.Provenance.FindingId;
                  }
                  if (left.StableFieldKey != right.StableFieldKey) {
                      return left.StableFieldKey < right.StableFieldKey;
                  }
                  if (left.TargetFieldId != right.TargetFieldId) {
                      return left.TargetFieldId < right.TargetFieldId;
                  }
                  if (left.PatchKind != right.PatchKind) {
                      return static_cast<uint32_t>(left.PatchKind) < static_cast<uint32_t>(right.PatchKind);
                  }
                  if (left.PropertyPath != right.PropertyPath) {
                      return left.PropertyPath < right.PropertyPath;
                  }
                  return left.PatchId < right.PatchId;
               });
}

void SortSerializationMappingFixRecords(std::vector<FieldSerializationMappingFixRecord>& fixRecords) {
    std::sort(fixRecords.begin(),
              fixRecords.end(),
              [](const FieldSerializationMappingFixRecord& left,
                 const FieldSerializationMappingFixRecord& right) {
                  if (left.Provenance.FindingId != right.Provenance.FindingId) {
                      return left.Provenance.FindingId < right.Provenance.FindingId;
                  }
                  if (left.StableFieldKey != right.StableFieldKey) {
                      return left.StableFieldKey < right.StableFieldKey;
                  }
                  if (left.TargetFieldId != right.TargetFieldId) {
                      return left.TargetFieldId < right.TargetFieldId;
                  }
                  if (left.FixKind != right.FixKind) {
                      return static_cast<uint32_t>(left.FixKind) < static_cast<uint32_t>(right.FixKind);
                  }
                  if (left.PropertyPath != right.PropertyPath) {
                      return left.PropertyPath < right.PropertyPath;
                  }
                  return left.FixId < right.FixId;
              });
}

} // namespace

Result<FieldSchemaPatchResult> PatchFieldSchemaDefinitions(const FieldSchemaPatchRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.Findings.empty()) {
        return Result<FieldSchemaPatchResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "schema-definitions") {
        return Result<FieldSchemaPatchResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    for (const FieldSchemaPatchSourceFinding& finding : request.Findings) {
        if (!IsValidSourceFinding(finding)) {
            return Result<FieldSchemaPatchResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldSchemaPatchResult>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<FieldSchemaPatchRecord> patchRecords;
    std::map<std::string, bool> seenPatchKey;

    for (const FieldSchemaPatchSourceFinding& sourceFinding : request.Findings) {
        const Core::Audit::FieldValidationFinding& finding = sourceFinding.Finding;
        const Core::Audit::FieldValidationEvidence& canonical = SelectCanonicalEvidence(finding.LeftEvidence, finding.RightEvidence);

        const std::vector<const Core::Audit::FieldValidationEvidence*> evidences = {&finding.LeftEvidence, &finding.RightEvidence};

        auto emitPatch = [&](const FieldSchemaPatchKind patchKind,
                             const Core::Audit::FieldValidationEvidence& evidence,
                             const std::string& propertyPath,
                             const std::string& existingValue,
                             const std::string& replacementValue) {
            std::string dedupeKey;
            dedupeKey.reserve(160u);
            dedupeKey.append(sourceFinding.FindingId);
            dedupeKey.push_back('|');
            dedupeKey.append(evidence.FieldId);
            dedupeKey.push_back('|');
            dedupeKey.append(std::to_string(static_cast<uint32_t>(patchKind)));
            dedupeKey.push_back('|');
            dedupeKey.append(propertyPath);
            dedupeKey.push_back('|');
            dedupeKey.append(replacementValue);
            if (seenPatchKey.contains(dedupeKey)) {
                return;
            }
            seenPatchKey.emplace(dedupeKey, true);

            FieldSchemaPatchRecord record{};
            record.PatchKind = patchKind;
            record.StableFieldKey = finding.StableFieldKey;
            record.DomainPair = finding.DomainPair;
            record.TargetFieldId = evidence.FieldId;
            record.PropertyPath = propertyPath;
            record.ExistingValue = existingValue;
            record.ReplacementValue = replacementValue;
            record.Provenance.FindingId = sourceFinding.FindingId;
            record.Provenance.RuleId = finding.RuleId;
            record.Provenance.Owner = sourceFinding.Owner;
            record.Provenance.RemediationBatchId = request.RemediationBatchId;
            record.PatchId = BuildPatchId(record);
            record.DeterministicDigest = ComputePatchRecordDigest(record);
            patchRecords.push_back(std::move(record));
        };

        for (const Core::Audit::FieldValidationEvidence* evidence : evidences) {
            if (evidence->FieldId == canonical.FieldId) {
                continue;
            }

            if (evidence->TypeName != canonical.TypeName) {
                emitPatch(FieldSchemaPatchKind::TypeNameCorrection,
                          *evidence,
                          "typeName",
                          evidence->TypeName,
                          canonical.TypeName);
            }

            if (evidence->Required != canonical.Required) {
                emitPatch(FieldSchemaPatchKind::NullabilityFlagCorrection,
                          *evidence,
                          "nullability.required",
                          BoolToString(evidence->Required),
                          BoolToString(canonical.Required));
                emitPatch(FieldSchemaPatchKind::RequiredFlagCorrection,
                          *evidence,
                          "required",
                          BoolToString(evidence->Required),
                          BoolToString(canonical.Required));
            }
        }
    }

    SortPatchRecords(patchRecords);

    FieldSchemaPatchResult result{};
    result.Scope = request.Scope;
    result.OutputDirectory = request.OutputDirectory;
    result.RemediationBatchId = request.RemediationBatchId;
    result.PatchRecords = std::move(patchRecords);
    for (const FieldSchemaPatchRecord& record : result.PatchRecords) {
        if (record.PatchKind == FieldSchemaPatchKind::TypeNameCorrection) {
            ++result.Summary.TypePatchCount;
        } else if (record.PatchKind == FieldSchemaPatchKind::NullabilityFlagCorrection) {
            ++result.Summary.NullabilityPatchCount;
        } else if (record.PatchKind == FieldSchemaPatchKind::RequiredFlagCorrection) {
            ++result.Summary.RequiredPatchCount;
        }
    }
    result.Summary.TotalPatchCount = static_cast<uint32_t>(result.PatchRecords.size());
    result.DeterministicDigest = ComputePatchResultDigest(result);
    return Result<FieldSchemaPatchResult>::Success(std::move(result));
}

Result<FieldDefaultFallbackNormalizationResult> NormalizeFieldDefaultAndFallbackPolicies(
    const FieldDefaultFallbackNormalizationRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.Findings.empty()) {
        return Result<FieldDefaultFallbackNormalizationResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "schema-definitions") {
        return Result<FieldDefaultFallbackNormalizationResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    for (const FieldDefaultFallbackPolicyFinding& finding : request.Findings) {
        if (!IsValidDefaultFallbackFinding(finding)) {
            return Result<FieldDefaultFallbackNormalizationResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldDefaultFallbackNormalizationResult>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<FieldDefaultFallbackNormalizationRecord> normalizationRecords;
    std::map<std::string, bool> seenNormalizationKey;
    for (const FieldDefaultFallbackPolicyFinding& finding : request.Findings) {
        const FieldDefaultFallbackPolicyEvidence& canonical =
            SelectCanonicalDefaultFallbackEvidence(finding.LeftEvidence, finding.RightEvidence);
        const std::vector<const FieldDefaultFallbackPolicyEvidence*> evidences = {&finding.LeftEvidence,
                                                                                   &finding.RightEvidence};

        auto emitNormalization = [&](const FieldDefaultFallbackNormalizationKind normalizationKind,
                                     const FieldDefaultFallbackPolicyEvidence& evidence,
                                     const std::string& propertyPath,
                                     const std::string& existingValue,
                                     const std::string& normalizedValue,
                                     const std::string& rationale) {
            std::string dedupeKey;
            dedupeKey.reserve(224u);
            dedupeKey.append(finding.FindingId);
            dedupeKey.push_back('|');
            dedupeKey.append(evidence.FieldId);
            dedupeKey.push_back('|');
            dedupeKey.append(std::to_string(static_cast<uint32_t>(normalizationKind)));
            dedupeKey.push_back('|');
            dedupeKey.append(propertyPath);
            dedupeKey.push_back('|');
            dedupeKey.append(normalizedValue);
            if (seenNormalizationKey.contains(dedupeKey)) {
                return;
            }
            seenNormalizationKey.emplace(dedupeKey, true);

            FieldDefaultFallbackNormalizationRecord record{};
            record.NormalizationKind = normalizationKind;
            record.StableFieldKey = finding.StableFieldKey;
            record.DomainPair = finding.DomainPair;
            record.TargetFieldId = evidence.FieldId;
            record.PropertyPath = propertyPath;
            record.ExistingValue = existingValue;
            record.NormalizedValue = normalizedValue;
            record.Rationale = rationale;
            record.Provenance.FindingId = finding.FindingId;
            record.Provenance.RuleId = finding.RuleId;
            record.Provenance.Owner = finding.Owner;
            record.Provenance.RemediationBatchId = request.RemediationBatchId;
            record.NormalizationId = BuildNormalizationId(record);
            record.DeterministicDigest = ComputeNormalizationRecordDigest(record);
            normalizationRecords.push_back(std::move(record));
        };

        for (const FieldDefaultFallbackPolicyEvidence* evidence : evidences) {
            if (evidence->FieldId == canonical.FieldId) {
                continue;
            }

            const bool defaultMismatch = (evidence->HasDefaultValue != canonical.HasDefaultValue) ||
                                         (evidence->HasDefaultValue && canonical.HasDefaultValue &&
                                          evidence->DefaultValue != canonical.DefaultValue);
            if (defaultMismatch) {
                emitNormalization(FieldDefaultFallbackNormalizationKind::DefaultValuePolicy,
                                  *evidence,
                                  "defaultValue",
                                  OptionalValueToString(evidence->HasDefaultValue, evidence->DefaultValue),
                                  OptionalValueToString(canonical.HasDefaultValue, canonical.DefaultValue),
                                  "resolve-ambiguous-default-using-canonical-scope-order");
            }

            const bool fallbackMismatch = (evidence->HasFallbackPath != canonical.HasFallbackPath) ||
                                          (evidence->HasFallbackPath && canonical.HasFallbackPath &&
                                           evidence->FallbackPath != canonical.FallbackPath);
            if (fallbackMismatch) {
                emitNormalization(FieldDefaultFallbackNormalizationKind::FallbackPathPolicy,
                                  *evidence,
                                  "fallbackPath",
                                  OptionalValueToString(evidence->HasFallbackPath, evidence->FallbackPath),
                                  OptionalValueToString(canonical.HasFallbackPath, canonical.FallbackPath),
                                  "resolve-ambiguous-fallback-using-canonical-scope-order");
            }
        }
    }

    SortNormalizationRecords(normalizationRecords);

    FieldDefaultFallbackNormalizationResult result{};
    result.Scope = request.Scope;
    result.OutputDirectory = request.OutputDirectory;
    result.RemediationBatchId = request.RemediationBatchId;
    result.NormalizationRecords = std::move(normalizationRecords);
    for (const FieldDefaultFallbackNormalizationRecord& record : result.NormalizationRecords) {
        if (record.NormalizationKind == FieldDefaultFallbackNormalizationKind::DefaultValuePolicy) {
            ++result.Summary.DefaultPolicyNormalizationCount;
        } else if (record.NormalizationKind == FieldDefaultFallbackNormalizationKind::FallbackPathPolicy) {
            ++result.Summary.FallbackPolicyNormalizationCount;
        }
    }
    result.Summary.TotalNormalizationCount = static_cast<uint32_t>(result.NormalizationRecords.size());
    result.DeterministicDigest = ComputeNormalizationResultDigest(result);
    return Result<FieldDefaultFallbackNormalizationResult>::Success(std::move(result));
}

Result<FieldSerializationMappingFixResult> FixFieldSerializationMappings(
    const FieldSerializationMappingFixRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.Findings.empty()) {
        return Result<FieldSerializationMappingFixResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "schema-definitions") {
        return Result<FieldSerializationMappingFixResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    for (const FieldSerializationMappingFinding& finding : request.Findings) {
        if (!IsValidSerializationMappingFinding(finding)) {
            return Result<FieldSerializationMappingFixResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<FieldSerializationMappingFixResult>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<FieldSerializationMappingFixRecord> fixRecords;
    std::map<std::string, bool> seenFixKeys;
    for (const FieldSerializationMappingFinding& finding : request.Findings) {
        const std::vector<const FieldSerializationMappingEvidence*> evidences = {
            &finding.RuntimeEvidence, &finding.EditorEvidence, &finding.CookedEvidence};
        const FieldSerializationMappingEvidence& canonical = SelectCanonicalSerializationMappingEvidence(evidences);
        const std::vector<std::string> canonicalAliases = NormalizeAliasNames(canonical.AliasNames);
        const std::string canonicalAliasText = JoinAliasNames(canonicalAliases);

        auto emitFix = [&](const FieldSerializationMappingFixKind fixKind,
                           const FieldSerializationMappingEvidence& evidence,
                           const std::string& propertyPath,
                           const std::string& existingValue,
                           const std::string& replacementValue,
                           const std::string& rationale) {
            std::string dedupeKey;
            dedupeKey.reserve(224u);
            dedupeKey.append(finding.FindingId);
            dedupeKey.push_back('|');
            dedupeKey.append(evidence.FieldId);
            dedupeKey.push_back('|');
            dedupeKey.append(std::to_string(static_cast<uint32_t>(fixKind)));
            dedupeKey.push_back('|');
            dedupeKey.append(propertyPath);
            dedupeKey.push_back('|');
            dedupeKey.append(replacementValue);
            if (seenFixKeys.contains(dedupeKey)) {
                return;
            }
            seenFixKeys.emplace(dedupeKey, true);

            FieldSerializationMappingFixRecord record{};
            record.FixKind = fixKind;
            record.StableFieldKey = finding.StableFieldKey;
            record.DomainPair = finding.DomainPair;
            record.TargetFieldId = evidence.FieldId;
            record.PropertyPath = propertyPath;
            record.ExistingValue = existingValue;
            record.ReplacementValue = replacementValue;
            record.Rationale = rationale;
            record.Provenance.FindingId = finding.FindingId;
            record.Provenance.RuleId = finding.RuleId;
            record.Provenance.Owner = finding.Owner;
            record.Provenance.RemediationBatchId = request.RemediationBatchId;
            record.FixId = BuildSerializationMappingFixId(record);
            record.DeterministicDigest = ComputeSerializationMappingFixRecordDigest(record);
            fixRecords.push_back(std::move(record));
        };

        for (const FieldSerializationMappingEvidence* evidence : evidences) {
            if (evidence->FieldId == canonical.FieldId) {
                continue;
            }

            if (evidence->SerializedName != canonical.SerializedName) {
                emitFix(FieldSerializationMappingFixKind::SerializedNameCorrection,
                        *evidence,
                        "serialization.name",
                        evidence->SerializedName,
                        canonical.SerializedName,
                        "align-serialized-name-with-canonical-scope-order");
            }

            const std::vector<std::string> evidenceAliases = NormalizeAliasNames(evidence->AliasNames);
            const std::string evidenceAliasText = JoinAliasNames(evidenceAliases);
            if (evidenceAliasText != canonicalAliasText) {
                emitFix(FieldSerializationMappingFixKind::AliasSetCorrection,
                        *evidence,
                        "serialization.aliases",
                        evidenceAliasText,
                        canonicalAliasText,
                        "align-alias-set-with-canonical-scope-order");
            }

            if (evidence->SerializedPath != canonical.SerializedPath) {
                emitFix(FieldSerializationMappingFixKind::SerializedPathCorrection,
                        *evidence,
                        "serialization.path",
                        evidence->SerializedPath,
                        canonical.SerializedPath,
                        "align-serialized-path-with-canonical-scope-order");
            }
        }
    }

    SortSerializationMappingFixRecords(fixRecords);

    FieldSerializationMappingFixResult result{};
    result.Scope = request.Scope;
    result.OutputDirectory = request.OutputDirectory;
    result.RemediationBatchId = request.RemediationBatchId;
    result.FixRecords = std::move(fixRecords);
    for (const FieldSerializationMappingFixRecord& record : result.FixRecords) {
        if (record.FixKind == FieldSerializationMappingFixKind::SerializedNameCorrection) {
            ++result.Summary.SerializedNameFixCount;
        } else if (record.FixKind == FieldSerializationMappingFixKind::AliasSetCorrection) {
            ++result.Summary.AliasSetFixCount;
        } else if (record.FixKind == FieldSerializationMappingFixKind::SerializedPathCorrection) {
            ++result.Summary.SerializedPathFixCount;
        }
    }
    result.Summary.TotalFixCount = static_cast<uint32_t>(result.FixRecords.size());
    result.DeterministicDigest = ComputeSerializationMappingFixResultDigest(result);
    return Result<FieldSerializationMappingFixResult>::Success(std::move(result));
}

} // namespace Core::Remediation
