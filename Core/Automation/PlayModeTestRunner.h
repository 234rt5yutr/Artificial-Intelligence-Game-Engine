#pragma once

#include "Core/Automation/AutomationTypes.h"

namespace Core::Automation {

Result<PlayModeSuiteResult> RunAutomatedPlayModeTests(const PlayModeSuiteRequest& request);

} // namespace Core::Automation
