#pragma once

#include "Core/Audit/FieldAuditTypes.h"

namespace Core::Audit {

Result<FieldRemediationBacklogReport> CreateFieldRemediationBacklogFromAudit(
    const FieldRemediationBacklogRequest& request);

} // namespace Core::Audit
