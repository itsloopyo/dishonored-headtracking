// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include "legacy_config/legacy_config.h"
#include "path_utils.h"
#include "ue3_math.h"

#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace DishonoredHeadTracking {

namespace {

using cameraunlock::config::DropRule;
using cameraunlock::config::DroppedValue;
using cameraunlock::config::ImportResult;
using cameraunlock::config::LegacyInput;
using cameraunlock::config::LegacyPoseShaping;
using cameraunlock::config::PoseShapingValue;
using cameraunlock::input::KeyModifiers;

constexpr const char* kFovExpectation = "0, or an angle from 20 to 170";

// A legacy hotkey code and its Ctrl+Shift chord switch as one key list: the code's binding,
// then the chord.
std::string KeyList(int vk, bool chord, char letter, const char* key, std::vector<DroppedValue>& dropped) {
    std::string list = cameraunlock::config::LegacyVirtualKeyToBindings(vk, "Hotkeys", key, dropped);
    if (chord) {
        const std::string chordKey =
            cameraunlock::input::FormatKeyBindings({{KeyModifiers::kCtrl | KeyModifiers::kShift, letter}});
        list += (list.empty() ? "" : ", ") + chordKey;
    }
    return list;
}

ImportResult Import(const LegacyInput& input, Config& out) {
    // The published build opened the file by the ANSI path it built itself, not the one the
    // owner derives, so the import builds it the same way.
    const std::string ansiPath = LegacyAnsiPath(input.path);
    if (ansiPath.empty()) {
        return ImportResult::Refused(
            "its path has no ANSI form within MAX_PATH, so the version that wrote this file could "
            "not open it and did not start");
    }

    legacy::Config c;
    const legacy::ReadResult read = c.Read(ansiPath.c_str());
    if (read.status == legacy::ReadStatus::Refused) {
        return ImportResult::Refused(read.reason);
    }

    std::vector<DroppedValue> dropped;
    std::vector<PoseShapingValue> shaping;

    out.enable_on_startup = c.enabled_on_startup;
    out.udp_port = c.udp_port;
    out.world_space_yaw = c.world_space_yaw;

    // [Position] Enabled chose only the startup mode: the cycle key reached every mode
    // either way.
    const cameraunlock::TrackingModeChannels mode = cameraunlock::EncodeTrackingMode(
        c.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                           : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = mode.rotation_enabled;
    out.position_enabled = mode.position_enabled;

    out.local_smoothing = c.local_smoothing;
    out.position.local_smoothing = c.local_smoothing;
    out.remote_smoothing = c.remote_smoothing;
    out.position.remote_smoothing = c.remote_smoothing;

    // The old file had one vertical limit, which the old runtime applied both ways. dev read the
    // limits with no upper bound, and the rows take 0 to 10, so a limit above 10 imports as 10
    // (N4). The comparison below takes the value as read: the player set it.
    using cameraunlock::config::LegacyClampToRange;
    using cameraunlock::config::schema::Concept;
    out.position.limit_x = LegacyClampToRange<Concept::PositionLimitX>(c.pos_limit_x, "Position", "LimitX", dropped);
    out.position.limit_y = LegacyClampToRange<Concept::PositionLimitY>(c.pos_limit_y, "Position", "LimitY", dropped);
    out.position.limit_y_down = out.position.limit_y;
    out.position.limit_z = LegacyClampToRange<Concept::PositionLimitZ>(c.pos_limit_z, "Position", "LimitZ", dropped);
    out.position.limit_z_back =
        LegacyClampToRange<Concept::PositionLimitZBack>(c.pos_limit_z_back, "Position", "LimitZBack", dropped);

    out.collision_enabled = c.collision_enabled;
    out.lean_clamp.skin = c.collision_margin;

    out.fov = c.fov;

    // Every rotation sensitivity and inversion and every position sensitivity and inversion
    // shipped at identity, so nothing folds and a value the player changed is dropped. The
    // unit scale shipped at the centimetres per metre the camera hook now applies itself.
    LegacyPoseShaping(c.sens_yaw, 1.0f, "Sensitivity", "Yaw", shaping, dropped);
    LegacyPoseShaping(c.sens_pitch, 1.0f, "Sensitivity", "Pitch", shaping, dropped);
    LegacyPoseShaping(c.sens_roll, 1.0f, "Sensitivity", "Roll", shaping, dropped);
    LegacyPoseShaping(c.invert_yaw, false, "Sensitivity", "InvertYaw", shaping, dropped);
    LegacyPoseShaping(c.invert_pitch, false, "Sensitivity", "InvertPitch", shaping, dropped);
    LegacyPoseShaping(c.invert_roll, false, "Sensitivity", "InvertRoll", shaping, dropped);
    LegacyPoseShaping(c.pos_sens_x, 1.0f, "Position", "SensitivityX", shaping, dropped);
    LegacyPoseShaping(c.pos_sens_y, 1.0f, "Position", "SensitivityY", shaping, dropped);
    LegacyPoseShaping(c.pos_sens_z, 1.0f, "Position", "SensitivityZ", shaping, dropped);
    LegacyPoseShaping(c.invert_pos_x, false, "Position", "InvertX", shaping, dropped);
    LegacyPoseShaping(c.invert_pos_y, false, "Position", "InvertY", shaping, dropped);
    LegacyPoseShaping(c.invert_pos_z, false, "Position", "InvertZ", shaping, dropped);
    LegacyPoseShaping(c.position_scale, kWorldUnitsPerMetre, "Position", "PositionScale", shaping, dropped);

    // The game's crosshair now always follows the aim.
    if (!c.move_crosshair) dropped.push_back({DropRule::Reticle, "General", "MoveCrosshair", "false"});

    out.toggle_key_name = KeyList(c.vk_toggle, c.chord_toggle, 'Y', "Toggle", dropped);
    out.cycle_tracking_mode_key_name = KeyList(c.vk_cycle_mode, c.chord_cycle_mode, 'G', "CycleMode", dropped);
    out.yaw_mode_key_name = KeyList(c.vk_yaw_mode, c.chord_yaw_mode, 'H', "YawMode", dropped);

    // A setting the player never changed from what the published build shipped follows
    // Defaults.ini. LimitY stood for both vertical bounds, and each hotkey for its code and its
    // chord switch together. The old build had no release pacing setting.
    const legacy::Config shipped;
    cameraunlock::config::LegacyFollowsDefaultsIni follows;
    follows.Setting(Concept::UdpPort, c.udp_port, shipped.udp_port);
    follows.Setting(Concept::EnableOnStartup, c.enabled_on_startup, shipped.enabled_on_startup);
    follows.Setting(Concept::WorldSpaceYaw, c.world_space_yaw, shipped.world_space_yaw);
    follows.TrackingMode(c.position_enabled, shipped.position_enabled);
    follows.Setting(Concept::LocalSmoothing, c.local_smoothing, shipped.local_smoothing);
    follows.Setting(Concept::RemoteSmoothing, c.remote_smoothing, shipped.remote_smoothing);
    follows.Setting(Concept::PositionLimitX, c.pos_limit_x, shipped.pos_limit_x);
    follows.Setting(Concept::PositionLimitY, c.pos_limit_y, shipped.pos_limit_y);
    follows.Setting(Concept::PositionLimitYDown, c.pos_limit_y, shipped.pos_limit_y);
    follows.Setting(Concept::PositionLimitZ, c.pos_limit_z, shipped.pos_limit_z);
    follows.Setting(Concept::PositionLimitZBack, c.pos_limit_z_back, shipped.pos_limit_z_back);
    follows.Setting(Concept::CollisionEnabled, c.collision_enabled, shipped.collision_enabled);
    follows.Setting(Concept::ToggleKey, c.vk_toggle == shipped.vk_toggle && c.chord_toggle == shipped.chord_toggle);
    follows.Setting(Concept::CycleTrackingModeKey,
                    c.vk_cycle_mode == shipped.vk_cycle_mode && c.chord_cycle_mode == shipped.chord_cycle_mode);
    follows.Setting(Concept::YawModeKey, c.vk_yaw_mode == shipped.vk_yaw_mode && c.chord_yaw_mode == shipped.chord_yaw_mode);

    return read.status == legacy::ReadStatus::Absent
               ? ImportResult::Absent(std::move(dropped), std::move(shaping), follows.Concepts())
               : ImportResult::Imported(std::move(dropped), std::move(shaping), follows.Concepts());
}

}  // namespace

cameraunlock::config::CodecParseResult<float> FovCodec::Parse(std::string_view text) const {
    cameraunlock::config::CodecParseResult<float> read = angle_.Parse(text);
    if (read.ok() && read.value != 0.0f && read.value < kMinFovDegrees) {
        return {0.0f, kFovExpectation};
    }
    if (!read.ok()) read.error = kFovExpectation;
    return read;
}

std::string FovCodec::Render(float value) const {
    if (value != 0.0f && value < kMinFovDegrees) {
        throw std::invalid_argument("[Camera] Fov " + std::to_string(value) + " is neither 0 nor 20 to 170");
    }
    return angle_.Render(value);
}

cameraunlock::config::ConfigTable<Config> MakeConfigTable() {
    using cameraunlock::config::schema::Concept;
    cameraunlock::config::ConfigTable<Config> table = cameraunlock::config::HeadTrackingConfigTable<Config>(
        {Concept::UdpPort, Concept::EnableOnStartup, Concept::WorldSpaceYaw, Concept::RotationEnabled,
         Concept::LocalSmoothing, Concept::RemoteSmoothing, Concept::PositionEnabled, Concept::PositionLimitX,
         Concept::PositionLimitY, Concept::PositionLimitYDown, Concept::PositionLimitZ, Concept::PositionLimitZBack,
         Concept::CollisionEnabled, Concept::CollisionMargin, Concept::ToggleKey, Concept::CycleTrackingModeKey,
         Concept::YawModeKey});
    table.Select(Concept::WorldSpaceYaw).Writable()
        .Select(Concept::RotationEnabled).Writable()
        .Select(Concept::PositionEnabled).Writable();
    table.Select(Concept::CollisionMargin)
        .Comment("How far, in centimetres, the view is held off a wall when you lean into it.");
    table.Local("Camera", "Fov", &Config::fov, FovCodec{},
                "Horizontal field of view in degrees at the game's default zoom: 0, or 20 to 170.\n"
                "0 keeps the game's own FOV - Dishonored has an FOV slider in Options > Graphics,\n"
                "so set this only to go past what that slider offers. The difference between this\n"
                "and the game's default is added to whatever FOV the game asks for, so weapon zooms\n"
                "still zoom by the same amount.");
    return table;
}

cameraunlock::config::LegacyImport<Config> MakeLegacyImport() {
    return {&Import, legacy::ReadKeys()};
}

cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder,
                                                                        cameraunlock::config::DefaultsFile defaults) {
    const auto wide = [](const char* name) { return std::wstring(name, name + std::char_traits<char>::length(name)); };
    cameraunlock::config::ConfigOwnerOptions<Config> options;
    options.path = folder + wide(kConfigFileName);
    options.legacy_path = folder + wide(kLegacyConfigFileName);
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.header.display_name = kConfigDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

}  // namespace DishonoredHeadTracking
