#pragma once

#include "Core/Remediation/FieldSchemaPatchTypes.h"

namespace Core::Remediation {

Result<FieldSchemaPatchResult> PatchFieldSchemaDefinitions(const FieldSchemaPatchRequest& request);

} // namespace Core::Remediation
