// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/memory/pe_fingerprint.h"

#include <cstdint>

namespace DishonoredHeadTracking {

// The game module every RVA in a profile is relative to, and the module the fingerprint
// is read from.
constexpr const char* kGameExeName = "Dishonored.exe";

// One shipped Dishonored build: its PE fingerprint and the RVAs the camera hook
// pins to. Append-only registry (see AGENTS.md "Maintain compatibility across new
// patches"): a patch that moves RVAs gets a NEW profile added to the top of
// kKnownProfiles, never an in-place edit, so users on older builds keep matching
// their original profile by fingerprint.
struct BuildProfile {
    const char* name;
    cameraunlock::memory::PeFingerprint fingerprint;

    // APlayerController::GetPlayerViewPoint(FVector* outLoc, FRotator* outRot),
    // __thiscall, ret 8. Reads the camera POV cache and is the shared "where is the
    // player looking" accessor: the renderer, weapon aim, interaction traces, AI and
    // audio all come through it.
    std::uintptr_t rvaGetPlayerViewPoint;

    // Return address of the ONE call to GetPlayerViewPoint inside
    // ULocalPlayer::CalcSceneView. Injecting only for this caller is what decouples
    // look from aim: the scene view gets the head-tracked rotation, every other
    // caller keeps the rotation the mouse chose.
    std::uintptr_t rvaCalcSceneViewReturn;

    // CalcSceneView itself has three callers, and they all reach GetPlayerViewPoint
    // through that same one call, so the return address above cannot tell them apart.
    // Only the viewport draw should be head-tracked. These are the return addresses of
    // the two that must not be: ULocalPlayer::DeProject, which turns a screen position
    // into a world ray and would hand a head-rotated answer to whatever asked, and the
    // streaming-only branch of FViewport::Draw, which just wants to know where to
    // prefetch textures from.
    std::uintptr_t rvaDeProjectCaller;
    std::uintptr_t rvaStreamingCaller;

    // APlayerController::GetFOVAngle, __thiscall(this) -> float degrees. Returns the
    // camera's LockedFOV when the locked bit is set and its POV cache FOV otherwise,
    // falling back to the controller's own FOVAngle when there is no camera. It is what
    // CalcSceneView builds the projection matrix from, so it is where a field of view
    // override has to be applied for the reticle projection to agree with the render.
    std::uintptr_t rvaGetFovAngle;

    // Return address of the ONE call to GetFOVAngle inside ULocalPlayer::CalcSceneView.
    // The override applies only there, leaving every FOV the game reads for its own
    // purposes - weapon zoom, camera modifiers, script - exactly as the game set it.
    std::uintptr_t rvaCalcSceneViewFovReturn;

    // APlayerController::PlayerCamera. The gate reads the FOV the projection matrix
    // is built from through it.
    std::uint32_t offPlayerCamera;

    // RVA of the GWorld pointer. Two readers: the menu gate walks
    // GWorld -> +0x2C0 -> +0x410 -> +0x41C to reach the UI manager and asks the main
    // menu and pause menu movie players whether they are open, and the collision clamp
    // loads it as the `this` for UWorld::SingleLineCheck. The second use is what names
    // it - AActor::execTrace and AActor::execFastTrace both load this exact global into
    // ECX before that call.
    std::uintptr_t rvaGWorld;

    // UWorld::SingleLineCheck(FCheckResult& Hit, AActor* SourceActor, const FVector& End,
    // const FVector& Start, DWORD TraceFlags, const FVector& Extent,
    // ULightComponent* SourceLight), __thiscall, ret 0x1C. The trace the lean clamp casts
    // to find out whether the head-tracked eye is about to move into the world.
    std::uintptr_t rvaSingleLineCheck;

    // UDisGFxMoviePlayerHUD's per-frame crosshair update, __thiscall(this, float dt).
    // It pushes the crosshair's current position into the Scaleform clip that carries
    // the crosshair art, so writing the aim point into the HUD just before it runs is
    // what puts the game's own crosshair under the shot.
    std::uintptr_t rvaCrosshairUpdate;
};

// Most-recent build first (diagnostic primary). The Steam build dated 2022-02-17
// (TimeDateStamp 0x620EAE07).
extern const BuildProfile kKnownProfiles[];
extern const int kKnownProfileCount;

// Returns the profile matching the running EXE, or nullptr when no profile
// matches (mod stays dormant - no hooks installed).
const BuildProfile* MatchRunningProfile();

}  // namespace DishonoredHeadTracking
