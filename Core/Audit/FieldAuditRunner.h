#pragma once

#include "Core/Audit/FieldAuditTypes.h"

namespace Core::Audit {

Result<FieldAuditRunReport> RunRuntimeStateFieldAudit(const FieldAuditRunRequest& request);
Result<FieldAuditRunReport> RunCookedAndPackagedArtifactFieldAudit(const FieldAuditRunRequest& request);
Result<FieldAuditRunReport> RunNetworkAndReplayFieldAudit(const FieldAuditRunRequest& request);

} // namespace Core::Audit
