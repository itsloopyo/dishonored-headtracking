// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"

#include "cameraunlock/camera/lean_line_sweep.h"

#include <cstdint>

namespace DishonoredHeadTracking {
namespace lean_trace {

// Binds UWorld::SingleLineCheck and GWorld from @p profile, with the mask the casts run
// (CollisionChannel).
void Init(const BuildProfile& profile, std::uintptr_t moduleBase, std::uint32_t traceFlags);

// One zero-extent line through UWorld::SingleLineCheck, in the shape core's
// LineSweepQuery builds its sphere out of. `context` is the APlayerController the frame's
// viewpoint came from, handed to the check as the actor to ignore. Distances are in the
// engine's centimetres. queried=false when GWorld cannot be read this frame.
cameraunlock::camera::LineHit Cast(void* context, const cameraunlock::math::Vec3& start,
                                   const cameraunlock::math::Vec3& direction, float length);

}  // namespace lean_trace
}  // namespace DishonoredHeadTracking
