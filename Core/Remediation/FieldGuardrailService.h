#pragma once

#include "Core/Remediation/FieldGuardrailTypes.h"

namespace Core::Remediation {

Result<FieldGuardrailResult> AddFieldInvariantAssertions(const FieldGuardrailRequest& request);

} // namespace Core::Remediation
