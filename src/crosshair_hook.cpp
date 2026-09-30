// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "crosshair_hook.h"

#include "aim_marker.h"
#include "aim_projection.h"
#include "hook_install.h"
#include "logging.h"
#include "runtime_discovery.h"
#include "xmm_guard.h"

#include <windows.h>

#include <cstdint>

namespace DishonoredHeadTracking {

namespace {

// UDisGFxMoviePlayerHUD, __thiscall(this, FSceneView* view). The per-frame crosshair
// update: it recomputes the crosshair's target position, then pushes the CURRENT
// position into `_root._dot_mc` through Scaleform's SetDisplayInfo, and invokes the
// clip's own SetCrosshairState to pick the art for the equipped item. Hooking it is how
// the aim point reaches the crosshair on the frame it belongs to.
using CrosshairUpdate_t = void(__fastcall*)(void*, void*, void*);
CrosshairUpdate_t g_original = nullptr;

constexpr DWORD kDiagnosticIntervalMs = 1000;
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
    const DWORD now = GetTickCount();
    if (s_last != 0 && now - s_last < kDiagnosticIntervalMs) {
        return;
    }
    s_last = now;

    Log::Line("CROSSHAIR vp=%.0fx%.0f fov=%.1f car=%.2f dir(r,u,f)=(%.3f,%.3f,%.3f) "
              "lean(r,u,f)cm=(%.1f,%.1f,%.1f) ndc=(%.3f,%.3f) px=(%.0f,%.0f)%s",
              vpW, vpH, m.fov_deg, m.constrained_aspect, m.right, m.up, m.forward,
              m.lean_right, m.lean_up, m.lean_forward, px.ndc_x, px.ndc_y, px.x, px.y,
              px.clamped ? " CLAMPED" : "");

}

void __cdecl UpdateCrosshair(void* hudPtr) {
    auto* hud = static_cast<std::uint8_t*>(hudPtr);
    if (!HasLiveClass(hud, LiveLayout().hudClass)) {
        return;
    }

    const AimMarker& marker = GetAimMarker();
    if (!marker.active.load(std::memory_order_relaxed)) {
        return;
    }

    const float vpW = ReadHudFloat(hud, LiveLayout().hudViewportW);
    const float vpH = ReadHudFloat(hud, LiveLayout().hudViewportH);
    if (!IsUsableViewport(vpW, vpH)) {
        return;
    }

    const AimMarkerSample m = SampleAimMarker(marker);
    const ViewRect rect = ComputeViewRect(vpW, vpH, m.constrained_aspect);
    AimPixel px;
    if (!ProjectAimToPixels(rect, m.fov_deg, m.right, m.up, m.forward, &px)) {
        return;
    }

    *reinterpret_cast<float*>(hud + LiveLayout().hudDotX) = px.x;
    *reinterpret_cast<float*>(hud + LiveLayout().hudDotY) = px.y;

    if (!g_confirmed) {
        g_confirmed = true;
        Log::Line("Crosshair moved to the aim point: the game's own crosshair now marks "
                  "where the shot lands");
    }
    ReportGeometry(m, vpW, vpH, px);
}

using CrosshairImpl_t = void(__cdecl*)(void*);
CrosshairImpl_t g_implPtr = &UpdateCrosshair;

// Stack at entry: [esp] return address, [esp+4] the scene view; ecx holds the HUD. The
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
