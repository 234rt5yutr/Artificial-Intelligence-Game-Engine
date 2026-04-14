#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Core::Audit {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

struct FieldSourceTraceMetadata {
    std::string SourceFile;
    std::string SourceSymbol;
    std::string CollectorId;
    uint32_t SourceLine = 0;
};

struct FieldInventoryEntry {
    std::string FieldId;
    std::string Domain;
    std::string OwnerSubsystem;
    std::string TypeName;
    std::string FieldPath;
    bool Required = true;
    bool HasNumericMinimum = false;
    double NumericMinimumInclusive = 0.0;
    bool HasNumericMaximum = false;
    double NumericMaximumInclusive = 0.0;
    std::vector<std::string> EnumDomainValues;
    std::string StringPattern;
    std::string NormalizedIdentifier;
    std::vector<std::string> AliasFieldIds;
    std::vector<std::string> VersionLineage;
    FieldSourceTraceMetadata SourceTrace;
};

struct FieldInventoryRequest {
    std::string Scope = "runtime";
    std::filesystem::path OutputDirectory;
};

struct FieldInventorySnapshot {
    std::string Scope;
    std::filesystem::path OutputDirectory;
    std::vector<std::string> Domains;
    std::vector<FieldInventoryEntry> Entries;
    std::string DeterministicDigest;
};

struct MergeFieldInventoryRequest {
    std::string Scope = "merged";
    std::filesystem::path OutputDirectory;
    FieldInventorySnapshot RuntimeSnapshot;
    FieldInventorySnapshot SerializedSnapshot;
    FieldInventorySnapshot ProtocolSnapshot;
};

enum class FieldValidationMismatchKind : uint8_t {
    TypeMismatch = 0,
    NullabilityMismatch = 1,
    RangeDomainMismatch = 2,
    EnumDomainMismatch = 3,
    PatternDomainMismatch = 4,
    IdentifierNormalizationMismatch = 5,
    ConditionalRequiredInvariantMismatch = 6,
    DependencyOrderingInvariantMismatch = 7,
    RelatedFieldConsistencyInvariantMismatch = 8,
    EvolutionBackwardCompatibilityMismatch = 9,
    EvolutionForwardCompatibilityMismatch = 10,
    EvolutionMigrationWindowMismatch = 11
};

struct FieldValidationRequest {
    std::string Scope = "type-nullability";
    std::filesystem::path OutputDirectory;
    FieldInventorySnapshot RuntimeSnapshot;
    FieldInventorySnapshot SerializedSnapshot;
    FieldInventorySnapshot ProtocolSnapshot;
};

struct FieldValidationEvidence {
    std::string SnapshotScope;
    std::string Domain;
    std::string FieldId;
    std::string TypeName;
    std::string FieldPath;
    bool Required = true;
    bool HasNumericMinimum = false;
    double NumericMinimumInclusive = 0.0;
    bool HasNumericMaximum = false;
    double NumericMaximumInclusive = 0.0;
    std::vector<std::string> EnumDomainValues;
    std::string StringPattern;
    std::string NormalizedIdentifier;
    FieldSourceTraceMetadata SourceTrace;
};

struct FieldValidationFinding {
    std::string RuleId;
    FieldValidationMismatchKind MismatchKind = FieldValidationMismatchKind::TypeMismatch;
    std::string StableFieldKey;
    std::string DomainPair;
    std::string MigrationRecommendationPlaceholder;
    FieldValidationEvidence LeftEvidence;
    FieldValidationEvidence RightEvidence;
};

struct FieldValidationSummary {
    uint32_t ComparedFieldCount = 0;
    uint32_t TypeMismatchCount = 0;
    uint32_t NullabilityMismatchCount = 0;
    uint32_t RangeDomainMismatchCount = 0;
    uint32_t EnumDomainMismatchCount = 0;
    uint32_t PatternDomainMismatchCount = 0;
    uint32_t IdentifierNormalizationMismatchCount = 0;
    uint32_t ConditionalRequiredInvariantMismatchCount = 0;
    uint32_t DependencyOrderingInvariantMismatchCount = 0;
    uint32_t RelatedFieldConsistencyInvariantMismatchCount = 0;
    uint32_t EvolutionBackwardCompatibilityMismatchCount = 0;
    uint32_t EvolutionForwardCompatibilityMismatchCount = 0;
    uint32_t EvolutionMigrationWindowMismatchCount = 0;
    uint32_t TotalFindingCount = 0;
};

struct FieldValidationReport {
    std::string Scope;
    std::filesystem::path OutputDirectory;
    std::vector<FieldValidationFinding> Findings;
    FieldValidationSummary Summary;
    std::string DeterministicDigest;
};

} // namespace Core::Audit
