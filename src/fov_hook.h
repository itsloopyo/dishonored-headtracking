// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"
#include "config.h"

#include <cstdint>

namespace DishonoredHeadTracking {

// Renders the scene at a field of view of the player's choosing.
//
// Dishonored has an FOV slider of its own in Options > Graphics, so this exists for
// the range that slider does not reach. It detours APlayerController::GetFOVAngle and
// changes the answer ONLY for the call ULocalPlayer::CalcSceneView makes when it builds
// the projection matrix. Every other reader - weapon zoom logic, camera modifiers,
// script - gets the FOV the game chose, so nothing but the projection moves.
//
// Every address the hook pins to comes from @p profile, resolved against @p moduleBase.
bool InstallFovHook(const BuildProfile& profile, std::uintptr_t moduleBase,
                    const Config& cfg);

// The FOV the scene view is actually rendered with, given the FOV the game asked for
// and the ACamera it came off. Returns @p gameFov unchanged when no override is in
// effect, which includes the case where the hook did not install - so a mod that failed
// to change the FOV never claims to have changed it.
//
// The crosshair projection has to be built from the same number the projection matrix
// was, so the camera hook publishes this rather than what it read off the camera.
float EffectiveFov(float gameFov, const void* camera);

// The field of view @p camera renders at with nothing zooming it: its DefaultFOV, put
// through the same override EffectiveFov applies. This is the baseline a zoom is
// measured against, and it has to move with the override or a configured field of view
// would read as a permanent zoom of its own. Returns 0 for a null camera, which is not
// a usable field of view and so measures no zoom.
float UnzoomedFov(const void* camera);

}  // namespace DishonoredHeadTracking
