#include "Core/Remediation/RuntimeFieldFixService.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <map>
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

[[nodiscard]] std::string AsExistingValue(const std::string& value) {
    if (value.empty()) {
        return "<unset>";
    }
    return value;
}

[[nodiscard]] std::vector<std::string> SortUniqueAliases(const std::vector<std::string>& aliases) {
    std::vector<std::string> normalized = aliases;
    normalized.erase(std::remove_if(normalized.begin(),
                                    normalized.end(),
                                    [](const std::string& alias) { return alias.empty(); }),
                     normalized.end());
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}

[[nodiscard]] std::string JoinAliases(const std::vector<std::string>& aliases) {
    if (aliases.empty()) {
        return "<none>";
    }

    std::string joined;
    for (std::size_t index = 0; index < aliases.size(); ++index) {
        if (index > 0u) {
            joined.push_back(',');
        }
        joined.append(aliases[index]);
    }
    return joined;
}

[[nodiscard]] bool IsValidEntry(const RuntimeFieldFixEntry& entry) {
    if (entry.StableFieldKey.empty() || entry.DomainPair.empty() || entry.TargetFieldId.empty()) {
        return false;
    }
    if (entry.Provenance.FindingId.empty() || entry.Provenance.RuleId.empty() || entry.Provenance.Owner.empty()) {
        return false;
    }

    const bool hasBindingSignal = !entry.BindingPath.empty() || !entry.ExpectedBindingPath.empty();
    const bool hasReflectionRouteSignal = !entry.ReflectionRoute.empty() || !entry.ExpectedReflectionRoute.empty();
    const bool hasAliasSignal = !entry.InterfaceAliases.empty() || !entry.ExpectedInterfaceAliases.empty();
    return hasBindingSignal || hasReflectionRouteSignal || hasAliasSignal;
}

[[nodiscard]] std::string BuildFixId(const RuntimeFieldFixRecord& record) {
    std::string idMaterial;
    idMaterial.reserve(320u);
    idMaterial.append("runtime-field-fix|");
    idMaterial.append(record.Provenance.FindingId);
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.Domain)));
    idMaterial.push_back('|');
    idMaterial.append(std::to_string(static_cast<uint32_t>(record.FixKind)));
    idMaterial.push_back('|');
    idMaterial.append(record.TargetFieldId);
    idMaterial.push_back('|');
    idMaterial.append(record.PropertyPath);
    idMaterial.push_back('|');
    idMaterial.append(record.ReplacementValue);
    return HashToHex(HashString(idMaterial));
}

[[nodiscard]] std::string BuildRollbackCheckpointId(const RuntimeFieldFixRecord& record) {
    std::string checkpointMaterial;
    checkpointMaterial.reserve(320u);
    checkpointMaterial.append("runtime-field-rollback|");
    checkpointMaterial.append(record.Provenance.RemediationBatchId);
    checkpointMaterial.push_back('|');
    checkpointMaterial.append(record.TargetFieldId);
    checkpointMaterial.push_back('|');
    checkpointMaterial.append(record.PropertyPath);
    checkpointMaterial.push_back('|');
    checkpointMaterial.append(record.ExistingValue);
    checkpointMaterial.push_back('|');
    checkpointMaterial.append(record.Provenance.FindingId);
    return HashToHex(HashString(checkpointMaterial));
}

[[nodiscard]] std::string ComputeFixRecordDigest(const RuntimeFieldFixRecord& record) {
    std::string digestMaterial;
    digestMaterial.reserve(704u);
    digestMaterial.append(record.FixId);
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(static_cast<uint32_t>(record.Domain)));
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
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rollback.RollbackCheckpointId);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rollback.RollbackPropertyPath);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rollback.RollbackValue);
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rollback.RollbackRequired ? "true" : "false");
    digestMaterial.push_back('|');
    digestMaterial.append(record.Rollback.RollbackManifestPath);
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputeRollbackManifestDigest(const RuntimeFieldFixResult& result) {
    std::string digestMaterial;
    digestMaterial.reserve((result.FixRecords.size() * 96u) + 224u);
    digestMaterial.append(result.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RemediationBatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RollbackManifestPath.generic_string());
    digestMaterial.push_back('\n');

    for (const RuntimeFieldFixRecord& record : result.FixRecords) {
        digestMaterial.append(record.FixId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.Rollback.RollbackCheckpointId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.Rollback.RollbackPropertyPath);
        digestMaterial.push_back('|');
        digestMaterial.append(record.Rollback.RollbackValue);
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

[[nodiscard]] std::string ComputeResultDigest(const RuntimeFieldFixResult& result) {
    std::string digestMaterial;
    digestMaterial.reserve((result.FixRecords.size() * 80u) + 320u);
    digestMaterial.append(result.Scope);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RemediationBatchId);
    digestMaterial.push_back('|');
    digestMaterial.append(result.RollbackManifestPath.generic_string());
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ECSFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.UIBindingFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.AnimationFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ToolingFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.BindingPathFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ReflectionRouteFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ReflectionInterfaceAliasFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.RollbackSafeFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.TotalFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(result.RollbackManifestDigest);
    digestMaterial.push_back('\n');

    for (const RuntimeFieldFixRecord& record : result.FixRecords) {
        digestMaterial.append(record.FixId);
        digestMaterial.push_back('|');
        digestMaterial.append(record.DeterministicDigest);
        digestMaterial.push_back('|');
        digestMaterial.append(record.TargetFieldId);
        digestMaterial.push_back('\n');
    }
    return HashToHex(HashString(digestMaterial));
}

void SortFixRecords(std::vector<RuntimeFieldFixRecord>& records) {
    std::sort(records.begin(), records.end(), [](const RuntimeFieldFixRecord& left, const RuntimeFieldFixRecord& right) {
        if (left.Domain != right.Domain) {
            return static_cast<uint32_t>(left.Domain) < static_cast<uint32_t>(right.Domain);
        }
        if (left.FixKind != right.FixKind) {
            return static_cast<uint32_t>(left.FixKind) < static_cast<uint32_t>(right.FixKind);
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
        if (left.ReplacementValue != right.ReplacementValue) {
            return left.ReplacementValue < right.ReplacementValue;
        }
        return left.Provenance.FindingId < right.Provenance.FindingId;
    });
}

} // namespace

Result<RuntimeFieldFixResult> FixRuntimeFieldBindingAndReflectionRoutes(const RuntimeFieldFixRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.RollbackManifestPath.empty() || request.Entries.empty()) {
        return Result<RuntimeFieldFixResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "runtime-field-routes") {
        return Result<RuntimeFieldFixResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    for (const RuntimeFieldFixEntry& entry : request.Entries) {
        if (!IsValidEntry(entry)) {
            return Result<RuntimeFieldFixResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
        }
    }

    if (!EnsureOutputDirectory(request.OutputDirectory)) {
        return Result<RuntimeFieldFixResult>::Failure("FIELD_AUDIT_REPORT_WRITE_FAILED");
    }

    std::vector<RuntimeFieldFixRecord> fixRecords;
    std::map<std::string, bool> seenFixKeys;

    for (const RuntimeFieldFixEntry& entry : request.Entries) {
        auto emitFix = [&](const RuntimeFieldFixKind fixKind,
                           const std::string& propertyPath,
                           const std::string& existingValue,
                           const std::string& replacementValue,
                           const std::string& rationale) {
            std::string dedupeKey;
            dedupeKey.reserve(288u);
            dedupeKey.append(entry.Provenance.FindingId);
            dedupeKey.push_back('|');
            dedupeKey.append(entry.TargetFieldId);
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

            RuntimeFieldFixRecord record{};
            record.Domain = entry.Domain;
            record.FixKind = fixKind;
            record.StableFieldKey = entry.StableFieldKey;
            record.DomainPair = entry.DomainPair;
            record.TargetFieldId = entry.TargetFieldId;
            record.PropertyPath = propertyPath;
            record.ExistingValue = existingValue;
            record.ReplacementValue = replacementValue;
            record.Rationale = rationale;
            record.Provenance = entry.Provenance;
            record.Provenance.RemediationBatchId = request.RemediationBatchId;
            record.Rollback.RollbackPropertyPath = propertyPath;
            record.Rollback.RollbackValue = existingValue;
            record.Rollback.RollbackManifestPath = request.RollbackManifestPath.generic_string();
            record.FixId = BuildFixId(record);
            record.Rollback.RollbackCheckpointId = BuildRollbackCheckpointId(record);
            record.DeterministicDigest = ComputeFixRecordDigest(record);
            fixRecords.push_back(std::move(record));
        };

        if (entry.BindingPath != entry.ExpectedBindingPath) {
            emitFix(RuntimeFieldFixKind::BindingPathCorrection,
                    "binding.path",
                    AsExistingValue(entry.BindingPath),
                    AsExistingValue(entry.ExpectedBindingPath),
                    "align-binding-path-with-runtime-canonical-route");
        }

        if (entry.ReflectionRoute != entry.ExpectedReflectionRoute) {
            emitFix(RuntimeFieldFixKind::ReflectionRouteCorrection,
                    "reflection.route",
                    AsExistingValue(entry.ReflectionRoute),
                    AsExistingValue(entry.ExpectedReflectionRoute),
                    "align-reflection-route-with-runtime-canonical-route");
        }

        const std::vector<std::string> existingAliases = SortUniqueAliases(entry.InterfaceAliases);
        const std::vector<std::string> expectedAliases = SortUniqueAliases(entry.ExpectedInterfaceAliases);
        const std::string existingAliasText = JoinAliases(existingAliases);
        const std::string expectedAliasText = JoinAliases(expectedAliases);
        if (existingAliasText != expectedAliasText) {
            emitFix(RuntimeFieldFixKind::ReflectionInterfaceAliasCorrection,
                    "reflection.interfaceAliases",
                    existingAliasText,
                    expectedAliasText,
                    "align-reflection-interface-aliases-with-runtime-canonical-route");
        }
    }

    SortFixRecords(fixRecords);

    RuntimeFieldFixResult result{};
    result.Scope = request.Scope;
    result.OutputDirectory = request.OutputDirectory;
    result.RemediationBatchId = request.RemediationBatchId;
    result.RollbackManifestPath = request.RollbackManifestPath;
    result.FixRecords = std::move(fixRecords);

    for (const RuntimeFieldFixRecord& record : result.FixRecords) {
        if (record.Domain == RuntimeFieldDomain::ECS) {
            ++result.Summary.ECSFixCount;
        } else if (record.Domain == RuntimeFieldDomain::UIBinding) {
            ++result.Summary.UIBindingFixCount;
        } else if (record.Domain == RuntimeFieldDomain::Animation) {
            ++result.Summary.AnimationFixCount;
        } else if (record.Domain == RuntimeFieldDomain::Tooling) {
            ++result.Summary.ToolingFixCount;
        }

        if (record.FixKind == RuntimeFieldFixKind::BindingPathCorrection) {
            ++result.Summary.BindingPathFixCount;
        } else if (record.FixKind == RuntimeFieldFixKind::ReflectionRouteCorrection) {
            ++result.Summary.ReflectionRouteFixCount;
        } else if (record.FixKind == RuntimeFieldFixKind::ReflectionInterfaceAliasCorrection) {
            ++result.Summary.ReflectionInterfaceAliasFixCount;
        }

        if (record.Rollback.RollbackRequired) {
            ++result.Summary.RollbackSafeFixCount;
        }
    }

    result.Summary.TotalFixCount = static_cast<uint32_t>(result.FixRecords.size());
    result.RollbackManifestDigest = ComputeRollbackManifestDigest(result);
    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<RuntimeFieldFixResult>::Success(std::move(result));
}

} // namespace Core::Remediation
