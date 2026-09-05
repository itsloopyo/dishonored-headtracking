// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include "config_sanitize.h"
#include "fov_range.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/input/hotkey_poller.h"

#include <windows.h>

namespace DishonoredHeadTracking {

namespace {

// Every default and bound this file writes and reads lives in config.h, so the writer,
// the reader's per-key fallback and the Config member initialisers cannot drift apart.
// The float-typed defaults widen to double for the WriteDouble calls and match ReadFloat
// exactly on the read side.
//
// Note what is NOT here: the protocol-to-engine sign conversions. Position X and Z are
// mirrored relative to UE3 and both are converted at the engine boundary in
// camera_hook.cpp rather than through an Invert default, because inverting inside the
// pipeline flips the value BEFORE the asymmetric clamp and hands the generous
// forward-lean budget to the backward lean.

bool FileExists(const char* path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

void WriteGeneralSection(cameraunlock::IniWriter& w) {
    w.WriteSection("General");
    w.WriteBool("EnableOnStartup", kDefaultEnableOnStartup);
    w.WriteInt("Port", kDefaultPort);
    w.WriteComment("Yaw mode: true = horizon-locked yaw (default), false = camera-local.");
    w.WriteBool("WorldSpaceYaw", kDefaultWorldSpaceYaw);
    w.WriteComment("The game draws its crosshair at screen centre; head tracking moves the view");
    w.WriteComment("away from it. This moves the crosshair to where the game is actually aiming.");
    w.WriteBool("MoveCrosshair", kDefaultMoveCrosshair);
}

void WriteCameraSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Camera");
    w.WriteComment("Horizontal field of view in degrees, at the game's default zoom.");
    w.WriteComment("0 keeps the game's own FOV - Dishonored has an FOV slider in Options > Graphics,");
    w.WriteComment("so set this only to go past what that slider offers.");
    w.WriteComment("The difference between this and the game's default is added to whatever FOV the");
    w.WriteComment("game asks for, so weapon zooms still zoom by the same amount, and the crosshair");
    w.WriteComment("is projected with the value the scene is rendered at.");
    w.WriteDouble("Fov", kDefaultFov);
}

void WriteSensitivitySection(cameraunlock::IniWriter& w) {
    w.WriteSection("Sensitivity");
    w.WriteDouble("Yaw", kDefaultSensitivity);
    w.WriteDouble("Pitch", kDefaultSensitivity);
    w.WriteDouble("Roll", kDefaultSensitivity);
    w.WriteComment("Flip an axis only if your tracker reports it backwards. The engine's own");
    w.WriteComment("sign conventions are already handled; these three ship off.");
    w.WriteBool("InvertYaw", kDefaultInvert);
    w.WriteBool("InvertPitch", kDefaultInvert);
    w.WriteBool("InvertRoll", kDefaultInvert);
}

void WriteSmoothingSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Smoothing");
    w.WriteComment("Chosen per connection from the tracker's source address; covers rotation and position.");
    w.WriteComment("LocalSmoothing: tracker running on this machine (loopback). 0 = none, 1 = heavy.");
    w.WriteDouble("LocalSmoothing", kDefaultLocalSmoothing);
    w.WriteComment("RemoteSmoothing: tracker on a remote network device. 0 = none, 1 = heavy.");
    w.WriteDouble("RemoteSmoothing", kDefaultRemoteSmoothing);
}

void WritePositionSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Position");
    w.WriteComment("6DOF positional tracking. PositionScale = world units (cm) per metre of head translation.");
    w.WriteBool("Enabled", kDefaultPositionEnabled);
    w.WriteDouble("SensitivityX", kDefaultPosSens);
    w.WriteDouble("SensitivityY", kDefaultPosSens);
    w.WriteDouble("SensitivityZ", kDefaultPosSens);
    w.WriteDouble("LimitX", kDefaultPosLimitX);
    w.WriteDouble("LimitY", kDefaultPosLimitY);
    w.WriteDouble("LimitZ", kDefaultPosLimitZ);
    w.WriteDouble("LimitZBack", kDefaultPosLimitZBack);
    w.WriteDouble("PositionScale", kDefaultPositionScale);
    w.WriteComment("As above: only for a tracker that reports an axis backwards. Leaving these");
    w.WriteComment("off is what keeps LimitZ on leaning in and LimitZBack on pulling away.");
    w.WriteBool("InvertX", kDefaultInvert);
    w.WriteBool("InvertY", kDefaultInvert);
    w.WriteBool("InvertZ", kDefaultInvert);
}

void WriteCollisionSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Collision");
    w.WriteComment("Head tracking moves the camera off the player's eye, so leaning toward a wall");
    w.WriteComment("can carry the view through it. This traces the lean against the world and");
    w.WriteComment("stops the camera short of whatever it would have entered.");
    w.WriteBool("Enabled", kDefaultCollision);
    w.WriteComment("World units (cm) kept between the camera and the surface it stopped at.");
    w.WriteComment("Raise it if you can still see through a wall you lean into; lower it if the");
    w.WriteComment("lean stops further from walls than you want.");
    w.WriteDouble("Margin", kDefaultCollisionMargin);
}

void WriteHotkeysSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Hotkeys");
    w.WriteComment("Virtual-key codes. Defaults: End (toggle), Page Up (cycle tracking mode), Page Down (yaw mode).");
    w.WriteHex("Toggle", kDefaultVkToggle);
    w.WriteHex("CycleMode", kDefaultVkCycleMode);
    w.WriteHex("YawMode", kDefaultVkYawMode);
    w.WriteComment("Chord alternatives: Ctrl+Shift+Y (toggle), Ctrl+Shift+G (cycle tracking mode), Ctrl+Shift+H (yaw mode).");
    w.WriteBool("ChordToggle", kDefaultChord);
    w.WriteBool("ChordCycleMode", kDefaultChord);
    w.WriteBool("ChordYawMode", kDefaultChord);
}

// Returns false when the file could not be created, so the caller can say WHY the
// config is missing. Without it the only diagnostic was "Failed to open INI", which
// names the symptom of a game directory the player cannot write to, not the cause.
bool WriteDefaultIni(const char* path) {
    cameraunlock::IniWriter w;
    if (!w.Open(path)) return false;
    w.WriteComment("Dishonored - Head Tracking configuration");
    w.WriteComment("Lives next to dinput8.dll in Binaries/Win32/.");
    w.WriteBlankLine();
    WriteGeneralSection(w);
    w.WriteBlankLine();
    WriteCameraSection(w);
    w.WriteBlankLine();
    WriteSensitivitySection(w);
    w.WriteBlankLine();
    WriteSmoothingSection(w);
    w.WriteBlankLine();
    WritePositionSection(w);
    w.WriteBlankLine();
    WriteCollisionSection(w);
    w.WriteBlankLine();
    WriteHotkeysSection(w);
    w.Close();
    return true;
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

// Reads a float that ends up multiplied into the injected viewpoint. strtod accepts
// "nan" and "inf", and either one propagates from here into the rotation or the camera
// location and leaves the player with a black screen and nothing in the log. Magnitude
// and sign are NOT checked: a sensitivity above 1, a negative one, or a limit the
// player has widened are all legitimate tuning.
float ReadFinite(const cameraunlock::IniReader& ini, const char* section, const char* key,
                 float fallback) {
    const float raw = ini.ReadFloat(section, key, fallback);
    return ReportIfSanitized(section, key, raw, SanitizeFinite(raw, fallback));
}

// A positional limit is additionally required to be above zero: a negative one inverts
// the processor's clamp and pins the camera at a constant offset. See config_sanitize.h.
float ReadLimit(const cameraunlock::IniReader& ini, const char* key, float fallback) {
    const float raw = ini.ReadFloat("Position", key, fallback);
    return ReportIfSanitized("Position", key, raw, SanitizePositiveLimit(raw, fallback));
}

// Smoothing is additionally clamped to [0,1], the whole domain the settle speed is
// mapped from. See config_sanitize.h.
float ReadSmoothing(const cameraunlock::IniReader& ini, const char* key, float fallback) {
    const float raw = ini.ReadFloat("Smoothing", key, fallback);
    return ReportIfSanitized("Smoothing", key, raw, SanitizeSmoothing(raw, fallback));
}

// Returns false on a port outside the bindable range, which is the one config error
// the mod refuses to start on: every other bad value has a usable fallback.
bool ReadGeneralSection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.enabled_on_startup = ini.ReadBool("General", "EnableOnStartup", kDefaultEnableOnStartup);
    const int port = ini.ReadInt("General", "Port", kDefaultPort);
    if (port < kMinPort || port > kMaxPort) {
        Log::Line("ERROR: INI port %d out of range %d-%d", port, kMinPort, kMaxPort);
        return false;
    }
    cfg.udp_port = static_cast<uint16_t>(port);
    cfg.world_space_yaw = ini.ReadBool("General", "WorldSpaceYaw", kDefaultWorldSpaceYaw);
    cfg.move_crosshair = ini.ReadBool("General", "MoveCrosshair", kDefaultMoveCrosshair);
    return true;
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
    // PositionScale multiplies the clamped lean straight into the camera location, so a
    // non-finite one reaches the viewpoint even with every limit intact.
    cfg.position_scale = ReadFinite(ini, "Position", "PositionScale", kDefaultPositionScale);
    cfg.invert_pos_x = ini.ReadBool("Position", "InvertX", kDefaultInvert);
    cfg.invert_pos_y = ini.ReadBool("Position", "InvertY", kDefaultInvert);
    cfg.invert_pos_z = ini.ReadBool("Position", "InvertZ", kDefaultInvert);
}

void ReadCollisionSection(Config& cfg, const cameraunlock::IniReader& ini) {
    cfg.collision_enabled = ini.ReadBool("Collision", "Enabled", kDefaultCollision);
    // A zero or negative margin puts the camera exactly on the surface it hit, where the
    // near clip plane renders straight through it - the clamp would then look broken
    // rather than absent. Sanitized like a positional limit for the same reason.
    const float raw = ini.ReadFloat("Collision", "Margin", kDefaultCollisionMargin);
    cfg.collision_margin =
        ReportIfSanitized("Collision", "Margin", raw,
                          SanitizePositiveLimit(raw, kDefaultCollisionMargin));
}

// A rebind the poller cannot act on is the worst kind of config error: the key simply
// never fires, and the log says the binding was accepted. GetAsyncKeyState takes a
// virtual-key code in [1, 254] and answers 0 for anything else, so `Toggle=0x1234` or
// a mistyped `Toggle=0` disables the hotkey with no diagnostic at all. Report the
// rejection and fall back to the documented default.
int ReadVirtualKey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const int raw = ini.ReadHex("Hotkeys", key, fallback);
    if (cameraunlock::input::IsValidHotkeyCode(raw)) {
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

bool Config::LoadOrCreate(const char* iniPath) {
    if (!iniPath || !*iniPath) {
        Log::Line("ERROR: could not resolve the directory this mod was loaded from, so "
                  "there is nowhere to read the INI from. The mod will not start.");
        return false;
    }
    if (!FileExists(iniPath) && !WriteDefaultIni(iniPath)) {
        Log::Line("ERROR: could not create the default INI at %s. The game directory is "
                  "not writable by this account.", iniPath);
        return false;
    }

    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return false;
    }

    if (!ReadGeneralSection(*this, ini)) {
        return false;
    }
    ReadCameraSection(*this, ini);
    ReadSensitivitySection(*this, ini);
    ReadSmoothingSection(*this, ini);
    ReadPositionSection(*this, ini);
    ReadCollisionSection(*this, ini);
    ReadHotkeysSection(*this, ini);
    return true;
}

}
