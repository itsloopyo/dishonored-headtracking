// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "legacy_config/legacy_config.h"

#include "legacy_config/config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

#include <windows.h>

#include <cmath>
#include <string>

namespace DishonoredHeadTracking::legacy {

namespace {

constexpr bool  kDefaultEnableOnStartup = true;
constexpr int   kDefaultPort            = 4242;
constexpr int   kMinPort                = 1024;
constexpr int   kMaxPort                = 65535;
constexpr bool  kDefaultWorldSpaceYaw   = true;
constexpr bool  kDefaultMoveCrosshair   = true;
constexpr float kDefaultFov             = 0.0f;
constexpr float kDefaultSensitivity     = 1.0f;
constexpr bool  kDefaultInvert          = false;
constexpr float kDefaultLocalSmoothing  = static_cast<float>(0.0);
constexpr float kDefaultRemoteSmoothing = static_cast<float>(0.15);
constexpr bool  kDefaultPositionEnabled = true;
constexpr float kDefaultPosSens         = 1.0f;
constexpr float kDefaultPosLimitX       = 0.30f;
constexpr float kDefaultPosLimitY       = 0.20f;
constexpr float kDefaultPosLimitZ       = 0.40f;
constexpr float kDefaultPosLimitZBack   = 0.10f;
constexpr float kDefaultPositionScale   = 100.0f;
constexpr bool  kDefaultCollision       = true;
constexpr float kDefaultCollisionMargin = 20.0f;
constexpr int   kDefaultVkToggle        = 0x23; // VK_END
constexpr int   kDefaultVkCycleMode     = 0x21; // VK_PRIOR (Page Up)
constexpr int   kDefaultVkYawMode       = 0x22; // VK_NEXT (Page Down)
constexpr bool  kDefaultChord           = true;

// src/fov_range.h as the frozen build had it.
constexpr float kMinFovDegrees = 20.0f;
constexpr float kMaxFovDegrees = 170.0f;

bool IsUsableFov(float fovDegrees) {
    return std::isfinite(fovDegrees) &&
           fovDegrees >= kMinFovDegrees && fovDegrees <= kMaxFovDegrees;
}

// cameraunlock::input::IsValidHotkeyCode as core had it when this reader was frozen.
bool IsValidHotkeyCode(int vkCode) {
    // Function keys F1-F12
    if (vkCode >= 0x70 && vkCode <= 0x7B) return true;

    // NumPad keys
    if (vkCode >= 0x60 && vkCode <= 0x6F) return true;

    // Special keys
    if (vkCode == 0x13) return true;  // Pause
    if (vkCode == 0x2C) return true;  // PrintScreen
    if (vkCode == 0x90) return true;  // NumLock
    if (vkCode == 0x91) return true;  // ScrollLock
    if (vkCode == 0x21) return true;  // PageUp
    if (vkCode == 0x22) return true;  // PageDown
    if (vkCode == 0x24) return true;  // Home
    if (vkCode == 0x23) return true;  // End
    if (vkCode == 0x2D) return true;  // Insert
    if (vkCode == 0x2E) return true;  // Delete

    if (vkCode == 0x1B) return true;  // Escape
    if (vkCode == 0x20) return true;  // Space

    if (vkCode >= 0x30 && vkCode <= 0x39) return true;  // 0-9
    if (vkCode >= 0x41 && vkCode <= 0x5A) return true;  // A-Z

    return false;
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                             const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (reader.ReadString(section, key, "").empty()) return;
    warned = true;
    Log::Line(
        "WARN: Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

// Reports a value the sanitizer had to change, and returns the sanitized one, so a
// setting the mod is not honouring never passes silently.
float ReportIfSanitized(const char* section, const char* key, float raw, float clean) {
    if (raw != clean) {
        Log::Line("WARN: INI [%s] %s value %.4f out of range or non-finite; using %.4f",
                  section, key, raw, clean);
    }
    return clean;
}

float ReadFinite(const cameraunlock::IniReader& ini, const char* section, const char* key,
                 float fallback) {
    const float raw = ini.ReadFloat(section, key, fallback);
    return ReportIfSanitized(section, key, raw, SanitizeFinite(raw, fallback));
}

float ReadLimit(const cameraunlock::IniReader& ini, const char* key, float fallback) {
    const float raw = ini.ReadFloat("Position", key, fallback);
    return ReportIfSanitized("Position", key, raw, SanitizePositiveLimit(raw, fallback));
}

float ReadSmoothing(const cameraunlock::IniReader& ini, const char* key, float fallback) {
    const float raw = ini.ReadFloat("Smoothing", key, fallback);
    return ReportIfSanitized("Smoothing", key, raw, SanitizeSmoothing(raw, fallback));
}

// Refused on a port outside the bindable range, which is the one config error the mod
// refused to start on: every other bad value has a usable fallback.
ReadResult ReadGeneralSection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.enabled_on_startup = ini.ReadBool("General", "EnableOnStartup", kDefaultEnableOnStartup);
    const int port = ini.ReadInt("General", "Port", kDefaultPort);
    if (port < kMinPort || port > kMaxPort) {
        Log::Line("ERROR: INI port %d out of range %d-%d", port, kMinPort, kMaxPort);
        return {ReadStatus::Refused,
                "its [General] Port " + std::to_string(port) + " is outside " +
                    std::to_string(kMinPort) + "-" + std::to_string(kMaxPort) +
                    ", so the version that wrote this file did not start"};
    }
    cfg.udp_port = static_cast<std::uint16_t>(port);
    cfg.world_space_yaw = ini.ReadBool("General", "WorldSpaceYaw", kDefaultWorldSpaceYaw);
    cfg.move_crosshair = ini.ReadBool("General", "MoveCrosshair", kDefaultMoveCrosshair);
    return {ReadStatus::Read, {}};
}

void ReadCameraSection(Config& cfg, const cameraunlock::IniReader& ini) {
    const float rawFov = ini.ReadFloat("Camera", "Fov", kDefaultFov);
    if (rawFov != kDefaultFov && !IsUsableFov(rawFov)) {
        Log::Line("WARN: INI Camera.Fov value %.1f is not 0 or within %.0f-%.0f degrees; "
                  "keeping the game's own field of view",
                  rawFov, kMinFovDegrees, kMaxFovDegrees);
        cfg.fov = kDefaultFov;
        return;
    }
    cfg.fov = rawFov;
}

void ReadSensitivitySection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.sens_yaw   = ReadFinite(ini, "Sensitivity", "Yaw",   kDefaultSensitivity);
    cfg.sens_pitch = ReadFinite(ini, "Sensitivity", "Pitch", kDefaultSensitivity);
    cfg.sens_roll  = ReadFinite(ini, "Sensitivity", "Roll",  kDefaultSensitivity);
    cfg.invert_yaw   = ini.ReadBool("Sensitivity", "InvertYaw",   kDefaultInvert);
    cfg.invert_pitch = ini.ReadBool("Sensitivity", "InvertPitch", kDefaultInvert);
    cfg.invert_roll  = ini.ReadBool("Sensitivity", "InvertRoll",  kDefaultInvert);
}

void ReadSmoothingSection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.local_smoothing  = ReadSmoothing(ini, "LocalSmoothing",  kDefaultLocalSmoothing);
    cfg.remote_smoothing = ReadSmoothing(ini, "RemoteSmoothing", kDefaultRemoteSmoothing);

    WarnRetiredSmoothingKey(ini, "Smoothing", "Smoothing");
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");
}

void ReadPositionSection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.position_enabled = ini.ReadBool("Position", "Enabled", kDefaultPositionEnabled);
    cfg.pos_sens_x = ReadFinite(ini, "Position", "SensitivityX", kDefaultPosSens);
    cfg.pos_sens_y = ReadFinite(ini, "Position", "SensitivityY", kDefaultPosSens);
    cfg.pos_sens_z = ReadFinite(ini, "Position", "SensitivityZ", kDefaultPosSens);
    cfg.pos_limit_x = ReadLimit(ini, "LimitX", kDefaultPosLimitX);
    cfg.pos_limit_y = ReadLimit(ini, "LimitY", kDefaultPosLimitY);
    cfg.pos_limit_z = ReadLimit(ini, "LimitZ", kDefaultPosLimitZ);
    cfg.pos_limit_z_back = ReadLimit(ini, "LimitZBack", kDefaultPosLimitZBack);
    cfg.position_scale = ReadFinite(ini, "Position", "PositionScale", kDefaultPositionScale);
    cfg.invert_pos_x = ini.ReadBool("Position", "InvertX", kDefaultInvert);
    cfg.invert_pos_y = ini.ReadBool("Position", "InvertY", kDefaultInvert);
    cfg.invert_pos_z = ini.ReadBool("Position", "InvertZ", kDefaultInvert);
}

void ReadCollisionSection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.collision_enabled = ini.ReadBool("Collision", "Enabled", kDefaultCollision);
    const float raw = ini.ReadFloat("Collision", "Margin", kDefaultCollisionMargin);
    cfg.collision_margin =
        ReportIfSanitized("Collision", "Margin", raw,
                          SanitizePositiveLimit(raw, kDefaultCollisionMargin));
}

int ReadVirtualKey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const int raw = ini.ReadHex("Hotkeys", key, fallback);
    if (IsValidHotkeyCode(raw)) {
        return raw;
    }
    Log::Line("WARN: INI [Hotkeys] %s value 0x%02X is not a usable virtual-key code; "
              "using the default 0x%02X", key, raw, fallback);
    return fallback;
}

void ReadHotkeysSection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.vk_toggle     = ReadVirtualKey(ini, "Toggle",    kDefaultVkToggle);
    cfg.vk_cycle_mode = ReadVirtualKey(ini, "CycleMode", kDefaultVkCycleMode);
    cfg.vk_yaw_mode   = ReadVirtualKey(ini, "YawMode",   kDefaultVkYawMode);
    cfg.chord_toggle     = ini.ReadBool("Hotkeys", "ChordToggle",    kDefaultChord);
    cfg.chord_cycle_mode = ini.ReadBool("Hotkeys", "ChordCycleMode", kDefaultChord);
    cfg.chord_yaw_mode   = ini.ReadBool("Hotkeys", "ChordYawMode",   kDefaultChord);
}

}

ReadResult Config::Read(const char* iniPath) {
    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        return {ReadStatus::Absent, {}};
    }

    const ReadResult general = ReadGeneralSection(*this, ini);
    if (general.status == ReadStatus::Refused) {
        return general;
    }
    ReadCameraSection(*this, ini);
    ReadSensitivitySection(*this, ini);
    ReadSmoothingSection(*this, ini);
    ReadPositionSection(*this, ini);
    ReadCollisionSection(*this, ini);
    ReadHotkeysSection(*this, ini);
    return {ReadStatus::Read, {}};
}

std::vector<cameraunlock::config::LegacyKey> ReadKeys() {
    return {
        {"General", "EnableOnStartup"},
        {"General", "Port"},
        {"General", "WorldSpaceYaw"},
        {"General", "MoveCrosshair"},
        {"Camera", "Fov"},
        {"Sensitivity", "Yaw"},
        {"Sensitivity", "Pitch"},
        {"Sensitivity", "Roll"},
        {"Sensitivity", "InvertYaw"},
        {"Sensitivity", "InvertPitch"},
        {"Sensitivity", "InvertRoll"},
        {"Smoothing", "LocalSmoothing"},
        {"Smoothing", "RemoteSmoothing"},
        {"Smoothing", "Smoothing"},
        {"Position", "Smoothing"},
        {"Position", "Enabled"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Position", "PositionScale"},
        {"Position", "InvertX"},
        {"Position", "InvertY"},
        {"Position", "InvertZ"},
        {"Collision", "Enabled"},
        {"Collision", "Margin"},
        {"Hotkeys", "Toggle"},
        {"Hotkeys", "CycleMode"},
        {"Hotkeys", "YawMode"},
        {"Hotkeys", "ChordToggle"},
        {"Hotkeys", "ChordCycleMode"},
        {"Hotkeys", "ChordYawMode"},
    };
}

}
