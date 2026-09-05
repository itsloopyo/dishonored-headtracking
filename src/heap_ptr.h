// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <windows.h>

#include <cstdint>

namespace DishonoredHeadTracking {

// The span a 32-bit UE3 heap allocation lands in, and the alignment every object in it
// has. A value outside this is not a pointer any hook may follow.
//
// Shared rather than per-hook: the camera hook and the FOV hook both walk from a
// controller to its PlayerCamera, and a field that is null on one frame and
// half-initialised on the next has to be rejected identically by both. The FOV hook
// checked only for null once, which left a partially constructed controller reading
// a float through whatever the field happened to hold.
constexpr std::uint32_t kMinHeapAddress = 0x01000000u;

// The top of the span comes from the OS, never a constant. Dishonored.exe is linked
// LARGE_ADDRESS_AWARE (Characteristics 0x0122), so on 64-bit Windows the process owns
// the full 4 GB user range and allocations routinely land above 2 GB. A hard-coded
// 0x7F000000 ceiling called every one of those a wild pointer, which would have failed
// the camera gate (head tracking off for the rest of the session), the menu walk (the
// gate fails open, so tracking would keep running in the menus) and the FOV hook's
// DefaultFOV read - all silently, and all more likely the longer the session runs.
// Every caller dereferences `ptr + offset` without validating again - the menu walk
// reaches +0x41C, the camera reads +0x348 - so the accepted range stops one page short
// of the true ceiling. Otherwise a value in the last page passes here and faults on the
// very next read.
constexpr std::uint32_t kHeapCeilingHeadroom = 0x10000u;

inline std::uint32_t MaxHeapAddress() {
    static const std::uint32_t kMax = [] {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return static_cast<std::uint32_t>(
                   reinterpret_cast<std::uintptr_t>(si.lpMaximumApplicationAddress)) -
               kHeapCeilingHeadroom;
    }();
    return kMax;
}

inline bool LooksLikeHeapPtr(std::uint32_t v) {
    return v >= kMinHeapAddress && v <= MaxHeapAddress() && (v & 3u) == 0;
}

}  // namespace DishonoredHeadTracking
