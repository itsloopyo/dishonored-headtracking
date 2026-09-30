// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "lean_trace.h"

#include "runtime_discovery.h"

namespace DishonoredHeadTracking {
namespace lean_trace {

namespace {

// UWorld::SingleLineCheck, __thiscall(this = GWorld), seven stack arguments, ret 0x1C.
//
// The extent is zero: a line. A box sweep reports an immediate overlap whenever the eye
// already sits within its half-width of a wall, which is routine in first person, and
// would then refuse a lean in ANY direction including away from that wall. The volume
// around the eye comes from core's LineSweepQuery instead, built out of these lines.
using SingleLineCheck_t = std::int32_t(__thiscall*)(void* world, void* hit, void* sourceActor,
                                                   const float* end, const float* start,
                                                   std::uint32_t traceFlags,
                                                   const float* extent, void* sourceLight);

SingleLineCheck_t g_check = nullptr;
std::uintptr_t g_gworld = 0;
std::uint32_t g_flags = 0;

}  // namespace

void Init(const BuildProfile& profile, std::uintptr_t moduleBase, std::uint32_t traceFlags) {
    g_check = reinterpret_cast<SingleLineCheck_t>(moduleBase + profile.rvaSingleLineCheck);
    g_gworld = moduleBase + profile.rvaGWorld;
    g_flags = traceFlags;
}

cameraunlock::camera::LineHit Cast(void* context, const cameraunlock::math::Vec3& start,
                                   const cameraunlock::math::Vec3& direction, float length) {
    cameraunlock::camera::LineHit out;

    // A level load replaces the world, so it is read and checked on every cast rather
    // than cached.
    std::uint32_t world = 0;
    if (!ReadLive(g_gworld, world) || !HasLiveClass(reinterpret_cast<const void*>(world), LiveLayout().worldClass) ||
        !HasLiveClass(context, LiveLayout().pcClass)) {
        return out;
    }

    const float from[3] = { start.x, start.y, start.z };
    const float to[3] = { start.x + direction.x * length, start.y + direction.y * length,
                          start.z + direction.z * length };
    const float extent[3] = { 0.0f, 0.0f, 0.0f };

    // Aligned because the engine fills it with a rep movsd and this file reads floats
    // and a pointer straight back out of it.
    alignas(16) std::uint8_t hit[512] = {};
    *reinterpret_cast<float*>(hit + LiveLayout().hitTime) = 1.0f;
    g_check(reinterpret_cast<void*>(world), hit, context, to, from, g_flags, extent, nullptr);

    out.queried = true;
    out.hit = *reinterpret_cast<const std::uint32_t*>(hit + LiveLayout().hitActor) != 0;
    if (out.hit) {
        const auto* normal = reinterpret_cast<const float*>(hit + LiveLayout().hitNormal);
        out.distance = *reinterpret_cast<const float*>(hit + LiveLayout().hitTime) * length;
        out.normal = cameraunlock::math::Vec3(normal[0], normal[1], normal[2]);
    }
    return out;
}

}  // namespace lean_trace
}  // namespace DishonoredHeadTracking
