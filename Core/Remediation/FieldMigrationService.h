#pragma once

#include "Core/Remediation/FieldMigrationTypes.h"

namespace Core::Remediation {

Result<FieldMigrationResult> MigrateSceneAndPrefabFieldData(const FieldMigrationRequest& request);
Result<FieldMigrationResult> MigrateUIAndLocalizationFieldData(const FieldMigrationRequest& request);
Result<FieldMigrationResult> MigrateAddressableBundleAndBuildManifestFieldData(const FieldMigrationRequest& request);

} // namespace Core::Remediation
