// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

#include "logging.h"

#include <windows.h>

namespace DishonoredHeadTracking {

// steam-win32-20220217: Steam retail build, TimeDateStamp 0x620EAE07,
// SizeOfImage 0x1207000, CheckSum 0x113BB15. ImageBase 0x400000.
//
// FUN_005E17A0 (VA 0x005E17A0 -> RVA 0x1E17A0) is APlayerController::GetPlayerViewPoint:
// it lazily spawns PlayerCamera from CameraClass (this+0x388) into this+0x384, then
// copies that camera's POV cache (camera+0x330 location, camera+0x33c FRotator) into
// the out-params. Every AActor-derived controller vtable in the image carries it at
// slot +0x3C4, including 0x01118738 - the vtable the camera's PCOwner pointer
// (camera+0x10c) resolves to in game, so this is the function the live controller runs.
//
// FUN_006C4710 is ULocalPlayer::CalcSceneView. It calls the +0x3C4 slot exactly once,
// at 0x006C48AC, returning to VA 0x006C48AE -> RVA 0x2C48AE. Weapon aim, interaction
// traces, AI vision and audio call GetPlayerViewPoint from their own sites, so that one
// return address already separates the view from everything that shoots.
//
// CalcSceneView has three callers of its own, though, and all three funnel through that
// same call, so the return address alone does not identify the render:
//   0x006C5CF3  UGameViewportClient::Draw            <- the render. Inject here.
//   0x006C524B  ULocalPlayer::DeProject              <- screen position to world ray.
//   0x005FC6D8  FViewport::Draw, streaming-only path <- texture prefetch origin.
// DeProject is the one that matters: handing it a head-rotated view would let a
// screen-to-world query answer with where the player is LOOKING rather than where the
// game is aiming, which is the coupling this whole hook exists to remove.
//
// FUN_00642FD0 (VA 0x00642FD0 -> RVA 0x242FD0) is APlayerController::GetFOVAngle: it
// tail-calls ACamera::GetFOVAngle (0x005BD050, LockedFOV at camera+0x25c when
// camera+0x258 bit 0 is set, else the POV cache's FOV at camera+0x348) and falls back to
// its own FOVAngle at controller+0x3ac when PlayerCamera is null. CalcSceneView calls it
// at 0x006C48B1, returning to VA 0x006C48B6 -> RVA 0x2C48B6, and the result reaches
// FPerspectiveMatrix as HalfFOV via * PI/360 - so it, and nothing else, is the FOV the
// scene is rendered with. camera+0x254 (DefaultFOV) only divides it into the LOD
// distance factor.
//
// FUN_0064E7A0 (VA 0x0064E7A0 -> RVA 0x24E7A0) is UWorld::SingleLineCheck, and
// 0x01449888 (RVA 0x1049888) is GWorld. Both come from AActor::execTrace (0x006D0ED0 in
// the native table), which loads that global into ECX at 0x006D1278 and calls the
// function at 0x006D1282 with seven stack arguments the callee cleans - Hit, SourceActor,
// End, Start, TraceFlags, Extent, SourceLight. AActor::execFastTrace (0x006CD240) makes
// the same call with the same shape, and both read the hit off FCheckResult+0x04 rather
// than the return value.
//
// FUN_00B9FF30 (VA 0x00B9FF30 -> RVA 0x79FF30) is the HUD's per-frame crosshair update.
// It builds a Scaleform DisplayInfo from HUD+0x3d8/+0x3dc and hands it to SetDisplayInfo
// for widget record 0 of the array at HUD+0x200, which the HUD binder fills from the
// path "_root._dot_mc"; the same record is then the target of an Invoke of
// "SetCrosshairState", so that clip carries the crosshair art rather than just a dot.
// The binder initialises HUD+0x3d8/+0x3dc to half the viewport width and height, which
// is what establishes that the clip's coordinates are viewport pixels.
static const BuildProfile kSteamProfile_20220217 = {
    "steam-win32-20220217",
    { 0x620EAE07u, 0x01207000u, 0x0113BB15u },
    0x1E17A0u,
    0x2C48AEu,
    0x2C524Bu,
    0x1FC6D8u,
    0x242FD0u,
    0x2C48B6u,
    0x384u,
    0x1049888u,
    0x24E7A0u,
    0x79FF30u,
};

const BuildProfile kKnownProfiles[] = {
    kSteamProfile_20220217,
};
const int kKnownProfileCount = static_cast<int>(sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]));

const BuildProfile* MatchRunningProfile() {
    HMODULE hExe = GetModuleHandleA(kGameExeName);
    if (!hExe) {
        Log::Line("ERROR: %s module not found for fingerprinting", kGameExeName);
        return nullptr;
    }

    cameraunlock::memory::PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(hExe, running)) {
        Log::Line("ERROR: could not read PE fingerprint of %s", kGameExeName);
        return nullptr;
    }

    for (int i = 0; i < kKnownProfileCount; ++i) {
        if (running.Matches(kKnownProfiles[i].fingerprint)) {
            Log::Line("Build profile matched: %s", kKnownProfiles[i].name);
            return &kKnownProfiles[i];
        }
    }

    using cameraunlock::memory::ClassifyMismatch;
    using cameraunlock::memory::FingerprintMismatch;
    const BuildProfile& primary = kKnownProfiles[0];
    switch (ClassifyMismatch(running, primary.fingerprint)) {
        case FingerprintMismatch::Newer:
            Log::Line("Unrecognised Dishonored build (newer than %s). "
                      "Check the releases page for an updated mod. Staying dormant.",
                      primary.name);
            break;
        case FingerprintMismatch::Older:
            Log::Line("Unrecognised Dishonored build (older than %s). "
                      "Let Steam finish updating. Staying dormant.",
                      primary.name);
            break;
        case FingerprintMismatch::Differs:
            Log::Line("Dishonored.exe is tampered/repacked (fingerprint differs). "
                      "Staying dormant.");
            break;
    }
    return nullptr;
}

}  // namespace DishonoredHeadTracking
