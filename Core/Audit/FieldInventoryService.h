#pragma once

#include "Core/Audit/FieldAuditTypes.h"

namespace Core::Audit {

Result<FieldInventorySnapshot> GenerateRuntimeFieldInventory(const FieldInventoryRequest& request);
Result<FieldInventorySnapshot> GenerateSerializedFieldInventory(const FieldInventoryRequest& request);

} // namespace Core::Audit
