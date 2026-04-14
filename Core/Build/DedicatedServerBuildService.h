#pragma once

#include "Core/Build/BuildPipelineTypes.h"

namespace Core::Build {

Result<DedicatedServerBuildResult> GenerateDedicatedServerBuildArtifacts(const DedicatedServerBuildRequest& request);

} // namespace Core::Build
