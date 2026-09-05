// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"
#include "config.h"
#include "ue3_math.h"

#include <cstdint>

namespace DishonoredHeadTracking {

// The shallowest approach angle the pull-back is divided by.
//
// The pull-back is measured along the surface normal, so a lean that runs into a wall
// at a glancing angle needs a longer stop distance than one that hits it head on. That
// scaling is 1/cos of the approach angle, which runs away to infinity as the lean turns
// parallel to the surface, so it is floored here at 5x. A hit that glancing is not the
// one about to put the eye through the wall anyway.
constexpr float kMinApproachCos = 0.2f;

// How fast the eye is allowed to travel back out along a lean that was blocked, in world
// units per second, once whatever it stopped against is gone. Blocking is immediate;
// only the release is paced, and the pacing never limits the lean itself - a lean the
// world does not obstruct is applied in full however fast the head moves.
//
// Without it the eye jumps. What the eye is allowed to reach changes smoothly as the
// player moves - except at a silhouette edge, where stepping a centimetre past a
// doorframe takes the lean from blocked to free between two frames.
constexpr float kCollisionReleaseUuPerSecond = 200.0f;

// Longest gap the release is allowed to integrate over. A frame that took longer than
// this was a hitch or a load, and pacing the release across it would let the whole
// recovery happen in one step, which is the jump this exists to prevent.
constexpr float kMaxCollisionDt = 0.1f;

// How far PAST the requested lean the trace has to reach.
//
// This is what the first version of this file got wrong. A ray that stopped at the end
// of the lean could not see a surface just beyond it, so the eye travelled the whole
// lean and came to rest against the wall - or inside it, once the near clip plane was
// closer than the wall. Only when the head pushed far enough for the ray itself to cross
// the surface did the hit appear, and the eye was then pulled back to the margin in one
// frame: the lean went through the wall and then popped back out of it.
//
// The margin is measured along the surface normal, so the furthest a hit can be and
// still constrain the lean is one whole pull-back past its end, and the pull-back is
// largest at the floored approach angle.
inline float TraceOverreach(float margin) {
    return margin / kMinApproachCos;
}

// How far along the lean the eye may travel, given a trace that hit @p hitDistance away
// at @p approachCos (the cosine of the angle between the lean and the surface normal,
// 1 = straight into the wall), holding @p margin off that surface.
//
// A distance, not a fraction of the requested lean, and that is the point: it depends on
// the world and the direction of the lean, never on how hard the head is pushing. Push
// harder into a wall and this does not move, so the eye stops dead at the surface and
// stays there. Pure geometry, so it is testable without a game.
inline float AllowedLeanDistance(float hitDistance, float approachCos, float margin) {
    const float square = approachCos > kMinApproachCos ? approachCos : kMinApproachCos;
    const float allowed = hitDistance - margin / square;
    return allowed > 0.0f ? allowed : 0.0f;
}

// Binds the world trace the lean clamp casts through, and takes the configured margin.
// Reports what it did to the log. A profile that does not carry the trace RVA, or a
// player who turned collision off, leaves the clamp disabled and AllowedLeanFraction
// answering 1 for every lean.
void InitCameraCollision(const BuildProfile& profile, std::uintptr_t moduleBase,
                         const Config& cfg);

// The fraction of the lean (@p dx, @p dy, @p dz, world units, from @p eye) that can be
// applied without putting the rendered eye inside the world.
//
// @p sourceActor is the actor the trace ignores; the controller the scene view asked for
// the viewpoint is what the caller has and is what the game itself passes for its own
// traces. Call once per rendered frame - the release pacing above integrates real time
// between calls.
float AllowedLeanFraction(void* sourceActor, const UE3Vector& eye, float dx, float dy,
                          float dz);

// Forgets the release pacing, for a frame that applies no lean at all: a closed gate, a
// tracker that stopped sending, rotation-only mode. The next lean starts from whatever
// the trace says rather than easing out of a block the player has since left behind.
void ResetCameraCollision();

}  // namespace DishonoredHeadTracking
