#pragma once

#include "Core/Audit/FieldAuditTypes.h"

namespace Core::Audit {

Result<FieldAuditComplianceReport> ExportFieldAuditComplianceReport(const FieldAuditComplianceReportRequest& request);

} // namespace Core::Audit
