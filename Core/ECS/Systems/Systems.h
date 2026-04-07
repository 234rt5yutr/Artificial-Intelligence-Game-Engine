#pragma once

// Include all ECS systems for convenience

#include "Core/ECS/ParallelECS.h"
#include "Core/ECS/Systems/RenderSystem.h"
#include "Core/ECS/Systems/SkeletalRenderSystem.h"
#include "Core/ECS/Systems/LightSystem.h"
#include "Core/ECS/Systems/TransformSystem.h"
#include "Core/ECS/Systems/PhysicsSystem.h"
#include "Core/ECS/Systems/PhysicsSyncSystem.h"
#include "Core/ECS/Systems/CameraSystem.h"
#include "Core/ECS/Systems/CharacterControllerSystem.h"
#include "Core/ECS/Systems/PlayerControllerSystem.h"
#include "Core/ECS/Systems/FirstPersonCameraSystem.h"
#include "Core/ECS/Systems/ThirdPersonCameraSystem.h"
#include "Core/ECS/Systems/CameraViewInterpolatorSystem.h"
#include "Core/ECS/Systems/AnimatorSystem.h"
#include "Core/ECS/Systems/IKSystem.h"

// Phase 13: World Building & Procedural Environments
#include "Core/ECS/Systems/TerrainSystem.h"
#include "Core/ECS/Systems/FoliageSystem.h"
#include "Core/ECS/Systems/SkyboxSystem.h"

// Phase 15: Post-Processing
#include "Core/ECS/Systems/PostProcessSystem.h"

// Phase 18: Advanced Gameplay Systems - AI
#include "Core/ECS/Systems/BehaviorTreeSystem.h"
#include "Core/ECS/Systems/FSMSystem.h"
