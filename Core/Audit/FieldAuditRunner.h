#pragma once

#include "Core/Audit/FieldAuditTypes.h"

namespace Core::Audit {

Result<FieldAuditRunReport> RunRuntimeStateFieldAudit(const FieldAuditRunRequest& request);

} // namespace Core::Audit
