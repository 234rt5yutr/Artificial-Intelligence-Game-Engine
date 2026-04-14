#pragma once

#include "Core/Remediation/FieldSchemaPatchTypes.h"

namespace Core::Remediation {

Result<FieldSchemaPatchResult> PatchFieldSchemaDefinitions(const FieldSchemaPatchRequest& request);
Result<FieldDefaultFallbackNormalizationResult> NormalizeFieldDefaultAndFallbackPolicies(
    const FieldDefaultFallbackNormalizationRequest& request);

} // namespace Core::Remediation
