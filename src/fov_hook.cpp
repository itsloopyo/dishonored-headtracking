// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "fov_hook.h"

#include "fov_range.h"
#include "heap_ptr.h"
#include "hook_install.h"
#include "logging.h"
#include "xmm_guard.h"

#include "cameraunlock/math/angle_utils.h"

#include <atomic>
#include <cmath>
#include <cstdint>

namespace DishonoredHeadTracking {

namespace {

// APlayerController::GetFOVAngle, __thiscall(this), returning the FOV in degrees on the
// x87 stack. Modelled as __fastcall with a dummy edx so MinHook can detour it: arg0 ->
// ecx (this), arg1 -> edx (unused), and the original takes no stack arguments.
using GetFOVAngle_t = float(__fastcall*)(void* thisptr, void* edx);

GetFOVAngle_t g_original = nullptr;

std::uintptr_t g_sceneViewFovReturn = 0;
std::uint32_t g_offPlayerCamera = 0;

// The configured FOV in degrees, or 0 for "leave the game's own FOV alone". Set only
// once the detour is live, so EffectiveFov cannot report an override the renderer is
// not applying - which means the store races the game threads already running through
// the detour and through the camera hook's EffectiveFov, so it has to be atomic.
std::atomic<float> g_fov{0.0f};

// ACamera::DefaultFOV, and APlayerController::DefaultFOV for the frames where the
// controller has not spawned its camera yet. CalcSceneView picks between the two by
// exactly this rule when it turns the FOV into a LOD distance factor, so basing the
// override on the same pair keeps it anchored to the engine's own idea of "unzoomed".
constexpr std::uint32_t kCamDefaultFov        = 0x254;
constexpr std::uint32_t kControllerDefaultFov = 0x3b4;

// The arithmetic lives in fov_range.h so it can be exercised without a running game.
// This wrapper adds the one thing that needs the process: saying so, once, when the
// camera hands back a pair the override cannot be applied to.
float ApplyOverride(float gameFov, float defaultFov) {
    const float configuredFov = g_fov.load(std::memory_order_relaxed);
    if (configuredFov > 0.0f && (!std::isfinite(gameFov) || !IsUsableFov(defaultFov))) {
        static bool s_warned = false;
        if (!s_warned) {
            s_warned = true;
            Log::Line("WARN: FOV override skipped: the camera reports fov=%.1f "
                      "default=%.1f, which is not a usable pair", gameFov, defaultFov);
        }
    }
    return ApplyFovOffset(gameFov, defaultFov, configuredFov);
}

// The controller's PlayerCamera is spawned lazily and cleared on a level change, so it
// is null for real frames and can hold a half-written value on the frame it is being
// assigned. Anything that is not a plausible heap object reads the controller's own
// DefaultFOV - the same answer the null case already gave - rather than pulling a float
// through it, which is a wild read on the game thread.
float ControllerDefaultFov(const void* controller) {
    const auto* c = static_cast<const std::uint8_t*>(controller);
    const std::uint32_t cam = *reinterpret_cast<const std::uint32_t*>(c + g_offPlayerCamera);
    if (!LooksLikeHeapPtr(cam)) {
        return *reinterpret_cast<const float*>(c + kControllerDefaultFov);
    }
    return *reinterpret_cast<const float*>(
        reinterpret_cast<const std::uint8_t*>(cam) + kCamDefaultFov);
}

float __cdecl FovImpl(void* thisptr, void* retaddr) {
    const float gameFov = g_original(thisptr, nullptr);

    // The projection matrix is the only thing that gets the override. The same function
    // answers the game's own FOV questions, and those must keep seeing the FOV the game
    // set for itself.
    if (!thisptr || reinterpret_cast<std::uintptr_t>(retaddr) != g_sceneViewFovReturn) {
        return gameFov;
    }

    const float fov = ApplyOverride(gameFov, ControllerDefaultFov(thisptr));

    static bool s_confirmed = false;
    if (!s_confirmed && fov != gameFov) {
        s_confirmed = true;
        Log::Line("Field of view override live: the scene view renders at %.1f where the "
                  "game asked for %.1f", fov, gameFov);
    }
    return fov;
}

using FovImpl_t = float(__cdecl*)(void*, void*);
FovImpl_t g_implPtr = &FovImpl;

// Stack at entry: [esp] return address, no arguments; ecx holds the controller. The
// return address becomes the impl's second argument - it is what tells CalcSceneView's
// projection apart from every other caller. The impl's result comes back on the x87
// stack, which nothing below touches, and the original's plain `ret` is preserved.
// See xmm_guard.h for the register preservation.
__declspec(naked) void __fastcall Detour(void*, void*) {
    DHT_DETOUR_ENTER
    __asm {
        push dword ptr [ebp+4]
        push ecx
        call dword ptr [g_implPtr]
        add  esp, 8
    }
    DHT_DETOUR_RESTORE_XMM
    __asm {
        mov  esp, ebp
        pop  ebp
        ret
    }
}

}  // namespace

float EffectiveFov(float gameFov, const void* camera) {
    if (!camera) {
        return gameFov;
    }
    return ApplyOverride(gameFov, *reinterpret_cast<const float*>(
                                      static_cast<const std::uint8_t*>(camera) +
                                      kCamDefaultFov));
}

float UnzoomedFov(const void* camera) {
    if (!camera) {
        return 0.0f;
    }
    const float defaultFov = *reinterpret_cast<const float*>(
        static_cast<const std::uint8_t*>(camera) + kCamDefaultFov);
    return ApplyOverride(defaultFov, defaultFov);
}

bool InstallFovHook(const BuildProfile& profile, std::uintptr_t moduleBase,
                    const Config& cfg) {
    g_sceneViewFovReturn = moduleBase + profile.rvaCalcSceneViewFovReturn;
    g_offPlayerCamera = profile.offPlayerCamera;

    const std::uintptr_t target = moduleBase + profile.rvaGetFovAngle;
    if (!InstallDetour(target, reinterpret_cast<void*>(&Detour),
                       reinterpret_cast<void**>(&g_original),
                       "APlayerController::GetFOVAngle")) {
        return false;
    }

    g_fov.store(cfg.fov, std::memory_order_relaxed);
    Log::Line("FOV hook installed on APlayerController::GetFOVAngle @ 0x%08X: the scene "
              "view will render at %.1f degrees where the game is at its default",
              static_cast<unsigned>(target), cfg.fov);
    return true;
}

}  // namespace DishonoredHeadTracking
