// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"
#include "config.h"
#include "ue3_math.h"

#include <cstdint>

namespace DishonoredHeadTracking {

// Keeps the lean from putting the rendered eye inside the world. Core's LeanClamp owns the
// policy and core's LineSweepQuery turns lean_trace's line casts into a sphere swept along
// the lean, so a door frame's edge or a table corner beside the eye's path stops it as
// surely as a wall straight ahead.
//
// Reports what it did to the log. With CollisionEnabled off the lean passes through
// unclamped.
void InitCameraCollision(const BuildProfile& profile, std::uintptr_t moduleBase,
                         const Config& cfg);

// The part of @p lean (world units, from the clean eye @p eye) the world leaves room for,
// along the same direction. @p controller is the APlayerController the scene view asked,
// and is the actor the casts ignore. Call once per rendered frame that applies a lean: the
// release is damped against real time between calls.
UE3Vector ClampLean(void* controller, const UE3Vector& eye, const UE3Vector& lean);

// Forgets the allowance, for a frame that applies no lean at all: a closed gate, a
// tracker that stopped sending, rotation-only mode. Without it the next lean eases out of
// a wall the player has since left behind.
void ResetCameraCollision();

}  // namespace DishonoredHeadTracking
