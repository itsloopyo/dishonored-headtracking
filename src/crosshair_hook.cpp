// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "crosshair_hook.h"

#include "aim_marker.h"
#include "aim_projection.h"
#include "hook_install.h"
#include "logging.h"
#include "xmm_guard.h"

#include <windows.h>

#include <cstdint>

namespace DishonoredHeadTracking {

namespace {

// UDisGFxMoviePlayerHUD, __thiscall(this, float deltaSeconds). The per-frame crosshair
// update: it recomputes the crosshair's target position, then pushes the CURRENT
// position into `_root._dot_mc` through Scaleform's SetDisplayInfo, and invokes the
// clip's own SetCrosshairState to pick the art for the equipped item. Hooking it is how
// the aim point reaches the crosshair on the frame it belongs to.
using CrosshairUpdate_t = void(__fastcall*)(void*, void*, float);
CrosshairUpdate_t g_original = nullptr;

// HUD fields, all read out of the same function that binds the widgets.
//
// The viewport pair is refreshed from the movie immediately before every call we hook,
// so it is never stale after a resolution change.
constexpr std::uint32_t kHudViewportW = 0x1e0;
constexpr std::uint32_t kHudViewportH = 0x1e4;
// Where the crosshair IS. The HUD eases this toward its target each tick and hands it
// to Scaleform as the clip's _x / _y. Writing it here, before the update reads it,
// places the crosshair this frame rather than the next; the target is left alone, so
// when tracking stops the game's own easing walks the crosshair back to centre with no
// help from us.
//
// `_root` is one-to-one with the viewport in pixels: the HUD does its own letterbox
// arithmetic in pixel space and hands Scaleform pixel coordinates, and the crosshair is
// initialised to exactly (width/2, height/2). So an aim point in pixels can be written
// straight in, with no stage-to-screen conversion and no scale mode to second-guess.
constexpr std::uint32_t kHudDotX = 0x3d8;
constexpr std::uint32_t kHudDotY = 0x3dc;

constexpr DWORD kDiagnosticIntervalMs = 1000;
// A bounded burst, not a running commentary. The line below is the crosshair bring-up
// diagnostic and it fires from a per-frame hook: a few seconds of it shows the geometry,
// while writing it for a whole session would bury the startup lines a player is asked to
// send under thousands of frames of arithmetic.
constexpr int kMaxDiagnosticLines = 8;

bool g_confirmed = false;

float ReadHudFloat(const std::uint8_t* hud, std::uint32_t offset) {
    return *reinterpret_cast<const float*>(hud + offset);
}

void ReportGeometry(const AimMarkerSample& m, float vpW, float vpH, const AimPixel& px) {
    // Every term the crosshair position depends on, on ONE line, once a second. Reading
    // the position from one line and the field of view from another is how a fix gets
    // shipped against the wrong fault: each half-reading fits several of them equally
    // well. The lean is here because the aim point is rotation-only - its residual error
    // is the lean divided by the target distance, and that is arithmetic only when both
    // terms are on the same line.
    static DWORD s_last = 0;
    static int s_lines = 0;
    if (s_lines >= kMaxDiagnosticLines) {
        return;
    }
    const DWORD now = GetTickCount();
    if (s_last != 0 && now - s_last < kDiagnosticIntervalMs) {
        return;
    }
    s_last = now;
    ++s_lines;
    Log::Line("CROSSHAIR vp=%.0fx%.0f fov=%.1f car=%.2f dir(r,u,f)=(%.3f,%.3f,%.3f) "
              "lean(r,u,f)cm=(%.1f,%.1f,%.1f) ndc=(%.3f,%.3f) px=(%.0f,%.0f)%s",
              vpW, vpH, m.fov_deg, m.constrained_aspect, m.right, m.up, m.forward,
              m.lean_right, m.lean_up, m.lean_forward, px.ndc_x, px.ndc_y, px.x, px.y,
              px.clamped ? " CLAMPED" : "");
    if (s_lines == kMaxDiagnosticLines) {
        Log::Line("Crosshair geometry reported %d times; the crosshair keeps following the "
                  "aim point but will not be logged again this session", kMaxDiagnosticLines);
    }
}

void __cdecl UpdateCrosshair(void* hudPtr) {
    auto* hud = static_cast<std::uint8_t*>(hudPtr);
    if (!hud) {
        return;
    }

    const AimMarker& marker = GetAimMarker();
    if (!marker.active.load(std::memory_order_relaxed)) {
        return;
    }

    const float vpW = ReadHudFloat(hud, kHudViewportW);
    const float vpH = ReadHudFloat(hud, kHudViewportH);
    if (!IsUsableViewport(vpW, vpH)) {
        return;
    }

    const AimMarkerSample m = SampleAimMarker(marker);
    const ViewRect rect = ComputeViewRect(vpW, vpH, m.constrained_aspect);
    AimPixel px;
    if (!ProjectAimToPixels(rect, m.fov_deg, m.right, m.up, m.forward, &px)) {
        return;
    }

    *reinterpret_cast<float*>(hud + kHudDotX) = px.x;
    *reinterpret_cast<float*>(hud + kHudDotY) = px.y;

    if (!g_confirmed) {
        g_confirmed = true;
        Log::Line("Crosshair moved to the aim point: the game's own crosshair now marks "
                  "where the shot lands");
    }
    ReportGeometry(m, vpW, vpH, px);
}

using CrosshairImpl_t = void(__cdecl*)(void*);
CrosshairImpl_t g_implPtr = &UpdateCrosshair;

// Stack at entry: [esp] return address, [esp+4] the frame delta; ecx holds the HUD. The
// tail jump hands the original exactly that, so it runs as if we had never been here and
// its own `ret 4` returns straight to the game. See xmm_guard.h for the register
// preservation; ecx is stashed alongside the XMM file because the impl call clobbers it.
__declspec(naked) void __fastcall Detour(void*, void*, float) {
    DHT_DETOUR_ENTER
    __asm {
        mov  [ebp-144], ecx
        push ecx
        call dword ptr [g_implPtr]
        add  esp, 4
    }
    DHT_DETOUR_RESTORE_XMM
    __asm {
        mov  ecx, [ebp-144]
        mov  esp, ebp
        pop  ebp
        jmp  dword ptr [g_original]
    }
}

}  // namespace

bool InstallCrosshairHook(const BuildProfile& profile, std::uintptr_t moduleBase) {
    const std::uintptr_t target = moduleBase + profile.rvaCrosshairUpdate;
    if (!InstallDetour(target, reinterpret_cast<void*>(&Detour),
                       reinterpret_cast<void**>(&g_original),
                       "the HUD crosshair update")) {
        return false;
    }

    Log::Line("Crosshair hook installed on the HUD crosshair update @ 0x%08X",
              static_cast<unsigned>(target));
    return true;
}

}  // namespace DishonoredHeadTracking
