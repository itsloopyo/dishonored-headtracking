// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include <algorithm>
#include <cstring>

#include "runtime_discovery.h"

namespace DishonoredHeadTracking {
bool SnapshotGameImage(HMODULE module, std::vector<std::uint8_t>& image, std::string& error) {
    auto base = reinterpret_cast<std::uintptr_t>(module);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    if (!ReadLive(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
        !ReadLive(base + static_cast<std::uint32_t>(dos.e_lfanew), nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt.OptionalHeader.SizeOfImage < 4096 || nt.OptionalHeader.SizeOfImage > 512 * 1024 * 1024 ||
        nt.OptionalHeader.SizeOfImage > UINTPTR_MAX - base) {
        error = "invalid PE32 image bounds";
        return false;
    }
    image.assign(nt.OptionalHeader.SizeOfImage, 0);
    std::size_t offset = 0;
    while (offset < image.size()) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(base + offset), &region, sizeof(region))) {
            error = "VirtualQuery failed: " + std::to_string(GetLastError());
            return false;
        }
        auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (end <= base + offset) {
            error = "memory region has invalid bounds";
            return false;
        }
        auto count = std::min<std::size_t>(image.size() - offset, end - (base + offset));
        if (region.State == MEM_COMMIT && !(region.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
            (region.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
            SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<const void*>(base + offset),
                                   image.data() + offset, count, &got) ||
                got != count) {
                error = "image snapshot read failed: " + std::to_string(GetLastError());
                return false;
            }
        }
        offset += count;
    }
    return true;
}
bool HasLiveClass(const void* object, std::uint32_t wanted) {
    if (!object || !wanted) return false;
    std::uint32_t cls = 0;
    if (!ReadLive(reinterpret_cast<std::uintptr_t>(object) + 0x30, cls)) return false;
    for (unsigned i = 0; cls && i < 32; ++i) {
        if (cls == wanted) return true;
        if (!ReadLive(cls + 0x44, cls)) return false;
    }
    return false;
}
bool ReadMenuState(const void* controller, bool& open) {
    const auto& l = LiveLayout();
    std::uint32_t world = 0, game = 0, ui = 0;
    if (!HasLiveClass(controller, l.pcClass) ||
        !ReadLive(reinterpret_cast<std::uintptr_t>(controller) + l.actorWorldInfo, world) ||
        !HasLiveClass(reinterpret_cast<const void*>(world), l.worldInfoClass) ||
        !ReadLive(world + l.game, game) ||
        !HasLiveClass(reinterpret_cast<const void*>(game), l.gameClass) ||
        !ReadLive(game + l.ui, ui) || !HasLiveClass(reinterpret_cast<const void*>(ui), l.uiClass))
        return false;
    open = false;
    for (auto slot : {l.mainMenu, l.pauseMenu}) {
        std::uint32_t movie = 0, bits = 0;
        if (!ReadLive(ui + slot, movie)) return false;
        if (!movie) continue;
        if (!HasLiveClass(reinterpret_cast<const void*>(movie), l.movieClass) ||
            !ReadLive(movie + l.movieOpen, bits))
            return false;
        open |= (bits & l.movieOpenMask) != 0;
    }
    return true;
}
}  // namespace DishonoredHeadTracking
