#pragma once

#include "Core/Audit/FieldAuditTypes.h"

namespace Core::Audit {

Result<FieldValidationReport> ValidateFieldTypeAndNullabilityContracts(const FieldValidationRequest& request);

} // namespace Core::Audit
