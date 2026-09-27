// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "fov_range.h"

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/value_codecs.h"

#include <string>
#include <string_view>

namespace DishonoredHeadTracking {

constexpr const char* kConfigFileName = "CameraUnlock.ini";
// The file every build before the canonical format read, beside kConfigFileName. Imported once
// while kConfigFileName is absent, and never written.
constexpr const char* kLegacyConfigFileName = "DishonoredHeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it.
constexpr const char* kConfigDisplayName = "Dishonored";

// Core's config with this game's defaults.
struct Config : cameraunlock::HeadTrackingConfig {
    // Horizontal field of view, in degrees, at the game's default zoom. 0 leaves the game's own
    // FOV alone - Dishonored has an FOV slider in Options > Graphics, so this is for the range
    // that slider does not reach.
    float fov = 0.0f;

    Config() {
        // World units (cm) held between the eye and the surface it stopped at, measured along
        // that surface's normal.
        lean_clamp.skin = 20.0f;
    }
};

// [Camera] Fov: 0, or an angle inside the range fov_range.h keeps the projection usable over.
class FovCodec {
public:
    using Value = float;

    cameraunlock::config::CodecParseResult<float> Parse(std::string_view text) const;
    // Throws std::invalid_argument for a value Parse would not read back.
    std::string Render(float value) const;
    bool Equal(float a, float b) const { return angle_.Equal(a, b); }

private:
    cameraunlock::config::FloatCodec angle_{0.0f, kMaxFovDegrees};
};

// The rows of CameraUnlock.ini. Only the tracking mode pair and WorldSpaceYaw are Writable:
// the mode and yaw hotkeys save the player's choice, and End changes the session only.
cameraunlock::config::ConfigTable<Config> MakeConfigTable();

// DishonoredHeadTracking.ini as the builds before the canonical format read it
// (legacy_config/), mapped into Config.
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for the files in `folder` (with its trailing separator): the settings in
// CameraUnlock.ini, imported once from DishonoredHeadTracking.ini. The mod passes
// DefaultsFile::PerUser() and a test a scratch file.
cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder,
                                                                        cameraunlock::config::DefaultsFile defaults);

}  // namespace DishonoredHeadTracking
