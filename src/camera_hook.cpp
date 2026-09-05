// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_hook.h"

#include "aim_marker.h"
#include "camera_collision.h"
#include "fov_hook.h"
#include "fov_range.h"
#include "heap_ptr.h"
#include "hook_install.h"
#include "logging.h"
#include "ue3_math.h"
#include "xmm_guard.h"
#include "zoom_compensation.h"

#include <windows.h>

#include <cmath>

namespace DishonoredHeadTracking {

namespace {

// APlayerController::GetPlayerViewPoint(FVector* outLoc, FRotator* outRot), __thiscall,
// modelled as __fastcall with a dummy edx so MinHook can detour it: arg0 -> ecx (this),
// arg1 -> edx (unused), arg2/arg3 -> stack, matching the original stack layout.
using GetPlayerViewPoint_t = void(__fastcall*)(void* thisptr, void* edx, void* outLoc,
                                               void* outRot);

GetPlayerViewPoint_t g_original = nullptr;
TrackingRuntime* g_tracking = nullptr;

// Everything the detour reads that is only known once the build profile is matched and
// the config is loaded. Written once at install time, before the detour is enabled, and
// read on the game thread from then on.
struct HookSettings {
    // Return address of the ONE call to GetPlayerViewPoint inside CalcSceneView, and the
    // return addresses of the two CalcSceneView callers that must NOT be head-tracked.
    std::uintptr_t sceneViewReturn = 0;
    std::uintptr_t deProjectCaller = 0;
    std::uintptr_t streamingCaller = 0;
    std::uintptr_t gworld = 0;
    std::uintptr_t moduleBase = 0;
    std::uint32_t offPlayerCamera = 0;
    float positionScale = 0.0f;
    bool moveCrosshair = false;
};
HookSettings g_hook;

// ACamera fields. The FOV the projection matrix is built from comes through
// ACamera::GetFOVAngle: LockedFOV when the locked bit is set, otherwise the POV cache's
// own FOV. camera+0x254 looks like a FOV but is DefaultFOV, used only as the divisor
// that turns the FOV into a LOD distance factor - reading it here would miss every
// weapon zoom and put the aim marker in the wrong place exactly when aiming. (It is the
// base the FOV override offsets from, which is why fov_hook.cpp reads it.)
//
// The aspect-ratio pair is the other half of the projection. Normally UE3 builds the
// projection from the viewport's own pixel size, but a view target that constrains the
// aspect ratio - a cinematic camera - makes CalcSceneView build it from
// ConstrainedAspectRatio instead and letterbox the view inside the viewport, which
// moves both the vertical scale and the pixel the aim point lands on.
constexpr std::uint32_t kCamLockedFovBits = 0x258;
constexpr std::uint32_t kCamLockedFov     = 0x25c;
constexpr std::uint32_t kCamPovFov        = 0x348;
constexpr std::uint8_t  kCamConstrainAspectBit = 0x02;
constexpr std::uint32_t kCamConstrainedAspect  = 0x260;

// Menus are the ONLY state the mod suppresses in. A scripted camera still renders
// through this same viewpoint, so head tracking rides along with it: the player can
// look around inside a cutscene, a takedown or a death cam, and the scripted camera
// keeps driving where the view is anchored. Nothing about that reaches game logic -
// only the scene view is injected into - so a cutscene plays out identically whether
// tracking is on or off.
//
// The UI manager's own per-frame dispatch (FUN_00C3DC60) asks exactly this
// question - "is the main menu movie open, else is the pause menu movie open" - before
// routing to the handler for each, so these are the game's own definition of being in a
// menu rather than a heap-diffed guess. The open bit is UGFxMoviePlayer's, corroborated
// from Start and Close guarding on it symmetrically.
//
// The whole walk is plain pointer reads replicated from FUN_00BBF700 rather than a call
// into the game, and every step is validated, so a broken chain gates nothing and head
// tracking keeps working. Failing OPEN is deliberate: a wrong read here that failed
// closed would disable tracking for the entire session with no obvious cause.
constexpr std::uint32_t kWorldToOwner    = 0x2C0;
constexpr std::uint32_t kOwnerToHolder   = 0x410;
constexpr std::uint32_t kHolderToManager = 0x41C;
constexpr std::uint32_t kManagerMainMenu  = 0x2D8;
constexpr std::uint32_t kManagerPauseMenu = 0x2EC;
constexpr std::uint32_t kMovieIsOpen      = 0x0C4;

// How far above our own locals a caller's frame pointer may sit before it is garbage
// rather than a stack frame. One thread stack's worth.
constexpr std::uintptr_t kMaxCallerFrameDistance = 0x100000u;

constexpr DWORD kTrafficReportIntervalMs = 5000;
// Ceilings on the two diagnostics that fire from the per-frame detour. Both describe a
// state that does not change on its own, so repeating them for a whole session adds
// nothing a reader did not have after the first few lines.
constexpr int kMaxTrafficReports = 6;
constexpr int kMaxGateReports = 64;

// The marker is ROTATION-ONLY: it projects the clean aim DIRECTION, and deliberately
// does not correct for the parallax that positional tracking introduces.
//
// Leaning moves the rendered eye away from the eye the shot leaves from, so the fixed
// impact point stops being straight ahead and a direction-based marker sits off it by
// roughly lean/distance - a few degrees at arm's length, under one across a room,
// always the same side, shrinking with range.
//
// Correcting that needs the distance to the surface the bullet stops on, on the frame
// being drawn. Dishonored's interaction and aim code is UnrealScript, so there is no
// native symbol to read a live aim result from, and casting our own ray means calling
// AActor::Trace with a hand-built FCheckResult. Until one of those exists, the honest
// choice is no correction at all.
//
// It must NOT be approximated with a fixed anchor distance. That was tried here: it
// makes the marker exact at the chosen range and wrong either side, with the error
// CHANGING SIDES as the player crosses it, which reads as a broken reticle and sends
// the next person hunting a sign fault that is not there.
//
// Bring parallax back only when BOTH hold:
//   1. a live per-frame distance to the bullet-blocking surface is available, and
//   2. it is filtered by collision layer and mask, never by object name.

enum GateBit {
    kGateNoCamera = 1 << 0,
    kGateBadPov   = 1 << 1,
    kGateMenu     = 1 << 2,
};

std::uint32_t Deref(std::uintptr_t addr) {
    return *reinterpret_cast<const std::uint32_t*>(addr);
}

bool MovieOpen(std::uint32_t manager, std::uint32_t slot) {
    const std::uint32_t movie = Deref(manager + slot);
    return LooksLikeHeapPtr(movie) &&
           (*reinterpret_cast<const std::uint8_t*>(movie + kMovieIsOpen) & 1u) != 0;
}

bool AnyMenuOpen() {
    if (!g_hook.gworld) {
        return false;
    }
    const std::uint32_t world = Deref(g_hook.gworld);
    if (!LooksLikeHeapPtr(world)) return false;
    const std::uint32_t owner = Deref(world + kWorldToOwner);
    if (!LooksLikeHeapPtr(owner)) return false;
    const std::uint32_t holder = Deref(owner + kOwnerToHolder);
    if (!LooksLikeHeapPtr(holder)) return false;
    const std::uint32_t manager = Deref(holder + kHolderToManager);
    if (!LooksLikeHeapPtr(manager)) return false;
    return MovieOpen(manager, kManagerMainMenu) || MovieOpen(manager, kManagerPauseMenu);
}

// Says so once, the first time a zoom is scaled for.
//
// The baseline in the line is the camera's own unzoomed field of view, which is the one
// assumption the scaling rests on. If this appears during ordinary play rather than in a
// scripted scene or a weapon zoom, that baseline is not the field of view the game
// renders at normally, and the head is being scaled the whole time.
void LogZoomCompensation(float fov, float unzoomed, float zoom) {
    static bool s_logged = false;
    if (s_logged || zoom >= 1.0f) {
        return;
    }
    s_logged = true;
    Log::Line("Zoom compensation live: the scene is at %.1f degrees where this camera is "
              "unzoomed at %.1f, so head movement is scaled to %.0f%% of the head to keep "
              "it 1:1 on screen", fov, unzoomed, zoom * 100.0f);
}

// Suppress injection in menus, and wherever the viewpoint cannot be read safely. The
// camera is reached through the controller CalcSceneView asked for the viewpoint, so
// there is no camera search and no cached pointer to go stale across a level load.
std::uint32_t ReadGate(const std::uint8_t* controller, const UE3Vector* loc,
                       float* outFov, float* outConstrainedAspect, float* outZoom) {
    std::uint32_t bits = 0;

    *outConstrainedAspect = 0.0f;
    *outZoom = 1.0f;

    const std::uint32_t cam =
        *reinterpret_cast<const std::uint32_t*>(controller + g_hook.offPlayerCamera);
    if (!LooksLikeHeapPtr(cam)) {
        *outFov = 0.0f;
        return kGateNoCamera;
    }
    const auto* c = reinterpret_cast<const std::uint8_t*>(cam);

    const std::uint8_t fovBits = *reinterpret_cast<const std::uint8_t*>(c + kCamLockedFovBits);
    const float rawFov = *reinterpret_cast<const float*>(c + ((fovBits & 1u) ? kCamLockedFov
                                                                            : kCamPovFov));
    // What the scene is rendered at, which is the raw FOV unless the player configured
    // an override. Published rather than the raw value so the crosshair is projected
    // with the number the projection matrix was built from.
    const float fov = EffectiveFov(rawFov, c);
    *outFov = fov;
    // How far a zoom has narrowed the view from what this camera renders at unzoomed.
    // The head is scaled by it so a scripted scene, which the player is still free to
    // look around inside, does not answer a head turn any harder than free play does.
    const float unzoomed = UnzoomedFov(c);
    *outZoom = ZoomCompensation(fov, unzoomed);
    LogZoomCompensation(fov, unzoomed, *outZoom);
    if (fovBits & kCamConstrainAspectBit) {
        *outConstrainedAspect = *reinterpret_cast<const float*>(c + kCamConstrainedAspect);
    }
    if (!IsUsableFov(fov) ||
        !std::isfinite(loc->X) || !std::isfinite(loc->Y) || !std::isfinite(loc->Z)) {
        bits |= kGateBadPov;
    }

    if (AnyMenuOpen()) {
        bits |= kGateMenu;
    }
    return bits;
}

// Why injection stopped, once per transition. Capped because the pointer walk behind the
// menu bits reads structures that are being rebuilt while a level streams in, so the gate
// can flap frame to frame: uncapped, one bad load would write a line per frame for as long
// as it lasted.
void LogGateChange(std::uint32_t bits, float fov) {
    static int s_reports = 0;
    if (s_reports >= kMaxGateReports) {
        return;
    }
    ++s_reports;
    Log::Line("Gate 0x%02X%s%s%s fov=%.1f%s", bits,
              (bits & kGateNoCamera) ? " NOCAMERA" : "",
              (bits & kGateBadPov)   ? " BADPOV" : "",
              (bits & kGateMenu)     ? " MENU" : "",
              fov,
              s_reports == kMaxGateReports ? " (further gate changes not logged)" : "");
}

// Publishes where the game's aim direction lands in the view the player is looking
// through, so the overlay can put the reticle there. The direction is resolved through
// the same rotator-to-basis conversion the injection just used rather than an Euler
// formula, so it cannot drift out of agreement with the camera on combined poses
// however the rotation is composed.
void PublishAimMarker(const UE3Rotator& clean, const UE3Rotator& tracked, float fov,
                     float constrainedAspect, const float leanRuf[3]) {
    // The clean aim direction, resolved basis-to-basis in the tracked view.
    float ruf[3];
    ResolveAimInTrackedView(clean, tracked, ruf);

    AimMarker& marker = GetAimMarker();
    marker.right.store(ruf[0], std::memory_order_relaxed);
    marker.up.store(ruf[1], std::memory_order_relaxed);
    marker.forward.store(ruf[2], std::memory_order_relaxed);
    marker.fov_deg.store(fov, std::memory_order_relaxed);
    marker.constrained_aspect.store(constrainedAspect, std::memory_order_relaxed);
    marker.lean_right.store(leanRuf[0], std::memory_order_relaxed);
    marker.lean_up.store(leanRuf[1], std::memory_order_relaxed);
    marker.lean_forward.store(leanRuf[2], std::memory_order_relaxed);
    marker.active.store(true, std::memory_order_relaxed);
}

// "No head tracking in game" has exactly two causes worth distinguishing: the hook
// never runs, or it runs but never sees the scene-view caller. Report the counts every
// few seconds until an injection actually happens, then go quiet - a working session
// should not write a log line per five seconds for hours.
void LogTraffic(bool fromSceneView, bool injected) {
    static bool s_confirmed = false;
    static DWORD s_lastLog = 0;
    static unsigned s_total = 0, s_scene = 0;
    static int s_reports = 0;

    if (s_confirmed) {
        return;
    }
    // Ahead of the report cap: the confirmation is the line the whole diagnostic exists
    // to reach, and injection can start long after the reports have run out.
    if (injected) {
        s_confirmed = true;
        Log::Line("Scene-view injection confirmed: head tracking is reaching the "
                  "rendered view and nothing else");
        return;
    }
    if (s_reports >= kMaxTrafficReports) {
        return;
    }

    ++s_total;
    if (fromSceneView) ++s_scene;
    const DWORD now = GetTickCount();
    if (s_lastLog == 0) {
        s_lastLog = now;
        return;
    }
    if (now - s_lastLog < kTrafficReportIntervalMs) {
        return;
    }
    s_lastLog = now;
    ++s_reports;
    Log::Line("Viewpoint: %u calls in the last %us, %u of them from the scene view, "
              "none injected%s", s_total,
              static_cast<unsigned>(kTrafficReportIntervalMs / 1000), s_scene,
              s_reports == kMaxTrafficReports
                  ? "; no further viewpoint reports this session" : "");
    s_total = 0;
    s_scene = 0;
}

// The frame this call belongs to is not being rendered: hide the marker so the overlay
// draws nothing, and keep counting for the traffic report.
void StandDown() {
    GetAimMarker().active.store(false, std::memory_order_relaxed);
    ResetCameraCollision();
    LogTraffic(true, false);
}

// Reads the return address of ULocalPlayer::CalcSceneView out of its own frame, given
// the frame pointer it held when it called us. CalcSceneView realigns the stack and
// then restores the classic frame layout, so its return address sits at [ebp+4] exactly
// as it would in any other function.
//
// Fails OPEN. A wrong read here must not be able to switch head tracking off; the two
// callers this exists to exclude are named explicitly, and anything unrecognised is
// logged and injected. The guard is that the frame has to be above our own locals and
// within one stack's reach of them, which a garbage value will not be.
std::uintptr_t SceneViewCaller(void* callerFrame) {
    const auto frame = reinterpret_cast<std::uintptr_t>(callerFrame);
    const auto here = reinterpret_cast<std::uintptr_t>(&callerFrame);
    if (frame <= here || frame - here > kMaxCallerFrameDistance || (frame & 3u) != 0) {
        return 0;
    }
    return *reinterpret_cast<const std::uintptr_t*>(frame + 4);
}

void LogSceneViewCaller(std::uintptr_t caller) {
    constexpr int kMaxLoggedCallers = 4;
    static std::uintptr_t s_seen[kMaxLoggedCallers] = {};
    static int s_count = 0;
    for (int i = 0; i < s_count; ++i) {
        if (s_seen[i] == caller) return;
    }
    // Full table: a caller that cannot be remembered must not be logged either, or every
    // frame it appears on writes the same line again.
    if (s_count == kMaxLoggedCallers) {
        return;
    }
    s_seen[s_count++] = caller;
    if (caller == 0) {
        Log::Line("Scene view caller frame walk failed; injecting anyway");
        return;
    }
    Log::Line("Scene view requested by RVA 0x%06X",
              static_cast<unsigned>(caller - g_hook.moduleBase));
}

// Moves the viewpoint by the tracked head position, and reports the lean it applied in
// the engine's own right/up/forward basis so the marker can publish it.
//
// The lean is what the collision clamp acts on: it is traced against the world from the
// clean eye and cut back to whatever keeps the rendered eye out of a wall, so what is
// added to the location and what is published to the marker are both the lean that was
// actually applied rather than the one the tracker asked for.
//
// @p zoom is the zoom compensation for this frame. A lean shifts the image by the
// parallax it opens up, which the projection scales by 1/tan(fov/2) exactly as it scales
// a rotation, so leaning is amplified by a zoom the same way turning is and is scaled
// back by the same factor.
void ApplyPositionOffset(const FrameSample& s, const UE3Rotator& clean, void* controller,
                         float zoom, UE3Vector* loc, float leanRuf[3]) {
    // Horizon-locked basis, built from the CLEAN yaw alone: forward = (cy, sy, 0),
    // right = (-sy, cy, 0), up = world +Z. Carrying the clean pitch into the forward
    // vector makes the three axes non-orthogonal and turns a forward lean into a
    // descent: looking down 60 degrees and leaning in 0.20 m drove the eye 17 cm
    // straight into the floor.
    const float yawRad = static_cast<float>(clean.Yaw) * kUnitsToRad;
    const float cy = std::cos(yawRad), sy = std::sin(yawRad);

    // Protocol-to-engine axis conversion, done HERE and only here. The core's
    // convention is that negative z is the forward lean, which is what puts the
    // generous LimitZ (0.40 m) on leaning in and the restricted LimitZBack (0.10 m)
    // on pulling away; UE3's camera-local +X is forward, so the sign flips at this
    // boundary, after the clamp rather than before it. Doing it with the
    // processor's invert_z instead flips the value ahead of the clamp and hands the
    // 0.40 m to the backward lean, which reads in game as "leaning in barely moves,
    // pulling back moves a lot". x is mirrored the same way and is converted in the
    // same place; its clamp is symmetric so only the direction changes.
    const float scale = g_hook.positionScale * zoom;
    const float oR = -s.pos_x * scale;
    const float oU =  s.pos_y * scale;
    const float oF = -s.pos_z * scale;

    const float dx = cy * oF - sy * oR;
    const float dy = sy * oF + cy * oR;
    const float dz = oU;

    const float allowed = AllowedLeanFraction(controller, *loc, dx, dy, dz);

    leanRuf[0] = oR * allowed;
    leanRuf[1] = oU * allowed;
    leanRuf[2] = oF * allowed;

    loc->X += dx * allowed;
    loc->Y += dy * allowed;
    loc->Z += dz * allowed;
}

// Adds the tracked head rotation to the viewpoint. The composition, the engine's roll
// conversion and the pitch bound all live in ue3_math.h so they are testable.
//
// Yaw and pitch are scaled by @p zoom, the zoom compensation for this frame. Roll is
// not: a head tilt turns the image by its own angle whatever the field of view, so there
// is nothing for a zoom to amplify and scaling it would under-tilt the view.
void ApplyHeadRotation(const FrameSample& s, const UE3Rotator& clean, bool worldSpaceYaw,
                       float zoom, UE3Rotator* rot) {
    ComposeHeadRotation(clean, s.pitch * zoom, s.yaw * zoom, s.roll, worldSpaceYaw, rot);
}

void __fastcall DetourImpl(void* thisptr, void* edx, void* outLoc, void* outRot,
                           void* retaddr, void* callerFrame) {
    g_original(thisptr, edx, outLoc, outRot);

    // Every caller but the scene view keeps the rotation the mouse chose. That is the
    // whole of aim decoupling: weapon fire, interaction traces, AI vision and audio all
    // read this function, and they read it clean.
    const bool fromSceneView =
        reinterpret_cast<std::uintptr_t>(retaddr) == g_hook.sceneViewReturn;
    // Read the runtime ONCE. Teardown clears it while game threads are still inside
    // this function, so a second read after the null check can hand SampleFrame a
    // pointer the check never saw.
    TrackingRuntime* const tracking = g_tracking;
    if (!tracking || !thisptr || !fromSceneView) {
        LogTraffic(fromSceneView, false);
        return;
    }

    const std::uintptr_t caller = SceneViewCaller(callerFrame);
    LogSceneViewCaller(caller);
    if (caller == g_hook.deProjectCaller || caller == g_hook.streamingCaller) {
        // A screen-to-world query, or the texture streamer asking where to prefetch
        // from. Neither is what the player is looking through.
        LogTraffic(true, false);
        return;
    }

    auto* loc = static_cast<UE3Vector*>(outLoc);
    auto* rot = static_cast<UE3Rotator*>(outRot);
    const UE3Rotator clean = *rot;

    float fov = 0.0f;
    float constrainedAspect = 0.0f;
    float zoom = 1.0f;
    const std::uint32_t gate = ReadGate(static_cast<std::uint8_t*>(thisptr), loc, &fov,
                                        &constrainedAspect, &zoom);

    static std::uint32_t s_lastGate = 0xFFFFFFFFu;
    if (gate != s_lastGate) {
        s_lastGate = gate;
        LogGateChange(gate, fov);
    }

    if (gate != 0) {
        StandDown();
        return;
    }

    const FrameSample s = tracking->SampleFrame();
    if (!s.has_rotation && !s.has_position) {
        StandDown();
        return;
    }

    float leanRuf[3] = { 0.0f, 0.0f, 0.0f };
    if (s.has_position) {
        ApplyPositionOffset(s, clean, thisptr, zoom, loc, leanRuf);
    } else {
        ResetCameraCollision();
    }
    if (s.has_rotation) {
        ApplyHeadRotation(s, clean, tracking->IsWorldSpaceYaw(), zoom, rot);
    }

    if (g_hook.moveCrosshair) {
        PublishAimMarker(clean, *rot, fov, constrainedAspect, leanRuf);
    }
    LogTraffic(true, true);
}

using DetourImpl_t = void(__fastcall*)(void*, void*, void*, void*, void*, void*);
DetourImpl_t g_implPtr = &DetourImpl;

// Stack at entry: [esp] return address, [esp+4] outLoc, [esp+8] outRot; ecx holds the
// controller and is passed straight through. The return address becomes the impl's
// fifth argument - it is what tells the scene view apart from every other caller - and
// the caller's own frame pointer becomes the sixth, which is how the scene view's three
// callers are told apart from each other. See xmm_guard.h for the register preservation.
__declspec(naked) void __fastcall Detour(void*, void*, void*, void*) {
    DHT_DETOUR_ENTER
    __asm {
        push dword ptr [ebp]
        push dword ptr [ebp+4]
        push dword ptr [ebp+12]
        push dword ptr [ebp+8]
        call dword ptr [g_implPtr]
    }
    DHT_DETOUR_RESTORE_XMM
    __asm {
        mov  esp, ebp
        pop  ebp
        ret  8
    }
}

}  // namespace

bool InstallCameraHook(const BuildProfile& profile, std::uintptr_t moduleBase,
                       TrackingRuntime& tracking, const Config& cfg) {
    g_tracking = &tracking;
    g_hook.sceneViewReturn = moduleBase + profile.rvaCalcSceneViewReturn;
    g_hook.deProjectCaller = moduleBase + profile.rvaDeProjectCaller;
    g_hook.streamingCaller = moduleBase + profile.rvaStreamingCaller;
    g_hook.gworld = moduleBase + profile.rvaGWorld;
    g_hook.moduleBase = moduleBase;
    g_hook.offPlayerCamera = profile.offPlayerCamera;
    g_hook.positionScale = cfg.position_scale;
    g_hook.moveCrosshair = cfg.move_crosshair;

    InitCameraCollision(profile, moduleBase, cfg);

    const std::uintptr_t target = moduleBase + profile.rvaGetPlayerViewPoint;
    if (!InstallDetour(target, reinterpret_cast<void*>(&Detour),
                       reinterpret_cast<void**>(&g_original),
                       "APlayerController::GetPlayerViewPoint")) {
        return false;
    }
    Log::Line("Camera hook installed on APlayerController::GetPlayerViewPoint @ 0x%p "
              "(scene-view caller returns to 0x%p)", reinterpret_cast<void*>(target),
              reinterpret_cast<void*>(g_hook.sceneViewReturn));
    return true;
}

}  // namespace DishonoredHeadTracking
