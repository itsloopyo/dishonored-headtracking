// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hook_install.h"

#include "logging.h"

#include "MinHook.h"

namespace DishonoredHeadTracking {

bool InstallDetour(std::uintptr_t target, void* detour, void** original, const char* what) {
    // Whichever hook installs first initialises MinHook; the rest find it up already.
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        Log::Line("ERROR: MH_Initialize failed for %s", what);
        return false;
    }

    void* addr = reinterpret_cast<void*>(target);
    if (MH_CreateHook(addr, detour, original) != MH_OK) {
        Log::Line("ERROR: MH_CreateHook on %s failed at 0x%08X", what,
                  static_cast<unsigned>(target));
        return false;
    }
    if (MH_EnableHook(addr) != MH_OK) {
        Log::Line("ERROR: MH_EnableHook on %s failed at 0x%08X", what,
                  static_cast<unsigned>(target));
        MH_RemoveHook(addr);
        *original = nullptr;
        return false;
    }
    return true;
}

}  // namespace DishonoredHeadTracking
