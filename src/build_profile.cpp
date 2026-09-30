// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

#include <windows.h>

#include <cstring>

#include "logging.h"
#include "runtime_discovery.h"

namespace DishonoredHeadTracking {
static const BuildProfile kSteamProfile_20220217 = {
    "steam-win32-20220217",
    {0x620EAE07u, 0x01207000u, 0x0113BB15u},
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
    0x2C5CF3u,
};
const BuildProfile kKnownProfiles[] = {kSteamProfile_20220217};
const int kKnownProfileCount = static_cast<int>(sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]));
namespace {
RuntimeLayout layout;
BuildProfile discovered;
bool CrossCheck(const BuildProfile& found, const RuntimeLayout& live, const BuildProfile& known) {
    struct Check {
        std::uintptr_t found, expected;
        const char* name;
    };
    const Check checks[] = {
        {found.rvaGetPlayerViewPoint, known.rvaGetPlayerViewPoint, "viewpoint"},
        {found.rvaCalcSceneViewReturn, known.rvaCalcSceneViewReturn, "scene-view call"},
        {found.rvaDeProjectCaller, known.rvaDeProjectCaller, "deprojection caller"},
        {found.rvaStreamingCaller, known.rvaStreamingCaller, "streaming caller"},
        {found.rvaGetFovAngle, known.rvaGetFovAngle, "FOV getter"},
        {found.rvaCalcSceneViewFovReturn, known.rvaCalcSceneViewFovReturn, "scene FOV call"},
        {found.offPlayerCamera, known.offPlayerCamera, "player camera field"},
        {found.rvaGWorld, known.rvaGWorld, "world global"},
        {found.rvaSingleLineCheck, known.rvaSingleLineCheck, "collision query"},
        {found.rvaCrosshairUpdate, known.rvaCrosshairUpdate, "crosshair update"},
        {found.rvaViewportSceneViewCaller, known.rvaViewportSceneViewCaller, "viewport caller"},
        {live.worldInfo, 0x2c0, "world info"},
        {live.game, 0x410, "game info"},
        {live.ui, 0x41c, "UI manager"},
        {live.mainMenu, 0x2d8, "main menu"},
        {live.pauseMenu, 0x2ec, "pause menu"},
        {live.movieOpen, 0xc4, "movie-open field"},
        {live.movieOpenMask, 1, "movie-open mask"},
        {live.camDefaultFov, 0x254, "camera default FOV"},
        {live.controllerDefaultFov, 0x3b4, "controller default FOV"},
        {live.lockedBits, 0x258, "locked-FOV field"},
        {live.lockedMask, 1, "locked-FOV mask"},
        {live.lockedFov, 0x25c, "locked FOV"},
        {live.povFov, 0x348, "cached FOV"},
        {live.aspectBits, 0x258, "aspect flag field"},
        {live.aspectMask, 2, "aspect mask"},
        {live.aspect, 0x260, "constrained aspect"},
        {live.hudViewportW, 0x1e0, "HUD width"},
        {live.hudViewportH, 0x1e4, "HUD height"},
        {live.hudDotX, 0x3d8, "crosshair X"},
        {live.hudDotY, 0x3dc, "crosshair Y"},
        {live.hitSize, 0x4c, "collision result size"},
        {live.hitActor, 4, "collision actor"},
        {live.hitTime, 0x20, "collision time"},
        {live.hitNormal, 0x14, "collision normal"},
    };
    for (const auto& check : checks)
        if (check.found != check.expected) {
            Log::Line(
                "ERROR: discovery disagrees with %s for %s (0x%X, expected 0x%X); staying dormant",
                known.name, check.name, static_cast<unsigned>(check.found),
                static_cast<unsigned>(check.expected));
            return false;
        }
    return true;
}
}  // namespace
const RuntimeLayout& LiveLayout() { return layout; }
const BuildProfile* MatchRunningProfile() {
    auto module = GetModuleHandleA(kGameExeName);
    cameraunlock::memory::PeFingerprint running{};
    if (!module || !cameraunlock::memory::ReadPeFingerprint(module, running)) {
        Log::Line("ERROR: could not read Dishonored PE fingerprint");
        return nullptr;
    }
    const BuildProfile* known = nullptr;
    for (const auto& profile : kKnownProfiles)
        if (running.Matches(profile.fingerprint)) {
            known = &profile;
            break;
        }
    auto start = GetTickCount64(), nextReport = start;
    std::string error;
    do {
        std::vector<std::uint8_t> image;
        BuildProfile candidate{};
        RuntimeLayout fields{};
        if (!SnapshotGameImage(module, image, error)) {
            Log::Line("ERROR: discovery snapshot: %s", error.c_str());
            return nullptr;
        }
        if (DiscoverRuntime(GetCurrentProcess(),
                            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(module)),
                            image, candidate, fields, error)) {
            if (known && !CrossCheck(candidate, fields, *known)) return nullptr;
            candidate.name = "runtime-discovery";
            candidate.fingerprint = running;
            discovered = candidate;
            layout = fields;
            Log::Line(
                "Runtime discovery validated camera, FOV, collision, HUD and menu layouts (%s)",
                known ? known->name : "unlisted fingerprint");
            Log::Line(
                "Discovery: viewpoint=0x%X viewport-caller=0x%X FOV=0x%X trace=0x%X HUD=0x%X "
                "names=0x%X",
                static_cast<unsigned>(candidate.rvaGetPlayerViewPoint),
                static_cast<unsigned>(candidate.rvaViewportSceneViewCaller),
                static_cast<unsigned>(candidate.rvaGetFovAngle),
                static_cast<unsigned>(candidate.rvaSingleLineCheck),
                static_cast<unsigned>(candidate.rvaCrosshairUpdate), fields.names);
            return &discovered;
        }
        if (error.rfind("reflection:", 0) != 0 &&
            error.find("not initialized") == std::string::npos) {
            Log::Line("ERROR: discovery rejected game layout: %s; staying dormant", error.c_str());
            return nullptr;
        }
        auto now = GetTickCount64();
        if (now >= nextReport) {
            Log::Line("Discovery waiting for initialized game metadata: %s", error.c_str());
            nextReport = now + 5000;
        }
        Sleep(250);
    } while (GetTickCount64() - start < 60000);
    Log::Line("ERROR: discovery startup deadline reached: %s; staying dormant", error.c_str());
    return nullptr;
}
}  // namespace DishonoredHeadTracking
