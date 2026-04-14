#pragma once

#include "Core/Remediation/FieldClosureTypes.h"

namespace Core::Remediation {

Result<FieldClosureResult> ReRunFullFieldAuditAndDiffAgainstBaseline(const FieldClosureRequest& request);
Result<FieldClosureResult> EnforceZeroCriticalFieldDefectGate(const FieldClosureRequest& request);
Result<FieldClosureResult> PublishFieldIntegritySignoffReport(const FieldClosureRequest& request);
Result<FieldClosureResult> FreezeFieldContractVersionForNextPhase(const FieldClosureRequest& request);

} // namespace Core::Remediation
