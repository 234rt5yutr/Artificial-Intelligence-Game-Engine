#pragma once

#include "Core/Diagnostics/ProfilerCaptureTypes.h"

namespace Core::Diagnostics {

Result<TraceExportResult> ExportProfilerTrace(const TraceExportRequest& request);

} // namespace Core::Diagnostics
