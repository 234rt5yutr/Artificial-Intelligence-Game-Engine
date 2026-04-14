#include "Core/Remediation/FieldSchemaPatchService.h"

#include <algorithm>
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
    if (scope == "serialized") {
        return 1u;
    }
    if (scope == "protocol") {
        return 2u;
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

} // namespace Core::Remediation
