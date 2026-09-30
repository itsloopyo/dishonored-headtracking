// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

#include "logging.h"

#include <windows.h>

namespace DishonoredHeadTracking {

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
    0x2C5CF3u,
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
            Log::Line("Dishonored.exe has a different size or checksum from the known profile. "
                      "Staying dormant.");
            break;
    }
    return nullptr;
}

}  // namespace DishonoredHeadTracking
