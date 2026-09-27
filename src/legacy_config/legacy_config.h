// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

// The config reader of the last build that read DishonoredHeadTracking.ini in its
// pre-canonical layout, frozen so a player updating from any older build is converted exactly
// as that build read the file. Nothing in this folder is ever edited. Three things differ from
// the reader it was taken from: it fills this frozen copy of that build's Config and defaults
// rather than the runtime type, it never writes the file (a missing file reads as the
// defaults, which is what the old reader read from the file it created there), and it reports
// a refusal apart from an absent file. The helpers it called that core does not freeze are
// copied beside it: Clamp in config_sanitize.h, IsValidHotkeyCode, IsUsableFov and the
// default values in legacy_config.cpp.

#include "cameraunlock/config/legacy_import.h"

#include <cstdint>
#include <string>
#include <vector>

namespace DishonoredHeadTracking::legacy {

enum class ReadStatus {
    Read,
    // No file at the path. Config holds the defaults.
    Absent,
    // The old reader returned false and the mod did not start.
    Refused,
};

struct ReadResult {
    ReadStatus status = ReadStatus::Read;
    // For Refused, what the old reader logged.
    std::string reason;
};

struct Config {
    bool  enabled_on_startup = true;
    std::uint16_t udp_port = 4242;

    float sens_yaw = 1.0f;
    float sens_pitch = 1.0f;
    float sens_roll = 1.0f;
    bool  invert_yaw = false;
    bool  invert_pitch = false;
    bool  invert_roll = false;

    float local_smoothing = static_cast<float>(0.0);
    float remote_smoothing = static_cast<float>(0.15);

    bool move_crosshair = true;

    bool world_space_yaw = true;

    float fov = 0.0f;

    bool  position_enabled = true;
    float pos_sens_x = 1.0f;
    float pos_sens_y = 1.0f;
    float pos_sens_z = 1.0f;
    float pos_limit_x = 0.30f;
    float pos_limit_y = 0.20f;
    float pos_limit_z = 0.40f;
    float pos_limit_z_back = 0.10f;
    bool  invert_pos_x = false;
    bool  invert_pos_y = false;
    bool  invert_pos_z = false;
    float position_scale = 100.0f;

    bool  collision_enabled = true;
    float collision_margin = 20.0f;

    int vk_toggle     = 0x23; // VK_END
    int vk_cycle_mode = 0x21; // VK_PRIOR (Page Up)
    int vk_yaw_mode   = 0x22; // VK_NEXT (Page Down)
    bool chord_toggle = true;
    bool chord_cycle_mode = true;
    bool chord_yaw_mode = true;

    // Call on a default-constructed Config.
    ReadResult Read(const char* iniPath);
};

// Every section and key Read reads, in the order it reads them.
std::vector<cameraunlock::config::LegacyKey> ReadKeys();

}
