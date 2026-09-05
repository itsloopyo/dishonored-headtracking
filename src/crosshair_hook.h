// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"

#include <cstdint>

namespace DishonoredHeadTracking {

// Moves the game's own crosshair to where the game is aiming.
//
// Head tracking turns the rendered view away from the aim, so the crosshair the HUD
// draws at screen centre stops marking the shot. Rather than drawing a second marker
// over the top, this writes the aim point into the two floats the HUD already uses to
// position `_root._dot_mc` - the Scaleform clip that carries the crosshair art - so the
// player still sees exactly one crosshair, the game's own, in the right place.
//
// The HUD's per-frame crosshair update comes from @p profile, resolved against
// @p moduleBase.
bool InstallCrosshairHook(const BuildProfile& profile, std::uintptr_t moduleBase);

}  // namespace DishonoredHeadTracking
