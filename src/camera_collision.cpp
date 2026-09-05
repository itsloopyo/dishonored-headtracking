// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_collision.h"

#include "heap_ptr.h"
#include "logging.h"

#include <windows.h>

#include <cmath>
#include <cfloat>

namespace DishonoredHeadTracking {

namespace {

// UWorld::SingleLineCheck, __thiscall(this = GWorld), seven stack arguments, ret 0x1C.
//
// Read off AActor::execTrace and AActor::execFastTrace, which are the game's own two
// script-facing traces and both call this one function with the same argument order:
//
//   006D1278  mov  ecx, [0x01449888]      ; GWorld
//   006D1282  call 0x0064E7A0             ; args pushed right to left, callee cleans
//
// It returns TRUE when the line is clear. On a hit it copies the nearest FCheckResult
// (0x13 dwords, the rep movsd at 0x0064E83A) into the caller's; on a miss it writes only
// Time = 1.0 and Actor = null (0x0064E874), which is why execTrace and execFastTrace both
// read the hit off Actor rather than the return value, and why this file does too.
//
// The extent argument makes it a box sweep. This mod passes a zero extent - a line - on
// purpose: a box sweep reports an immediate overlap whenever the eye already sits within
// the box's own half-width of a wall, which is routine in first person, and would then
// refuse a lean in ANY direction including away from that wall.
using SingleLineCheck_t = std::int32_t(__thiscall*)(void* world, void* hit, void* sourceActor,
                                                   const float* end, const float* start,
                                                   std::uint32_t traceFlags,
                                                   const float* extent, void* sourceLight);

// TRACE_Movers | TRACE_Level | TRACE_LevelGeometry | TRACE_Terrain, i.e. UE3's
// TRACE_World: the world the camera can be pushed through and nothing else. It is the
// exact value AActor::execTrace builds for a script Trace() with bTraceActors false
// (0x006D120D), so it is the game's own definition rather than a reconstructed one.
// Pawns and other actors are deliberately not in it - a guard walking past should not
// shove the player's view.
constexpr std::uint32_t kTraceWorld = 0x2086u;

// FCheckResult, sized and laid out from the two exec natives above: the copy on a hit is
// 0x4C bytes, execTrace reads its result actor from +0x04 and its HitLocation and
// HitNormal out-params from +0x08 and +0x14, and both natives seed Time at +0x20 with
// the 1.0 at 0x011F1340.
constexpr std::uint32_t kCheckResultSize = 0x4Cu;
constexpr std::uint32_t kHitActor  = 0x04u;
constexpr std::uint32_t kHitNormal = 0x14u;
constexpr std::uint32_t kHitTime   = 0x20u;

// Below this a lean is not worth a trace, and is too short for the direction it is in to
// be meaningful.
constexpr float kMinLeanUu = 0.05f;

SingleLineCheck_t g_singleLineCheck = nullptr;
std::uintptr_t g_gworld = 0;
float g_margin = 0.0f;
// How far the eye is currently allowed along the lean, in world units. FLT_MAX means
// nothing is in the way. Paced upward, dropped instantly - see kCollisionReleaseUuPerSecond.
float g_allowed = FLT_MAX;
LARGE_INTEGER g_lastTick{};

// Seconds since the previous call, clamped. Returns 0 on the first call, which is the
// right answer for it: with no elapsed time the release cannot advance, and the first
// frame's limit comes straight off the trace.
float ElapsedSeconds() {
    static const double kSecondsPerCount = [] {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return 1.0 / static_cast<double>(freq.QuadPart);
    }();

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const LONGLONG previous = g_lastTick.QuadPart;
    g_lastTick = now;
    if (previous == 0 || now.QuadPart <= previous) {
        return 0.0f;
    }
    const float dt = static_cast<float>(static_cast<double>(now.QuadPart - previous) *
                                        kSecondsPerCount);
    return dt > kMaxCollisionDt ? kMaxCollisionDt : dt;
}

// Once, the first time a lean is actually stopped, and with the numbers behind it. "Is
// the collision doing anything, and is it stopping at the right distance" is otherwise
// unanswerable from the log, and it is the first question asked of a camera that still
// clips.
void LogFirstBlock(float leanLen, float hitDistance, float approachCos, float allowed) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("Camera collision engaged: a %.1f unit lean met the world %.1f units out "
              "(approach %.2f) and was held to %.1f", leanLen, hitDistance, approachCos,
              allowed);
}

}  // namespace

void InitCameraCollision(const BuildProfile& profile, std::uintptr_t moduleBase,
                         const Config& cfg) {
    g_singleLineCheck = nullptr;
    g_gworld = 0;
    g_allowed = FLT_MAX;
    g_lastTick.QuadPart = 0;

    if (!cfg.collision_enabled) {
        Log::Line("Camera collision off by config: leaning will push the view through "
                  "walls it gets close enough to");
        return;
    }

    g_margin = cfg.collision_margin;
    g_gworld = moduleBase + profile.rvaGWorld;
    g_singleLineCheck = reinterpret_cast<SingleLineCheck_t>(moduleBase +
                                                            profile.rvaSingleLineCheck);
    Log::Line("Camera collision on: the lean is traced against the world through "
              "UWorld::SingleLineCheck @ 0x%p and stops %.1f units short of what it hits",
              reinterpret_cast<void*>(g_singleLineCheck), g_margin);
}

float AllowedLeanFraction(void* sourceActor, const UE3Vector& eye, float dx, float dy,
                          float dz) {
    if (!g_singleLineCheck) {
        return 1.0f;
    }

    const float leanLen = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (leanLen < kMinLeanUu) {
        ResetCameraCollision();
        return 1.0f;
    }

    // The trace is a plain read of the collision world, but it is still a call into the
    // engine from inside a detour, so the pointer it runs against is validated on every
    // frame rather than cached. A level load replaces the world.
    const std::uint32_t world = *reinterpret_cast<const std::uint32_t*>(g_gworld);
    if (!LooksLikeHeapPtr(world)) {
        return 1.0f;
    }

    // Deliberately longer than the lean. See TraceOverreach: a ray that stopped where
    // the lean stops cannot see the wall the lean is about to come to rest against.
    const float traceLen = leanLen + TraceOverreach(g_margin);
    const float scale = traceLen / leanLen;

    const float start[3] = { eye.X, eye.Y, eye.Z };
    const float end[3]   = { eye.X + dx * scale, eye.Y + dy * scale, eye.Z + dz * scale };
    const float extent[3] = { 0.0f, 0.0f, 0.0f };

    // Aligned because the engine fills it with a rep movsd and this file reads floats
    // and a pointer straight back out of it.
    alignas(16) std::uint8_t hit[kCheckResultSize] = {};
    *reinterpret_cast<float*>(hit + kHitTime) = 1.0f;
    g_singleLineCheck(reinterpret_cast<void*>(world), hit, sourceActor, end, start,
                      kTraceWorld, extent, nullptr);

    float target = FLT_MAX;
    if (*reinterpret_cast<const std::uint32_t*>(hit + kHitActor) != 0) {
        const auto* normal = reinterpret_cast<const float*>(hit + kHitNormal);
        // The normal faces back along the lean, so this is positive for a lean running
        // into the surface. A degenerate normal reads as a glancing hit and takes the
        // floored pull-back, which stops the lean short - the safe direction to fail.
        const float approachCos =
            -(dx * normal[0] + dy * normal[1] + dz * normal[2]) / leanLen;
        const float hitDistance =
            *reinterpret_cast<const float*>(hit + kHitTime) * traceLen;
        target = AllowedLeanDistance(hitDistance, approachCos, g_margin);
        if (target < leanLen) {
            LogFirstBlock(leanLen, hitDistance, approachCos, target);
        }
    }

    const float dt = ElapsedSeconds();
    if (target <= g_allowed) {
        g_allowed = target;
    } else {
        const float paced = g_allowed + kCollisionReleaseUuPerSecond * dt;
        g_allowed = paced < target ? paced : target;
    }

    // A hard stop, not a scaled-back lean: push harder into a wall and the eye does not
    // move, because what is allowed depends on the world rather than on the pose.
    return g_allowed >= leanLen ? 1.0f : g_allowed / leanLen;
}

void ResetCameraCollision() {
    g_allowed = FLT_MAX;
    g_lastTick.QuadPart = 0;
}

}  // namespace DishonoredHeadTracking
