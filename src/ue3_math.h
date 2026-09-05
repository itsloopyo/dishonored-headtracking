// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/math/angle_utils.h"

#include <cmath>
#include <cstdint>

namespace DishonoredHeadTracking {

// UE3 POD camera types (32-bit engine: FVector is 3 floats, FRotator is 3 int32
// where 65536 units = 360 degrees). Do NOT use the core ue_math.h FVector/FRotator
// here - those are UE5 doubles and would mis-decode this engine's out-params.
struct UE3Vector {
    float X, Y, Z;
};
struct UE3Rotator {
    std::int32_t Pitch, Yaw, Roll;
};

constexpr float kUnitsToRad = static_cast<float>(2.0 * cameraunlock::math::kPi / 65536.0);
constexpr float kDegToRad   = static_cast<float>(cameraunlock::math::kPi / 180.0);

// UE3 keeps a view pitch inside a quarter turn. Past it cos(pitch) goes negative, which
// mirrors the forward axis's horizontal part and inverts the up axis, so the view snaps
// upside-down and backwards. The engine does not renormalise what the camera hook hands
// back, so the sum has to be clamped.
constexpr std::int32_t kMaxPitchUnits = 16384;

// An FRotator field reduced to the signed half-turn range [-32768, 32767].
//
// A rotator is modular, so the engine is free to hand back either representation and
// UE3 produces both: FMatrix::Rotator() returns signed, while the positive-wrapped form
// (a 30 degree downward look as 60000 rather than -5536) comes out of the view-limiting
// path. Every other use the mod makes of the rotator is periodic and cannot tell them
// apart - RotatorToMatrix is trig, MatrixToRotator re-derives a signed value - but the
// pitch clamp compares against a bound, and against the positive-wrapped form it would
// read every downward look as far past the limit and pin the view at the sky.
//
// Writing the normalised value back is safe for the same reason: the out-param feeds
// FRotationMatrix, which is modular.
inline std::int32_t NormalizeUnits(std::int32_t units) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(units));
}

// Every float that reaches an FRotator or an FVector out-param has to be finite.
// DegToUnits truncates to int32 and the position offset is added straight into the
// camera location, so one non-finite component parks the view at a coordinate the
// renderer cannot resolve: a black screen, with nothing in the log to say why. The
// pose is checked once, where it leaves the tracking pipeline.
inline bool AllFinite(float a, float b, float c) {
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(c);
}

// @p deg must be finite (see AllFinite).
//
// A rotator is modular, so reducing by whole turns is exact and only ever applied to a
// value that would otherwise overflow the conversion. That matters because an INI
// sensitivity is finite-checked but deliberately NOT magnitude-checked - a large one is
// legitimate tuning - and a big enough product puts lround's result outside int32,
// where the narrowing conversion is undefined rather than merely wrong. Below the
// threshold nothing is reduced, so a full turn still converts to a full turn's units.
inline std::int32_t DegToUnits(float deg) {
    constexpr float kMaxDirectDegrees = 32768.0f;
    const float reduced = (deg > -kMaxDirectDegrees && deg < kMaxDirectDegrees)
                              ? deg
                              : std::fmod(deg, 360.0f);
    return static_cast<std::int32_t>(std::lround(reduced * 65536.0f / 360.0f));
}

inline std::int32_t RadToUnits(float rad) {
    return static_cast<std::int32_t>(std::lround(rad * 32768.0 / cameraunlock::math::kPi));
}

inline std::int32_t ClampPitchUnits(std::int32_t pitch) {
    return cameraunlock::math::Clamp(pitch, -kMaxPitchUnits, kMaxPitchUnits);
}

// UE3 FRotationMatrix: row 0 is the forward (X) axis, row 1 right (Y), row 2 up (Z),
// composed as M(P,Y,R) = M(P,0,R) * M(0,Y,0). Yaw being the outermost rotation about
// world Z is why plain FRotator addition gives horizon-locked yaw, and why camera-local
// yaw needs this matrix path.
struct Mat3 {
    float m[3][3];
};

inline Mat3 RotatorToMatrix(float pitchRad, float yawRad, float rollRad) {
    const float sp = std::sin(pitchRad), cp = std::cos(pitchRad);
    const float sy = std::sin(yawRad),   cy = std::cos(yawRad);
    const float sr = std::sin(rollRad),  cr = std::cos(rollRad);
    Mat3 M;
    M.m[0][0] = cp * cy;
    M.m[0][1] = cp * sy;
    M.m[0][2] = sp;
    M.m[1][0] = sr * sp * cy - cr * sy;
    M.m[1][1] = sr * sp * sy + cr * cy;
    M.m[1][2] = -sr * cp;
    M.m[2][0] = -(cr * sp * cy + sr * sy);
    M.m[2][1] = sr * cy - cr * sp * sy;
    M.m[2][2] = cr * cp;
    return M;
}

inline Mat3 RotatorToMatrix(const UE3Rotator& r) {
    return RotatorToMatrix(static_cast<float>(r.Pitch) * kUnitsToRad,
                           static_cast<float>(r.Yaw) * kUnitsToRad,
                           static_cast<float>(r.Roll) * kUnitsToRad);
}

// The forward (X) axis on its own. It is row 0 of RotatorToMatrix, which carries no
// roll term at all, so a caller that only needs "where is this rotator pointing" pays
// four trig calls instead of six and skips building the two rows it would discard.
inline void RotatorForward(const UE3Rotator& r, float out[3]) {
    const float pitchRad = static_cast<float>(r.Pitch) * kUnitsToRad;
    const float yawRad   = static_cast<float>(r.Yaw) * kUnitsToRad;
    const float cp = std::cos(pitchRad);
    out[0] = cp * std::cos(yawRad);
    out[1] = cp * std::sin(yawRad);
    out[2] = std::sin(pitchRad);
}

inline Mat3 MatMul(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }
    return r;
}

// How much of @p headPitchDeg the view has room for, in FRotator units, given the
// engine's current pitch.
//
// Past a quarter turn cos(pitch) goes negative, which mirrors the forward axis and
// inverts the up axis: the view snaps upside-down and backwards. Bounding the head's
// CONTRIBUTION rather than the final sum is what lets both yaw modes share one limit -
// the camera-local branch composes rather than adds, and clamping its result after the
// fact cannot undo a flip that has already thrown yaw and roll half a turn out.
inline std::int32_t BoundedPitchContribution(std::int32_t cleanPitch, float headPitchDeg) {
    const std::int32_t normalized = NormalizeUnits(cleanPitch);
    return ClampPitchUnits(normalized + DegToUnits(headPitchDeg)) - normalized;
}

// Where the CLEAN aim direction lands in the TRACKED view, as components along that
// view's own right/up/forward axes. This is the whole of the reticle projection's
// geometry, and it is deliberately resolved basis-to-basis rather than through an Euler
// formula: the tracked rotator is the same one handed to the engine, so the crosshair
// cannot drift out of agreement with the render however the rotation was composed.
//
// Only the clean FORWARD axis is needed, and row 0 carries no roll term, so the clean
// roll never reaches the result.
inline void ResolveAimInTrackedView(const UE3Rotator& clean, const UE3Rotator& tracked,
                                    float outRuf[3]) {
    float f[3];
    RotatorForward(clean, f);
    const Mat3 t = RotatorToMatrix(tracked);
    outRuf[0] = f[0] * t.m[1][0] + f[1] * t.m[1][1] + f[2] * t.m[1][2];
    outRuf[1] = f[0] * t.m[2][0] + f[1] * t.m[2][1] + f[2] * t.m[2][2];
    outRuf[2] = f[0] * t.m[0][0] + f[1] * t.m[0][1] + f[2] * t.m[0][2];
}

// UE3 FMatrix::Rotator(): pitch/yaw from the forward axis, roll from the
// right/up axes projected onto the roll-free right axis.
inline void MatrixToRotator(const Mat3& M, UE3Rotator* out) {
    const float fx = M.m[0][0], fy = M.m[0][1], fz = M.m[0][2];
    const float pitch = std::atan2(fz, std::sqrt(fx * fx + fy * fy));
    const float yaw   = std::atan2(fy, fx);
    const float syx = -std::sin(yaw), syy = std::cos(yaw);
    const float roll  = std::atan2(M.m[2][0] * syx + M.m[2][1] * syy,
                                   M.m[1][0] * syx + M.m[1][1] * syy);
    out->Pitch = RadToUnits(pitch);
    out->Yaw   = RadToUnits(yaw);
    out->Roll  = RadToUnits(roll);
}

// Adds a tracker-convention head pose to the game's viewpoint rotator.
//
// This is the engine boundary for rotation, and the whole of it: the roll sign, the
// quarter-turn pitch bound, and the two yaw modes. It lives here rather than inside the
// detour so it can be exercised without a running game - the bug it exists to prevent
// was at the call site, not in the helpers it calls.
//
// Roll: the tracker and UE3 agree on the sign here - a tracker roll reaches the engine
// unchanged. The first build negated it, on the fleet's usual assumption that yaw and
// roll arrive mirrored, and in game the view tilted the wrong way.
//
// Yaw: horizon-locked yaw is plain FRotator addition, because UE3 composes yaw outermost
// about world Z. Camera-local yaw needs the matrix path to rotate about the view's own
// axes.
inline void ComposeHeadRotation(const UE3Rotator& clean, float pitchDeg, float yawDeg,
                                float rollDeg, bool worldSpaceYaw, UE3Rotator* rot) {
    const std::int32_t cleanPitch = NormalizeUnits(clean.Pitch);
    const std::int32_t pitchUnits = BoundedPitchContribution(cleanPitch, pitchDeg);

    if (worldSpaceYaw) {
        rot->Yaw  += DegToUnits(yawDeg);
        rot->Roll += DegToUnits(rollDeg);
        rot->Pitch = cleanPitch + pitchUnits;
        return;
    }
    const Mat3 cleanM = RotatorToMatrix(clean);
    const Mat3 head = RotatorToMatrix(static_cast<float>(pitchUnits) * kUnitsToRad,
                                      yawDeg * kDegToRad, rollDeg * kDegToRad);
    MatrixToRotator(MatMul(head, cleanM), rot);
}

}  // namespace DishonoredHeadTracking
