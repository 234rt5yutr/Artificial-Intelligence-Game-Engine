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

[[nodiscard]] bool IsValidParityEntry(const RuntimeFieldFixEntry& entry) {
    if (entry.StableFieldKey.empty() || entry.DomainPair.empty() || entry.TargetFieldId.empty()) {
        return false;
    }
    if (entry.Provenance.FindingId.empty() || entry.Provenance.RuleId.empty() || entry.Provenance.Owner.empty()) {
        return false;
    }

    if (entry.Domain == RuntimeFieldDomain::Replication) {
        return !entry.ReplicationAuthoritativeSchema.empty() && !entry.ReplicationClientSchema.empty();
    }

    if (entry.Domain == RuntimeFieldDomain::RPC) {
        if (entry.RPCCanonicalPayloadSchema.empty()) {
            return false;
        }
        return !entry.RPCRequestPayloadSchema.empty() || !entry.RPCResponsePayloadSchema.empty();
    }

    if (entry.Domain == RuntimeFieldDomain::ReplayRollback) {
        return !entry.ReplaySchema.empty() && !entry.RollbackSchema.empty();
    }

    return false;
}

[[nodiscard]] bool IsValidDeterminismEntry(const RuntimeFieldFixEntry& entry) {
    if (entry.StableFieldKey.empty() || entry.DomainPair.empty() || entry.TargetFieldId.empty()) {
        return false;
    }
    if (entry.Provenance.FindingId.empty() || entry.Provenance.RuleId.empty() || entry.Provenance.Owner.empty()) {
        return false;
    }

    const bool hasFramePhaseSignal = !entry.FramePhaseOrdering.empty() || !entry.ExpectedFramePhaseOrdering.empty();
    const bool hasJobBoundarySignal = !entry.JobBoundaryOrdering.empty() || !entry.ExpectedJobBoundaryOrdering.empty();
    const bool hasCheckpointSignal =
        !entry.SerializationCheckpointOrdering.empty() || !entry.ExpectedSerializationCheckpointOrdering.empty();
    return hasFramePhaseSignal || hasJobBoundarySignal || hasCheckpointSignal;
}

[[nodiscard]] bool IsValidStoreAndDedicatedServerEntry(const RuntimeFieldFixEntry& entry) {
    if (entry.StableFieldKey.empty() || entry.DomainPair.empty() || entry.TargetFieldId.empty()) {
        return false;
    }
    if (entry.Provenance.FindingId.empty() || entry.Provenance.RuleId.empty() || entry.Provenance.Owner.empty()) {
        return false;
    }

    if (entry.Domain == RuntimeFieldDomain::Store) {
        return !entry.StoreReleaseArtifactMetadataContract.empty() &&
               !entry.ExpectedStoreReleaseArtifactMetadataContract.empty();
    }

    if (entry.Domain == RuntimeFieldDomain::DedicatedServer) {
        const bool hasDescriptorSignal = !entry.DedicatedServerDeploymentDescriptor.empty() &&
                                         !entry.ExpectedDedicatedServerDeploymentDescriptor.empty();
        const bool hasManifestSignal =
            !entry.DedicatedServerArtifactManifest.empty() && !entry.ExpectedDedicatedServerArtifactManifest.empty();
        return hasDescriptorSignal || hasManifestSignal;
    }

    return false;
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
    digestMaterial.append(std::to_string(result.Summary.StoreFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.DedicatedServerFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.BindingPathFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ReflectionRouteFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ReflectionInterfaceAliasFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ReplicationFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.RPCFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ReplayRollbackFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ReplicationSchemaParityFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.RPCPayloadParityFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.ReplayRollbackSchemaParityFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.FramePhaseOrderingFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.JobBoundaryOrderingFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.SerializationCheckpointOrderingFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.StoreReleaseArtifactMetadataContractFixCount));
    digestMaterial.push_back('|');
    digestMaterial.append(std::to_string(result.Summary.DedicatedServerDeploymentManifestContractFixCount));
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

void AddSummaryDomainCount(RuntimeFieldFixSummary& summary, const RuntimeFieldDomain domain) {
    if (domain == RuntimeFieldDomain::ECS) {
        ++summary.ECSFixCount;
    } else if (domain == RuntimeFieldDomain::UIBinding) {
        ++summary.UIBindingFixCount;
    } else if (domain == RuntimeFieldDomain::Animation) {
        ++summary.AnimationFixCount;
    } else if (domain == RuntimeFieldDomain::Tooling) {
        ++summary.ToolingFixCount;
    } else if (domain == RuntimeFieldDomain::Replication) {
        ++summary.ReplicationFixCount;
    } else if (domain == RuntimeFieldDomain::RPC) {
        ++summary.RPCFixCount;
    } else if (domain == RuntimeFieldDomain::ReplayRollback) {
        ++summary.ReplayRollbackFixCount;
    } else if (domain == RuntimeFieldDomain::Store) {
        ++summary.StoreFixCount;
    } else if (domain == RuntimeFieldDomain::DedicatedServer) {
        ++summary.DedicatedServerFixCount;
    }
}

void AddSummaryFixKindCount(RuntimeFieldFixSummary& summary, const RuntimeFieldFixKind fixKind) {
    if (fixKind == RuntimeFieldFixKind::BindingPathCorrection) {
        ++summary.BindingPathFixCount;
    } else if (fixKind == RuntimeFieldFixKind::ReflectionRouteCorrection) {
        ++summary.ReflectionRouteFixCount;
    } else if (fixKind == RuntimeFieldFixKind::ReflectionInterfaceAliasCorrection) {
        ++summary.ReflectionInterfaceAliasFixCount;
    } else if (fixKind == RuntimeFieldFixKind::ReplicationSchemaParityCorrection) {
        ++summary.ReplicationSchemaParityFixCount;
    } else if (fixKind == RuntimeFieldFixKind::RPCPayloadParityCorrection) {
        ++summary.RPCPayloadParityFixCount;
    } else if (fixKind == RuntimeFieldFixKind::ReplayRollbackSchemaParityCorrection) {
        ++summary.ReplayRollbackSchemaParityFixCount;
    } else if (fixKind == RuntimeFieldFixKind::FramePhaseOrderingCorrection) {
        ++summary.FramePhaseOrderingFixCount;
    } else if (fixKind == RuntimeFieldFixKind::JobBoundaryOrderingCorrection) {
        ++summary.JobBoundaryOrderingFixCount;
    } else if (fixKind == RuntimeFieldFixKind::SerializationCheckpointOrderingCorrection) {
        ++summary.SerializationCheckpointOrderingFixCount;
    } else if (fixKind == RuntimeFieldFixKind::StoreReleaseArtifactMetadataContractCorrection) {
        ++summary.StoreReleaseArtifactMetadataContractFixCount;
    } else if (fixKind == RuntimeFieldFixKind::DedicatedServerDeploymentManifestContractCorrection) {
        ++summary.DedicatedServerDeploymentManifestContractFixCount;
    }
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
        AddSummaryDomainCount(result.Summary, record.Domain);
        AddSummaryFixKindCount(result.Summary, record.FixKind);

        if (record.Rollback.RollbackRequired) {
            ++result.Summary.RollbackSafeFixCount;
        }
    }

    result.Summary.TotalFixCount = static_cast<uint32_t>(result.FixRecords.size());
    result.RollbackManifestDigest = ComputeRollbackManifestDigest(result);
    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<RuntimeFieldFixResult>::Success(std::move(result));
}

Result<RuntimeFieldFixResult> FixReplicationRPCAndRollbackFieldParity(const RuntimeFieldFixRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.RollbackManifestPath.empty() || request.Entries.empty()) {
        return Result<RuntimeFieldFixResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "runtime-netcode-parity") {
        return Result<RuntimeFieldFixResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    for (const RuntimeFieldFixEntry& entry : request.Entries) {
        if (!IsValidParityEntry(entry)) {
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

        if (entry.Domain == RuntimeFieldDomain::Replication &&
            entry.ReplicationClientSchema != entry.ReplicationAuthoritativeSchema) {
            emitFix(RuntimeFieldFixKind::ReplicationSchemaParityCorrection,
                    "replication.clientSchema",
                    AsExistingValue(entry.ReplicationClientSchema),
                    AsExistingValue(entry.ReplicationAuthoritativeSchema),
                    "align-replication-client-schema-with-authoritative-schema");
        }

        if (entry.Domain == RuntimeFieldDomain::RPC) {
            if (entry.RPCRequestPayloadSchema != entry.RPCCanonicalPayloadSchema) {
                emitFix(RuntimeFieldFixKind::RPCPayloadParityCorrection,
                        "rpc.requestPayloadSchema",
                        AsExistingValue(entry.RPCRequestPayloadSchema),
                        AsExistingValue(entry.RPCCanonicalPayloadSchema),
                        "align-rpc-request-payload-with-canonical-schema");
            }

            if (entry.RPCResponsePayloadSchema != entry.RPCCanonicalPayloadSchema) {
                emitFix(RuntimeFieldFixKind::RPCPayloadParityCorrection,
                        "rpc.responsePayloadSchema",
                        AsExistingValue(entry.RPCResponsePayloadSchema),
                        AsExistingValue(entry.RPCCanonicalPayloadSchema),
                        "align-rpc-response-payload-with-canonical-schema");
            }
        }

        if (entry.Domain == RuntimeFieldDomain::ReplayRollback && entry.RollbackSchema != entry.ReplaySchema) {
            emitFix(RuntimeFieldFixKind::ReplayRollbackSchemaParityCorrection,
                    "rollback.schema",
                    AsExistingValue(entry.RollbackSchema),
                    AsExistingValue(entry.ReplaySchema),
                    "align-rollback-schema-with-replay-schema");
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
        AddSummaryDomainCount(result.Summary, record.Domain);
        AddSummaryFixKindCount(result.Summary, record.FixKind);

        if (record.Rollback.RollbackRequired) {
            ++result.Summary.RollbackSafeFixCount;
        }
    }

    result.Summary.TotalFixCount = static_cast<uint32_t>(result.FixRecords.size());
    result.RollbackManifestDigest = ComputeRollbackManifestDigest(result);
    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<RuntimeFieldFixResult>::Success(std::move(result));
}

Result<RuntimeFieldFixResult> FixFieldUpdateOrderingForDeterminism(const RuntimeFieldFixRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.RollbackManifestPath.empty() || request.Entries.empty()) {
        return Result<RuntimeFieldFixResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "runtime-ordering-determinism") {
        return Result<RuntimeFieldFixResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    for (const RuntimeFieldFixEntry& entry : request.Entries) {
        if (!IsValidDeterminismEntry(entry)) {
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

        if (entry.FramePhaseOrdering != entry.ExpectedFramePhaseOrdering) {
            emitFix(RuntimeFieldFixKind::FramePhaseOrderingCorrection,
                    "ordering.framePhases",
                    AsExistingValue(entry.FramePhaseOrdering),
                    AsExistingValue(entry.ExpectedFramePhaseOrdering),
                    "align-frame-phase-ordering-with-deterministic-runtime-schedule");
        }

        if (entry.JobBoundaryOrdering != entry.ExpectedJobBoundaryOrdering) {
            emitFix(RuntimeFieldFixKind::JobBoundaryOrderingCorrection,
                    "ordering.jobBoundaries",
                    AsExistingValue(entry.JobBoundaryOrdering),
                    AsExistingValue(entry.ExpectedJobBoundaryOrdering),
                    "align-job-boundary-ordering-with-deterministic-runtime-schedule");
        }

        if (entry.SerializationCheckpointOrdering != entry.ExpectedSerializationCheckpointOrdering) {
            emitFix(RuntimeFieldFixKind::SerializationCheckpointOrderingCorrection,
                    "ordering.serializationCheckpoints",
                    AsExistingValue(entry.SerializationCheckpointOrdering),
                    AsExistingValue(entry.ExpectedSerializationCheckpointOrdering),
                    "align-serialization-checkpoint-ordering-with-deterministic-runtime-schedule");
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
        AddSummaryDomainCount(result.Summary, record.Domain);
        AddSummaryFixKindCount(result.Summary, record.FixKind);

        if (record.Rollback.RollbackRequired) {
            ++result.Summary.RollbackSafeFixCount;
        }
    }

    result.Summary.TotalFixCount = static_cast<uint32_t>(result.FixRecords.size());
    result.RollbackManifestDigest = ComputeRollbackManifestDigest(result);
    result.DeterministicDigest = ComputeResultDigest(result);
    return Result<RuntimeFieldFixResult>::Success(std::move(result));
}

Result<RuntimeFieldFixResult> FixStoreAndDedicatedServerFieldContracts(const RuntimeFieldFixRequest& request) {
    if (request.Scope.empty() || request.OutputDirectory.empty() || request.RemediationBatchId.empty() ||
        request.RollbackManifestPath.empty() || request.Entries.empty()) {
        return Result<RuntimeFieldFixResult>::Failure("FIELD_AUDIT_ARGUMENT_INVALID");
    }

    if (request.Scope != "runtime-store-server-contracts") {
        return Result<RuntimeFieldFixResult>::Failure("FIELD_AUDIT_SCOPE_UNSUPPORTED");
    }

    for (const RuntimeFieldFixEntry& entry : request.Entries) {
        if (!IsValidStoreAndDedicatedServerEntry(entry)) {
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

        if (entry.Domain == RuntimeFieldDomain::Store &&
            entry.StoreReleaseArtifactMetadataContract != entry.ExpectedStoreReleaseArtifactMetadataContract) {
            emitFix(RuntimeFieldFixKind::StoreReleaseArtifactMetadataContractCorrection,
                    "store.releaseArtifactMetadataContract",
                    AsExistingValue(entry.StoreReleaseArtifactMetadataContract),
                    AsExistingValue(entry.ExpectedStoreReleaseArtifactMetadataContract),
                    "align-store-release-artifact-metadata-contract-with-canonical-release-manifest");
        }

        if (entry.Domain == RuntimeFieldDomain::DedicatedServer) {
            if (entry.DedicatedServerDeploymentDescriptor != entry.ExpectedDedicatedServerDeploymentDescriptor) {
                emitFix(RuntimeFieldFixKind::DedicatedServerDeploymentManifestContractCorrection,
                        "dedicatedServer.deploymentDescriptor",
                        AsExistingValue(entry.DedicatedServerDeploymentDescriptor),
                        AsExistingValue(entry.ExpectedDedicatedServerDeploymentDescriptor),
                        "align-dedicated-server-deployment-descriptor-with-canonical-contract");
            }

            if (entry.DedicatedServerArtifactManifest != entry.ExpectedDedicatedServerArtifactManifest) {
                emitFix(RuntimeFieldFixKind::DedicatedServerDeploymentManifestContractCorrection,
                        "dedicatedServer.artifactManifest",
                        AsExistingValue(entry.DedicatedServerArtifactManifest),
                        AsExistingValue(entry.ExpectedDedicatedServerArtifactManifest),
                        "align-dedicated-server-artifact-manifest-with-canonical-contract");
            }
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
        AddSummaryDomainCount(result.Summary, record.Domain);
        AddSummaryFixKindCount(result.Summary, record.FixKind);

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
