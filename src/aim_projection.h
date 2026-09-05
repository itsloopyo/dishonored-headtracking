// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "fov_range.h"

#include "cameraunlock/math/angle_utils.h"

#include <cmath>

namespace DishonoredHeadTracking {

// Turning the aim direction the camera hook published into the viewport pixel the
// crosshair belongs on. Pure arithmetic, deliberately free of the HUD it is read out
// of and written back into, so the projection can be exercised without a running game.

// The aim has swung more than 84 degrees off the rendered view. The guard is on the
// forward component APPROACHING zero rather than merely going negative: the projection
// diverges either side of it.
constexpr float kMinForwardComponent = 0.1f;

// Keep the crosshair fully on screen when the aim leaves the frame, which a routine head
// turn does. Pinned to the edge it still reads as "the gun points that way", which is the
// honest thing to say; letting it fly off leaves the player with no crosshair at all.
constexpr float kEdgeInsetFraction = 0.015f;

// A viewport this far outside the range any display reports is a mis-read of the HUD
// rather than a resolution.
constexpr float kMinViewportPixels = 64.0f;
constexpr float kMaxViewportPixels = 16384.0f;

inline bool IsUsableViewport(float viewportW, float viewportH) {
    return std::isfinite(viewportW) && std::isfinite(viewportH) &&
           viewportW >= kMinViewportPixels && viewportH >= kMinViewportPixels &&
           viewportW <= kMaxViewportPixels && viewportH <= kMaxViewportPixels;
}

// The rectangle of the viewport the scene view is rendered into, in viewport pixels.
struct ViewRect {
    float x, y, w, h;
};

// Normally the whole viewport, and UE3 builds the projection from its pixel size. When
// the camera constrains the aspect ratio the projection comes from that ratio instead,
// and FViewport::CalculateViewExtents shrinks the view to it about the viewport's
// centre: a wider constrained ratio takes height, a narrower one takes width.
//
// @p constrainedAspect is the camera's ConstrainedAspectRatio, or 0 when it is not
// constraining the view. @p viewportW / @p viewportH must have passed IsUsableViewport.
//
// Anything outside the range a real constrained view uses is treated as "not
// constraining" rather than believed. The field is read straight off the camera on the
// frame a bit flips, so a half-written or mis-decoded value is reachable, and believing
// one collapses the rect to a sliver: a ratio of 100 on a 1920-wide viewport gives a
// 19-pixel-tall rect, and the crosshair is then pinned inside a band across the middle
// of the screen with the vertical projection saturated.
constexpr float kMinConstrainedAspect = 0.3f;
constexpr float kMaxConstrainedAspect = 4.0f;

inline ViewRect ComputeViewRect(float viewportW, float viewportH, float constrainedAspect) {
    ViewRect rect = { 0.0f, 0.0f, viewportW, viewportH };
    if (std::isfinite(constrainedAspect) && constrainedAspect >= kMinConstrainedAspect &&
        constrainedAspect <= kMaxConstrainedAspect) {
        if (constrainedAspect > viewportW / viewportH) {
            rect.h = viewportW / constrainedAspect;
            rect.y = (viewportH - rect.h) * 0.5f;
        } else {
            rect.w = viewportH * constrainedAspect;
            rect.x = (viewportW - rect.w) * 0.5f;
        }
    }
    return rect;
}

struct AimPixel {
    float x, y;
    float ndc_x, ndc_y;
    // True when the aim left the view rect and the position was pinned to its edge.
    bool clamped;
};

// Projects the aim direction - expressed in the tracked view's own right/up/forward
// basis - onto @p rect, and pins the result inside the rect's inset edge.
//
// UE3 projects with FPerspectiveMatrix(HalfFOV, Width, Height, NearZ), whose x scale is
// 1/tan(HalfFOV) and y scale that over Height/Width. HalfFOV is half the HORIZONTAL
// field of view, and (Width, Height) is the view rect - or the constrained ratio
// against 1, which is the same shape.
//
// Returns false, leaving @p out untouched, only when the aim cannot be placed at all: a
// field of view outside the usable range, a non-finite result, or an aim pointing so far
// behind the view that there is no on-screen direction to pin it to.
//
// An aim merely swung off the FRAME is placed on the edge, not refused. Refusing it is
// not neutral: the HUD recomputes the crosshair target as viewport centre every frame
// and eases toward it, so declining to write the position walks the crosshair back to
// the middle of the screen, where it asserts the shot lands dead ahead while it lands up
// to 180 degrees away. Pinned to the edge it still reads as "the gun points that way",
// which is the honest thing to say.
inline bool ProjectAimToPixels(const ViewRect& rect, float fovDegrees,
                               float right, float up, float forward, AimPixel* out) {
    if (!IsUsableFov(fovDegrees) || !std::isfinite(right) || !std::isfinite(up) ||
        !std::isfinite(forward)) {
        return false;
    }

    // The inset is a fraction of the SHORTER side, and never more than can fit twice
    // inside either one. Taking it from the height alone inverted the horizontal bounds
    // on a rect narrower than two insets and placed the crosshair outside the rect.
    const float shortSide = rect.w < rect.h ? rect.w : rect.h;
    const float inset = shortSide * kEdgeInsetFraction;
    const float loX = rect.x + inset, hiX = rect.x + rect.w - inset;
    const float loY = rect.y + inset, hiY = rect.y + rect.h - inset;

    constexpr float kPi = static_cast<float>(cameraunlock::math::kPi);
    const float tanH = std::tan(fovDegrees * 0.5f * kPi / 180.0f);
    const float tanV = tanH * rect.h / rect.w;

    float ndcX, ndcY;
    if (forward > kMinForwardComponent) {
        ndcX = (right / forward) / tanH;
        ndcY = (up / forward) / tanV;
    } else {
        // Off the frame, or behind it. The perspective divide diverges as the forward
        // component approaches zero and mirrors the answer once it goes negative, so
        // clamp the DIVISOR rather than the result: this is the same expression as
        // above, evaluated at the guard, which makes the pinned position continuous
        // with the on-frame branch and keeps it aspect-correct.
        //
        // Normalising (right, up) instead looks equivalent and is not: it drops tanH
        // and tanV, so the crosshair jumped several hundred pixels along the edge as
        // the aim crossed the guard, and landed on the wrong edge point for any aim
        // whose horizontal and vertical parts differ.
        if (!(right != 0.0f || up != 0.0f)) {
            return false;  // exactly behind: no direction to point at
        }
        ndcX = (right / kMinForwardComponent) / tanH;
        ndcY = (up / kMinForwardComponent) / tanV;
    }
    if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) {
        return false;
    }

    float x = rect.x + (0.5f + 0.5f * ndcX) * rect.w;
    float y = rect.y + (0.5f - 0.5f * ndcY) * rect.h;
    const bool clamped = x < loX || x > hiX || y < loY || y > hiY;
    if (x < loX) x = loX;
    if (x > hiX) x = hiX;
    if (y < loY) y = loY;
    if (y > hiY) y = hiY;

    out->x = x;
    out->y = y;
    out->ndc_x = ndcX;
    out->ndc_y = ndcY;
    out->clamped = clamped;
    return true;
}

}  // namespace DishonoredHeadTracking
