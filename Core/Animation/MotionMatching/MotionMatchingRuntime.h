#pragma once

#include "Core/Animation/AnimationRuntimeTypes.h"
#include "Core/Animation/MotionMatching/MotionMatchingDatabase.h"

namespace Core::Animation {

void RegisterMotionMatchingDatabase(
    const MotionMatchingDatabaseAsset& database);

void ClearMotionMatchingDatabases();

MotionMatchingResult EvaluateMotionMatchingDatabase(
    const MotionMatchingQuery& query);

AnimationRuntimeDiagnosticsState GetMotionMatchingDiagnosticsState(
    const std::string& databaseId);

} // namespace Core::Animation

