// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>

namespace DishonoredHeadTracking {

// The range a field of view stays usable over. Outside it the projection is not
// something the player can see through - at 0 it degenerates and the screen goes
// black - and a value this far out read off the camera is a mis-read rather than a
// choice.
//
// One definition for the four places that need it: the INI validates the configured
// override against it, the FOV hook clamps the overridden result into it, the camera
// gate refuses to inject when the camera reports a FOV outside it, and the crosshair
// projection refuses to place a crosshair from one.
constexpr float kMinFovDegrees = 20.0f;
constexpr float kMaxFovDegrees = 170.0f;

inline bool IsUsableFov(float fovDegrees) {
    return std::isfinite(fovDegrees) &&
           fovDegrees >= kMinFovDegrees && fovDegrees <= kMaxFovDegrees;
}

// The field of view the scene should render at, given what the game asked for
// (@p gameFov), the camera's own unzoomed DefaultFOV (@p defaultFov) and the player's
// configured override (@p configuredFov, 0 for "leave it alone").
//
// The override is an OFFSET, not a replacement. Dishonored moves the FOV for weapon
// zoom, sprinting and powers by writing the POV cache and blends back to DefaultFOV
// afterwards; adding a constant shifts all of that together, so a zoom still removes the
// same number of degrees it always did. Replacing the FOV outright would flatten every
// one of those effects to a single value.
//
// The result is clamped into the range above, which is also the range the crosshair
// projection accepts - so an override can never render the scene at a FOV the crosshair
// cannot then be placed from.
inline float ApplyFovOffset(float gameFov, float defaultFov, float configuredFov) {
    if (!(configuredFov > 0.0f)) {
        return gameFov;
    }
    if (!std::isfinite(gameFov) || !IsUsableFov(defaultFov)) {
        return gameFov;
    }
    const float shifted = gameFov + (configuredFov - defaultFov);
    return shifted < kMinFovDegrees ? kMinFovDegrees
                                    : (shifted > kMaxFovDegrees ? kMaxFovDegrees : shifted);
}

}  // namespace DishonoredHeadTracking
