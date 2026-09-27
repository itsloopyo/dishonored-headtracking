// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/math/angle_utils.h"

#include <cmath>

namespace DishonoredHeadTracking {

// Boundary validation for floats read from the user-editable INI. The core
// library already finite-checks rotation values arriving over UDP
// (OpenTrackPacket::FiniteFloat); the same guarantee must hold for config
// values, which feed into the identical smoothing/quaternion math. strtod
// parses "nan" and "inf", so a malformed INI (e.g. "LocalSmoothing=nan")
// otherwise poisons the smoothed quaternion and the injected view matrix.

inline float SanitizeFinite(float v, float fallback) {
    return std::isfinite(v) ? v : fallback;
}

// LocalSmoothing and RemoteSmoothing must each be finite and within [0,1].
// [0,1] is the whole meaningful domain: CalculateSmoothingFactor maps it onto a
// settle speed between 50 (a flat 20 ms time constant) and 0.1 (a flat 10 s
// time constant), and the core clamps that speed to [0.1, 50] itself, so a
// value outside the range no longer drives the per-frame factor negative. It
// just saturates at one end while the INI goes on advertising a setting the mod
// is not honouring, so the clamp stays: it keeps the stored value and the
// behaviour in agreement, and gives the caller something to log.
//
// This is validation, never a floor. Any value inside [0,1] reaches the
// processor untouched, 0.0 included. `fallback` is the shipped default of the
// key being read, 0.0 for LocalSmoothing and 0.15 for RemoteSmoothing, so a
// malformed RemoteSmoothing lands on the remote default instead of silently
// handing a phone-over-WiFi user the local "no smoothing at all".
inline float SanitizeSmoothing(float v, float fallback) {
    return cameraunlock::math::Clamp(SanitizeFinite(v, fallback), 0.0f, 1.0f);
}

// A positional limit is a distance, so it has to be finite and above zero. Magnitude is
// NOT checked - a player who wants a wider lean than the shipped 0.30 m is tuning, not
// misconfiguring - but the sign is, because a negative one does not merely widen or
// narrow the range, it inverts the clamp.
//
// PositionProcessor clamps z as Clamp(z, -limit_z, +limit_z_back), and Clamp is
// `v < min ? min : (v > max ? max : v)`. With LimitZ = -0.4 the bounds become
// [0.4, 0.1] - min above max - and every input returns one of those two constants. The
// camera parks at a fixed offset that no longer answers the tracker at all, which reads
// in game as positional tracking having died rather than as a bad value in the INI.
inline float SanitizePositiveLimit(float v, float fallback) {
    const float finite = SanitizeFinite(v, fallback);
    return finite > 0.0f ? finite : fallback;
}

}
