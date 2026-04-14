#pragma once

#include "Core/Build/BuildPipelineTypes.h"

namespace Core::Build {

Result<PlatformBuildResult> BuildForPlatformTarget(const PlatformBuildRequest& request);

} // namespace Core::Build
