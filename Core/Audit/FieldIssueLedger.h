#pragma once

#include "Core/Audit/FieldAuditTypes.h"

namespace Core::Audit {

Result<FieldIssueLedgerReport> GenerateFieldAuditIssueLedger(const FieldIssueLedgerRequest& request);
Result<FieldIssueLedgerReport> ComputeFieldIssueSeverityAndBlastRadius(const FieldIssueLedgerRequest& request);

} // namespace Core::Audit
