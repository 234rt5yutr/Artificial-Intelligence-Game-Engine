#pragma once

#include "Core/Remediation/FieldGuardrailTypes.h"

namespace Core::Remediation {

Result<FieldGuardrailResult> AddFieldInvariantAssertions(const FieldGuardrailRequest& request);
Result<FieldGuardrailResult> AddFieldContractRegressionSuites(const FieldGuardrailRequest& request);
Result<FieldGuardrailResult> AddFieldAuditGateToBuildPipeline(const FieldGuardrailRequest& request);
Result<FieldGuardrailResult> AddFieldDriftMonitoringAndAlerting(const FieldGuardrailRequest& request);

} // namespace Core::Remediation
