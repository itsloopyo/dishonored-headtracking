// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

namespace DishonoredHeadTracking {

// Creates and enables one MinHook detour, initialising MinHook if this is the first
// hook of the process. @p what names the detoured function for the log.
//
// Either the detour is live and @p original holds the trampoline, or nothing is left
// installed: a failure to enable removes the hook it just created rather than leaving a
// created-but-disabled entry behind in MinHook's table.
//
// There is no matching uninstall. The module is pinned for the life of the process (see
// PinSelf in dllmain.cpp), so a hook that installs stays installed and valid until exit.
bool InstallDetour(std::uintptr_t target, void* detour, void** original, const char* what);

}  // namespace DishonoredHeadTracking
