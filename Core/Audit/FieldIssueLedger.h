#pragma once

#include "Core/Audit/FieldAuditTypes.h"

namespace Core::Audit {

Result<FieldIssueLedgerReport> GenerateFieldAuditIssueLedger(const FieldIssueLedgerRequest& request);

} // namespace Core::Audit
