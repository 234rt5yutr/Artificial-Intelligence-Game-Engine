#pragma once

#include "Core/Automation/AutomationTypes.h"

namespace Core::Automation {

Result<PerformanceSuiteResult> RunAutomatedPerformanceTests(const PerformanceSuiteRequest& request);

} // namespace Core::Automation
