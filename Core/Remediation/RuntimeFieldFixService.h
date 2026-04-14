#pragma once

#include "Core/Remediation/RuntimeFieldFixTypes.h"

namespace Core::Remediation {

Result<RuntimeFieldFixResult> FixRuntimeFieldBindingAndReflectionRoutes(const RuntimeFieldFixRequest& request);
Result<RuntimeFieldFixResult> FixReplicationRPCAndRollbackFieldParity(const RuntimeFieldFixRequest& request);
Result<RuntimeFieldFixResult> FixFieldUpdateOrderingForDeterminism(const RuntimeFieldFixRequest& request);

} // namespace Core::Remediation
