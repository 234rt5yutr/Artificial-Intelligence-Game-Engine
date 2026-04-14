#pragma once

#include "Core/Remediation/FieldSchemaPatchTypes.h"

namespace Core::Remediation {

Result<FieldSchemaPatchResult> PatchFieldSchemaDefinitions(const FieldSchemaPatchRequest& request);
Result<FieldDefaultFallbackNormalizationResult> NormalizeFieldDefaultAndFallbackPolicies(
    const FieldDefaultFallbackNormalizationRequest& request);
Result<FieldSerializationMappingFixResult> FixFieldSerializationMappings(
    const FieldSerializationMappingFixRequest& request);
Result<FieldSchemaMigrationResult> VersionAndApplyFieldSchemaMigrations(const FieldSchemaMigrationRequest& request);

} // namespace Core::Remediation
