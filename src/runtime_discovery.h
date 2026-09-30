// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "build_profile.h"
namespace DishonoredHeadTracking {
struct RuntimeLayout {
    std::uint32_t names = 0, slot = 0, worldInfo = 0, controllerList = 0, nextController = 0,
                  player = 0, localPlayerClass = 0;
    std::uint32_t actorWorldInfo = 0, game = 0, ui = 0, mainMenu = 0, pauseMenu = 0, movieOpen = 0,
                  movieOpenMask = 0;
    std::uint32_t camDefaultFov = 0, controllerDefaultFov = 0, lockedBits = 0, lockedMask = 0,
                  lockedFov = 0, povFov = 0;
    std::uint32_t aspectBits = 0, aspectMask = 0, aspect = 0, povLocation = 0, povRotation = 0;
    std::uint32_t hudViewportW = 0, hudViewportH = 0, hudDotX = 0, hudDotY = 0;
    std::uint32_t hitSize = 0, hitActor = 0, hitTime = 0, hitNormal = 0;
    std::uint32_t pcClass = 0, cameraClass = 0, hudClass = 0, worldClass = 0, worldInfoClass = 0,
                  gameClass = 0, uiClass = 0, movieClass = 0;
};
bool DiscoverRuntime(HANDLE process, std::uint32_t base, const std::vector<std::uint8_t>& image,
                     BuildProfile& profile, RuntimeLayout& layout, std::string& error);
bool SnapshotGameImage(HMODULE module, std::vector<std::uint8_t>& image, std::string& error);
const RuntimeLayout& LiveLayout();
bool HasLiveClass(const void* object, std::uint32_t cls);
bool ReadMenuState(const void* controller, bool& open);
template <class T>
bool ReadLive(std::uintptr_t address, T& value) {
    SIZE_T got = 0;
    return address >= 0x10000 && address <= UINTPTR_MAX - sizeof(T) &&
           ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), &value,
                             sizeof(T), &got) &&
           got == sizeof(T);
}
}  // namespace DishonoredHeadTracking
