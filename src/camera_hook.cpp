// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_hook.h"

#include "aim_marker.h"
#include "camera_collision.h"
#include "fov_hook.h"
#include "fov_range.h"
#include "runtime_discovery.h"
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

// Everything the detour reads that is only known once runtime discovery is validated and
// the config is loaded. Written once at install time, before the detour is enabled, and
// read on the game thread from then on.
struct HookSettings {
    // Return address of the ONE call to GetPlayerViewPoint inside CalcSceneView, and the
    // return addresses of the two CalcSceneView callers that must NOT be head-tracked.
    std::uintptr_t sceneViewReturn = 0;
    std::uintptr_t deProjectCaller = 0;
    std::uintptr_t streamingCaller = 0;
    std::uintptr_t viewportCaller = 0;

    std::uintptr_t moduleBase = 0;
    std::uint32_t offPlayerCamera = 0;
};
HookSettings g_hook;

constexpr DWORD kTrafficReportIntervalMs = 5000;
// Positional parallax needs a live aim-distance query. A fixed anchor distance
// would move the error to the opposite side as the player crosses that distance.

enum GateBit {
    kGateNoCamera = 1 << 0,
    kGateBadPov   = 1 << 1,
    kGateMenu     = 1 << 2,
    kGateLayout   = 1 << 3,
};

// Says so once, the first time a zoom is scaled for.
//
// The baseline in the line is the camera's own unzoomed field of view, which is the one
// assumption the scaling rests on. If this appears during ordinary play rather than in a
// scripted scene or a weapon zoom, that baseline is not the field of view the game
// renders at normally, and the head is being scaled the whole time.
void LogZoomCompensation(float fov, float unzoomed, float zoom) {
    // Every term, on the first frame the camera is read, tracker or not. A baseline that is
    // wrong by a constant scales the head the whole session and looks right from inside
    // any single frame, so the gate is this line reading 1.0000 in ordinary play.
    static bool s_basisLogged = false;
    if (!s_basisLogged) {
        s_basisLogged = true;
        Log::Line("Zoom compensation basis: scene fov %.2f degrees (horizontal), unzoomed "
                  "fov %.2f degrees (ACamera::DefaultFOV through the FOV override), factor "
                  "%.4f", fov, unzoomed, zoom);
    }
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

    const auto& layout = LiveLayout();
    std::uint32_t cam = 0;
    if (!HasLiveClass(controller, layout.pcClass) ||
        !ReadLive(reinterpret_cast<std::uintptr_t>(controller) + g_hook.offPlayerCamera, cam) ||
        !HasLiveClass(reinterpret_cast<const void*>(cam), layout.cameraClass)) {
        *outFov = 0.0f;
        return kGateNoCamera;
    }
    const auto* c = reinterpret_cast<const std::uint8_t*>(cam);

    std::uint32_t fovBits = 0, aspectBits = 0;
    float rawFov = 0.0f, defaultFov = 0.0f;
    if (!ReadLive(cam + layout.lockedBits, fovBits) ||
        !ReadLive(cam + ((fovBits & layout.lockedMask) ? layout.lockedFov : layout.povFov), rawFov) ||
        !ReadLive(cam + layout.camDefaultFov, defaultFov) || !IsUsableFov(defaultFov) ||
        !ReadLive(cam + layout.aspectBits, aspectBits)) return kGateLayout;
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
    if (aspectBits & layout.aspectMask) {
        if (!ReadLive(cam + layout.aspect, *outConstrainedAspect)) return kGateLayout;
    }
    if (!IsUsableFov(fov) ||
        !std::isfinite(loc->X) || !std::isfinite(loc->Y) || !std::isfinite(loc->Z)) {
        bits |= kGateBadPov;
    }

    bool menuOpen = false;
    if (!ReadMenuState(controller, menuOpen)) return kGateLayout;
    if (menuOpen) {
        bits |= kGateMenu;
    }
    return bits;
}

void LogGateChange(std::uint32_t bits, float fov) {
    static std::uint32_t lastBits = 0xFFFFFFFFu;
    static DWORD lastTime = 0;
    const DWORD now = GetTickCount();
    if (bits == lastBits || (lastTime && now - lastTime < 1000)) return;
    lastBits = bits;
    lastTime = now;
    Log::Line("Gate 0x%02X%s%s%s%s fov=%.1f", bits,
              (bits & kGateNoCamera) ? " NOCAMERA" : "",
              (bits & kGateBadPov) ? " BADPOV" : "",
              (bits & kGateMenu) ? " MENU" : "",
              (bits & kGateLayout) ? " LAYOUT" : "", fov);
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

void LogTraffic(bool fromSceneView, bool injected) {
    static bool confirmed = false;
    static DWORD last = 0;
    static unsigned total = 0, scene = 0, applied = 0;
    ++total;
    if (fromSceneView) ++scene;
    if (injected) ++applied;
    if (injected && !confirmed) {
        confirmed = true;
        Log::Line("Scene-view injection confirmed");
    }
    const DWORD now = GetTickCount();
    if (!last) { last = now; return; }
    if (now - last < kTrafficReportIntervalMs) return;
    Log::Line("Viewpoint: %u calls, %u scene-view, %u injected in %us", total, scene,
              applied, static_cast<unsigned>((now - last) / 1000));
    last = now;
    total = scene = applied = 0;
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
// The return-address slot has to lie between our
// own locals and the base of this thread's stack, the only span that is committed.
std::uintptr_t SceneViewCaller(void* callerFrame) {
    const auto frame = reinterpret_cast<std::uintptr_t>(callerFrame);
    const auto here = reinterpret_cast<std::uintptr_t>(&callerFrame);
    const auto stackBase = reinterpret_cast<std::uintptr_t>(
        reinterpret_cast<const NT_TIB*>(NtCurrentTeb())->StackBase);
    if (frame <= here || frame + 8 > stackBase || (frame & 3u) != 0) {
        return 0;
    }
    return *reinterpret_cast<const std::uintptr_t*>(frame + 4);
}

void LogSceneViewCaller(std::uintptr_t caller) {
    static std::uintptr_t lastCaller = UINTPTR_MAX;
    static DWORD lastTime = 0;
    const DWORD now = GetTickCount();
    if (caller == lastCaller || (lastTime && now - lastTime < 1000)) return;
    lastCaller = caller;
    lastTime = now;
    if (caller == 0) {
        Log::Line("Scene view caller frame walk failed; leaving viewpoint unchanged");
        return;
    }
    const char* source = caller == g_hook.viewportCaller ? "viewport" :
        caller == g_hook.deProjectCaller ? "deprojection" :
        caller == g_hook.streamingCaller ? "streaming" : "unrecognised; unchanged";
    Log::Line("Scene view requested by RVA 0x%06X (%s)",
              static_cast<unsigned>(caller - g_hook.moduleBase), source);
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
    const float scale = kWorldUnitsPerMetre * zoom;
    const float oR = -s.pos_x * scale;
    const float oU =  s.pos_y * scale;
    const float oF = -s.pos_z * scale;

    const float dx = cy * oF - sy * oR;
    const float dy = sy * oF + cy * oR;
    const float dz = oU;

    const UE3Vector allowed = ClampLean(controller, *loc, UE3Vector{ dx, dy, dz });

    // The clamp only ever shortens the lean along its own direction, so the published
    // right/up/forward lean shrinks by the same ratio.
    const float wanted = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float kept = std::sqrt(allowed.X * allowed.X + allowed.Y * allowed.Y +
                                 allowed.Z * allowed.Z);
    const float fraction = wanted > 0.0f ? kept / wanted : 1.0f;
    leanRuf[0] = oR * fraction;
    leanRuf[1] = oU * fraction;
    leanRuf[2] = oF * fraction;

    loc->X += allowed.X;
    loc->Y += allowed.Y;
    loc->Z += allowed.Z;
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
    if (!thisptr || !fromSceneView) {
        LogTraffic(fromSceneView, false);
        return;
    }

    const std::uintptr_t caller = SceneViewCaller(callerFrame);
    LogSceneViewCaller(caller);
    if (caller != g_hook.viewportCaller) {
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

    LogGateChange(gate, fov);

    if (gate != 0) {
        StandDown();
        return;
    }

    const FrameSample s = g_tracking->SampleFrame();
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
        ApplyHeadRotation(s, clean, g_tracking->IsWorldSpaceYaw(), zoom, rot);
    }

    PublishAimMarker(clean, *rot, fov, constrainedAspect, leanRuf);
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
    g_hook.viewportCaller = moduleBase + profile.rvaViewportSceneViewCaller;

    g_hook.moduleBase = moduleBase;
    g_hook.offPlayerCamera = profile.offPlayerCamera;

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
