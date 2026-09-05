// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "fov_range.h"

#include "cameraunlock/math/angle_utils.h"

#include <cmath>

namespace DishonoredHeadTracking {

// How much of the tracked head movement to apply, given the field of view the frame is
// being rendered at (@p renderedFov) and the field of view the same camera renders at
// when nothing is zooming it (@p unzoomedFov).
//
// A zoom does not change the angle a head turn asks for, it changes what that angle is
// worth on screen: an offset lands at tan(angle) / tan(fov/2) of the way to the edge of
// the frame, so a camera at half the field of view answers the same head turn with
// roughly twice the movement. Dishonored zooms for scripted scenes, which the player is
// still free to look around inside, and the view there tracks the head far harder than
// it does in ordinary play. The tangent ratio is exactly the factor that cancels, so a
// head turn is worth the same distance on screen at every zoom level.
//
// It only ever scales DOWN. Keeping parity through a field of view WIDER than the
// unzoomed one would mean turning the view further than the head turned, and this mod
// does not amplify head tracking.
inline float ZoomCompensation(float renderedFov, float unzoomedFov) {
    if (!IsUsableFov(renderedFov) || !IsUsableFov(unzoomedFov) ||
        renderedFov >= unzoomedFov) {
        return 1.0f;
    }
    constexpr float kHalfDegToRad = static_cast<float>(cameraunlock::math::kPi / 360.0);
    return std::tan(renderedFov * kHalfDegToRad) / std::tan(unzoomedFov * kHalfDegToRad);
}

}  // namespace DishonoredHeadTracking
