#pragma once

#include "Core/Remediation/FieldClosureTypes.h"

namespace Core::Remediation {

Result<FieldClosureResult> ReRunFullFieldAuditAndDiffAgainstBaseline(const FieldClosureRequest& request);

} // namespace Core::Remediation
