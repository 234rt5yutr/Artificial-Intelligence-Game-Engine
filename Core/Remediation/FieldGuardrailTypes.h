#pragma once

#include "Core/Asset/Addressables/AddressablesCatalogTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Core::Remediation {

template <typename T>
using Result = Core::Asset::Addressables::Result<T>;

enum class FieldGuardrailDomain : uint8_t {
    Runtime = 0,
    Editor = 1,
    Build = 2
};

struct FieldGuardrailLineageMetadata {
    std::string FindingId;
    std::string RuleId;
    std::string Owner;
    std::string RemediationBatchId;
};

struct FieldGuardrailTaxonomyMetadata {
    std::string TaxonomyId;
    std::string Category;
    std::string Invariant;
    FieldGuardrailLineageMetadata Lineage;
};

struct FieldGuardrailRegressionSuiteMetadata {
    std::string SuiteId;
    std::vector<std::string> Stage30CoverageMap;
};

struct FieldGuardrailEntry {
    FieldGuardrailDomain Domain = FieldGuardrailDomain::Runtime;
    std::string StableFieldKey;
    std::string DomainPair;
    std::string TargetFieldId;
    std::string PropertyPath;
    std::string AssertionExpression;
    std::string ExpectedAssertionExpression;
    std::string Rationale;
    FieldGuardrailTaxonomyMetadata Taxonomy;
    FieldGuardrailRegressionSuiteMetadata RegressionSuite;
};

struct FieldGuardrailRequest {
    std::string Scope = "field-invariant-assertions";
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::vector<FieldGuardrailEntry> Entries;
};

struct FieldGuardrailRecord {
    std::string AssertionId;
    FieldGuardrailDomain Domain = FieldGuardrailDomain::Runtime;
    std::string StableFieldKey;
    std::string DomainPair;
    std::string TargetFieldId;
    std::string PropertyPath;
    std::string ExistingAssertionExpression;
    std::string AssertionExpression;
    std::string Rationale;
    FieldGuardrailTaxonomyMetadata Taxonomy;
    FieldGuardrailRegressionSuiteMetadata RegressionSuite;
    std::string DeterministicDigest;
};

struct FieldGuardrailSummary {
    uint32_t RuntimeAssertionCount = 0;
    uint32_t EditorAssertionCount = 0;
    uint32_t BuildAssertionCount = 0;
    uint32_t TotalAssertionCount = 0;
    uint32_t RegressionSuiteCount = 0;
    uint32_t RegressionCoverageSignalCount = 0;
    uint32_t RegressionCoverageCorrectionCount = 0;
};

struct FieldGuardrailResult {
    std::string Scope;
    std::filesystem::path OutputDirectory;
    std::string RemediationBatchId;
    std::vector<FieldGuardrailRecord> AssertionRecords;
    FieldGuardrailSummary Summary;
    std::string DeterministicDigest;
};

} // namespace Core::Remediation
