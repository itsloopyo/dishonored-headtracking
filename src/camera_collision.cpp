// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_collision.h"

#include "lean_trace.h"
#include "logging.h"

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/camera/lean_line_sweep.h"

#include <windows.h>

#include <cmath>

namespace DishonoredHeadTracking {

namespace {

using cameraunlock::math::Vec3;

// Longest gap the release is allowed to integrate over. A frame that took longer was a
// hitch or a load, and easing across it would let the whole release happen in one step.
constexpr float kMaxCollisionDt = 0.1f;

// How far the clean eye may move between two frames before it is a camera cut rather
// than the player walking: a Blink, a level change, a scripted camera taking over. The
// allowance belongs to the room it was measured in, so a cut drops it.
constexpr float kCameraCutUu = 100.0f;

// A change of state is logged at most this often, so a player stepping past a doorframe
// cannot write a line per frame, and a state that has not changed is still sampled at the
// slower interval so a clamp that stopped running cannot hide behind a quiet log.
constexpr ULONGLONG kMinReportIntervalMs = 1000;
constexpr ULONGLONG kSampleIntervalMs = 30000;

// Touched only from the scene-view detour on the game thread: one camera, one allowance.
cameraunlock::camera::LeanClamp g_clamp;
cameraunlock::camera::LineSweep g_sweep;
// Null is the feature switched off, which LeanClamp passes straight through.
cameraunlock::camera::LeanQueryFn g_query = nullptr;

LARGE_INTEGER g_lastTick{};
bool g_hasLastEye = false;
UE3Vector g_lastEye{};

// Seconds since the previous call, clamped. 0 on the first call after a reset, which is
// the right answer: with no elapsed time the release cannot advance.
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

enum class LeanState { Clear, Contact, QueryFailed };

const char* StateText(LeanState s) {
    switch (s) {
        case LeanState::Clear:       return "the world leaves room for the whole lean";
        case LeanState::Contact:     return "the lean is held short of a surface";
        case LeanState::QueryFailed: return "WARN: the world check could not run, so the lean "
                                            "is not being held off anything";
    }
    return "";
}

// One line per report, carrying the numbers a reader needs to tell a bad margin from a
// bad trace and to see what the sweep costs: the lean asked for and allowed on this frame,
// and the average sweep time since the last line.
void Report(LeanState state, float desired, float allowed, double sweepUs) {
    static bool s_seen = false;
    static LeanState s_logged = LeanState::Clear;
    static ULONGLONG s_lastMs = 0;
    static double s_totalUs = 0.0;
    static unsigned s_frames = 0;
    static unsigned s_changes = 0;
    static LeanState s_previous = LeanState::Clear;

    s_totalUs += sweepUs;
    ++s_frames;
    if (s_seen && state != s_previous) ++s_changes;
    s_previous = state;

    const ULONGLONG ms = GetTickCount64();
    const ULONGLONG since = ms - s_lastMs;
    if (s_seen && !(state != s_logged && since >= kMinReportIntervalMs) &&
        since < kSampleIntervalMs) {
        return;
    }
    Log::Line("Lean collision: %s (lean %.1f of %.1f cm; %u state changes, sweep %.0f us "
              "average over %u frames since the last line)",
              StateText(state), allowed, desired, s_changes, s_totalUs / s_frames, s_frames);
    s_seen = true;
    s_logged = state;
    s_lastMs = ms;
    s_totalUs = 0.0;
    s_frames = 0;
    s_changes = 0;
}

double MicrosecondsBetween(const LARGE_INTEGER& from, const LARGE_INTEGER& to) {
    static const double kUsPerCount = [] {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return 1e6 / static_cast<double>(freq.QuadPart);
    }();
    return static_cast<double>(to.QuadPart - from.QuadPart) * kUsPerCount;
}

}  // namespace

void InitCameraCollision(const BuildProfile& profile, std::uintptr_t moduleBase,
                         const Config& cfg) {
    ResetCameraCollision();
    if (!cfg.collision_enabled) {
        g_query = nullptr;
        Log::Line("Camera collision off by config: leaning will push the view through "
                  "walls it gets close enough to");
        return;
    }

    const auto flags = static_cast<std::uint32_t>(cfg.collision_channel);
    lean_trace::Init(profile, moduleBase, flags);
    g_clamp.SetSettings(cfg.lean_clamp);
    // The sweep's radius and the clamp's skin are one number: the clamp holds the eye
    // skin back from what the sweep reports, and the sweep adds its radius back on.
    g_sweep.cast = &lean_trace::Cast;
    g_sweep.settings.radius = cfg.lean_clamp.skin;
    g_query = &cameraunlock::camera::LineSweepQuery;
    Log::Line("Camera collision on: a %.1f cm sphere is swept along the lean through "
              "UWorld::SingleLineCheck @ 0x%p with trace flags 0x%X (%d line casts a frame), "
              "release smoothing %.2f",
              cfg.lean_clamp.skin, reinterpret_cast<void*>(moduleBase + profile.rvaSingleLineCheck),
              flags, 1 + 2 * g_sweep.settings.ring_rays, cfg.lean_clamp.release_smoothing);
}

UE3Vector ClampLean(void* controller, const UE3Vector& eye, const UE3Vector& lean) {
    if (!g_query) {
        return lean;
    }

    if (g_hasLastEye) {
        const float mx = eye.X - g_lastEye.X, my = eye.Y - g_lastEye.Y, mz = eye.Z - g_lastEye.Z;
        if (mx * mx + my * my + mz * mz > kCameraCutUu * kCameraCutUu) {
            ResetCameraCollision();
        }
    }
    g_lastEye = eye;
    g_hasLastEye = true;

    g_sweep.cast_context = controller;
    const Vec3 desired(lean.X, lean.Y, lean.Z);
    const float dt = ElapsedSeconds();

    LARGE_INTEGER before, after;
    QueryPerformanceCounter(&before);
    const Vec3 allowed = g_clamp.Apply(Vec3(eye.X, eye.Y, eye.Z), desired, dt, g_query, &g_sweep);
    QueryPerformanceCounter(&after);

    const LeanState state = g_clamp.LastQueryFailed() ? LeanState::QueryFailed
                          : g_clamp.InContact()       ? LeanState::Contact
                                                      : LeanState::Clear;
    Report(state, desired.Magnitude(), allowed.Magnitude(), MicrosecondsBetween(before, after));
    return UE3Vector{ allowed.x, allowed.y, allowed.z };
}

void ResetCameraCollision() {
    g_clamp.Reset();
    g_lastTick.QuadPart = 0;
    g_hasLastEye = false;
}

}  // namespace DishonoredHeadTracking
