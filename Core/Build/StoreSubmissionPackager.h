#pragma once

#include "Core/Build/BuildPipelineTypes.h"

namespace Core::Build {

Result<StoreSubmissionResult> PackageStoreSubmissionArtifacts(const StoreSubmissionRequest& request);

} // namespace Core::Build
