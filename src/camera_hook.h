// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"
#include "config.h"
#include "tracking_runtime.h"

#include <cstdint>

namespace DishonoredHeadTracking {

// Detours APlayerController::GetPlayerViewPoint and injects head rotation and the
// 6DOF position offset into the out-params, but ONLY when the caller is
// ULocalPlayer::CalcSceneView. Weapon fire, interaction traces, AI vision and audio call
// the same function from their own sites and keep the rotation the mouse chose, which is
// what decouples look from aim.
//
// Every address the hook pins to comes from @p profile, resolved against @p moduleBase.
bool InstallCameraHook(const BuildProfile& profile, std::uintptr_t moduleBase,
                       TrackingRuntime& tracking, const Config& cfg);

}  // namespace DishonoredHeadTracking
