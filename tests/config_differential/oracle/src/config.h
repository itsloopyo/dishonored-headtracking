// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

#include <cstdint>

namespace DishonoredHeadTracking {

// The shipped defaults, in one place.
//
// These are the single source of truth for three readers that used to keep their own
// copies and could drift apart silently: the INI writer, the INI reader's per-key
// fallback, and the Config member initialisers below. A test can pin them because they
// are declared here rather than in an anonymous namespace in the .cpp.
constexpr bool  kDefaultEnableOnStartup = true;
constexpr int   kDefaultPort            = 4242;
constexpr int   kMinPort                = 1024;
constexpr int   kMaxPort                = 65535;
constexpr bool  kDefaultWorldSpaceYaw   = true;
constexpr bool  kDefaultMoveCrosshair   = true;
// 0 = render at whatever FOV the game asks for.
constexpr float kDefaultFov             = 0.0f;
constexpr float kDefaultSensitivity     = 1.0f;
constexpr bool  kDefaultInvert          = false;
constexpr float kDefaultLocalSmoothing  = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
constexpr float kDefaultRemoteSmoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

constexpr bool  kDefaultPositionEnabled = true;
constexpr float kDefaultPosSens         = 1.0f;
constexpr float kDefaultPosLimitX       = cameraunlock::PositionSettings{}.limit_x;
constexpr float kDefaultPosLimitY       = cameraunlock::PositionSettings{}.limit_y;
constexpr float kDefaultPosLimitZ       = cameraunlock::PositionSettings{}.limit_z;
constexpr float kDefaultPosLimitZBack   = cameraunlock::PositionSettings{}.limit_z_back;
constexpr float kDefaultPositionScale   = 100.0f;

// Head tracking moves the rendered eye off the player's own, so a lean toward a wall
// can carry it through the surface. The clamp traces the lean against the world and
// stops the eye short of what it would have entered.
constexpr bool  kDefaultCollision       = true;
// World units (cm) held between the eye and the surface it stopped at, measured along
// that surface's normal.
constexpr float kDefaultCollisionMargin = 20.0f;

constexpr int   kDefaultVkToggle        = 0x23; // VK_END
constexpr int   kDefaultVkCycleMode     = 0x21; // VK_PRIOR (Page Up)
constexpr int   kDefaultVkYawMode       = 0x22; // VK_NEXT (Page Down)
constexpr bool  kDefaultChord           = true;

struct Config {
    bool  enabled_on_startup = kDefaultEnableOnStartup;
    uint16_t udp_port = kDefaultPort;

    float sens_yaw = kDefaultSensitivity;
    float sens_pitch = kDefaultSensitivity;
    float sens_roll = kDefaultSensitivity;
    // Per-axis user inversion, for a tracker that genuinely reports an axis backwards.
    // The protocol-to-engine sign conversion is NOT carried here: the position axes are
    // mirrored and are converted at the engine boundary in camera_hook.cpp, so all three
    // of these mean the same thing and all three ship off.
    bool  invert_yaw = kDefaultInvert;
    bool  invert_pitch = kDefaultInvert;
    bool  invert_roll = kDefaultInvert;

    // Smoothing is picked per connection from the packet source address:
    // loopback senders get local_smoothing, remote network devices get
    // remote_smoothing. Both cover rotation and position.
    float local_smoothing = kDefaultLocalSmoothing;
    float remote_smoothing = kDefaultRemoteSmoothing;

    // The game draws its crosshair at screen centre, which is no longer where the game
    // is aiming once the view is head-tracked. This moves it to the aim point.
    bool move_crosshair = kDefaultMoveCrosshair;

    // true = horizon-locked (world-space) yaw, false = camera-local yaw.
    bool world_space_yaw = kDefaultWorldSpaceYaw;

    // Horizontal field of view, in degrees, at the game's default zoom. 0 leaves the
    // game's own FOV alone - Dishonored has an FOV slider in Options > Graphics, so this
    // is for the range that slider does not reach.
    float fov = kDefaultFov;

    // 6DOF positional tracking.
    bool  position_enabled = kDefaultPositionEnabled;
    float pos_sens_x = kDefaultPosSens;
    float pos_sens_y = kDefaultPosSens;
    float pos_sens_z = kDefaultPosSens;
    float pos_limit_x = kDefaultPosLimitX;
    float pos_limit_y = kDefaultPosLimitY;
    float pos_limit_z = kDefaultPosLimitZ;
    float pos_limit_z_back = kDefaultPosLimitZBack;
    bool  invert_pos_x = kDefaultInvert;
    bool  invert_pos_y = kDefaultInvert;
    bool  invert_pos_z = kDefaultInvert;
    // World units (cm) per metre of head translation. UE3 world is centimetres.
    float position_scale = kDefaultPositionScale;

    // Keeps the head-tracked eye out of the world. See kDefaultCollision.
    bool  collision_enabled = kDefaultCollision;
    float collision_margin = kDefaultCollisionMargin;

    int vk_toggle     = kDefaultVkToggle;
    int vk_cycle_mode = kDefaultVkCycleMode;
    int vk_yaw_mode   = kDefaultVkYawMode;
    bool chord_toggle = kDefaultChord;
    bool chord_cycle_mode = kDefaultChord;
    bool chord_yaw_mode = kDefaultChord;

    bool LoadOrCreate(const char* iniPath);
};

}
