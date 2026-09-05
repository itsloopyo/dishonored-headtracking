// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <atomic>

namespace DishonoredHeadTracking {

// Where the game's aim direction lands in the head-tracked view.
//
// The camera hook injects head rotation into the scene view only, so the game keeps
// aiming along the rotation the mouse chose while the player looks somewhere else.
// The reticle therefore has to leave screen centre. The hook publishes the clean aim
// direction resolved in the tracked view's own basis; the overlay turns that into a
// screen position with the backbuffer it is actually drawing into.
//
// Written on the game thread inside the scene-view hook and read on the render thread
// in the D3D9 present hook, so the components are atomic. They are deliberately not
// updated as one transaction: a torn read costs one frame of a few pixels on a marker
// that is already moving, and a lock on the render path costs more than that.
struct AimMarker {
    // Clean aim direction expressed in the tracked view basis: right, up, forward.
    std::atomic<float> right{0.0f};
    std::atomic<float> up{0.0f};
    std::atomic<float> forward{1.0f};
    // Horizontal field of view the scene view is rendered with, in degrees. This is the
    // value the projection matrix was built from, so it already carries any FOV override
    // the player configured as well as the game's own zooms.
    std::atomic<float> fov_deg{75.0f};
    // The camera's ConstrainedAspectRatio when it is constraining the view's aspect,
    // 0 when it is not. UE3 then builds the projection from that ratio rather than the
    // viewport's pixel size and centres the view inside the viewport with bars, so both
    // the vertical scale and the pixel the aim point lands on come from the smaller rect.
    std::atomic<float> constrained_aspect{0.0f};
    // The positional lean the hook applied this frame, in the engine's own units
    // (centimetres), along the same right/up/forward basis. The projection does NOT
    // use it: the marker is rotation-only (see PublishAimMarker for why, and for the
    // two conditions on bringing parallax back). It is published so the residual the
    // stand-down leaves - roughly lean divided by target distance - can be read off the
    // same diagnostic line as the direction that produced it, rather than inferred.
    std::atomic<float> lean_right{0.0f};
    std::atomic<float> lean_up{0.0f};
    std::atomic<float> lean_forward{0.0f};
    // False when tracking is not being injected this frame, so the overlay draws
    // nothing and the game's own centred crosshair is the only marker on screen.
    std::atomic<bool> active{false};
};

AimMarker& GetAimMarker();

// One reader's view of the marker, with every field loaded exactly once.
//
// The consumer has to project, write and then report the SAME numbers: reading an
// atomic twice can hand the diagnostic line a different value from the one that placed
// the crosshair, which is how a fix gets shipped against a fault that was never there.
struct AimMarkerSample {
    float right, up, forward;
    float fov_deg;
    float constrained_aspect;
    float lean_right, lean_up, lean_forward;
};

inline AimMarkerSample SampleAimMarker(const AimMarker& marker) {
    AimMarkerSample s;
    s.right = marker.right.load(std::memory_order_relaxed);
    s.up = marker.up.load(std::memory_order_relaxed);
    s.forward = marker.forward.load(std::memory_order_relaxed);
    s.fov_deg = marker.fov_deg.load(std::memory_order_relaxed);
    s.constrained_aspect = marker.constrained_aspect.load(std::memory_order_relaxed);
    s.lean_right = marker.lean_right.load(std::memory_order_relaxed);
    s.lean_up = marker.lean_up.load(std::memory_order_relaxed);
    s.lean_forward = marker.lean_forward.load(std::memory_order_relaxed);
    return s;
}

}  // namespace DishonoredHeadTracking
