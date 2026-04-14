#pragma once

#include "Core/Audit/FieldAuditTypes.h"

namespace Core::Audit {

Result<FieldValidationReport> ValidateFieldTypeAndNullabilityContracts(const FieldValidationRequest& request);
Result<FieldValidationReport> ValidateFieldRangeEnumAndPatternDomains(const FieldValidationRequest& request);
Result<FieldValidationReport> ValidateCrossFieldInvariantRules(const FieldValidationRequest& request);
Result<FieldValidationReport> ValidateFieldEvolutionCompatibility(const FieldValidationRequest& request);

} // namespace Core::Audit
