// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hotkeys.h"

#include "logging.h"

#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"

#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace DishonoredHeadTracking {

namespace {
// Poll interval for the hotkey thread, in milliseconds. ~60Hz: fast enough that a
// deliberate keypress is never missed, slow enough to cost nothing.
constexpr int kPollIntervalMs = 16;

// The config table already refused a list that does not parse, so one here is a bug rather
// than a player's typo.
std::vector<cameraunlock::input::KeyBinding> Parse(const std::string& list) {
    cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error("hotkey list '" + list + "' does not parse: " + parsed.error);
    return parsed.bindings;
}
}

bool Hotkeys::Start(const Config& cfg, Action onToggle,
                    Action onCycleMode, Action onYawMode) {
    if (m_started) return true;

    // One registration per key: a binding without modifiers stays quiet while Ctrl and
    // Shift are both held, so Ctrl+Shift with a key reaches only a binding that names it,
    // and one press never fires an action twice.
    using cameraunlock::input::RegisterKeyBindings;
    RegisterKeyBindings(m_poller, Parse(cfg.toggle_key_name), std::move(onToggle));
    RegisterKeyBindings(m_poller, Parse(cfg.cycle_tracking_mode_key_name), std::move(onCycleMode));
    RegisterKeyBindings(m_poller, Parse(cfg.yaw_mode_key_name), std::move(onYawMode));

    // The poller rethrows std::system_error when the process cannot spawn its thread,
    // deliberately, so the failure is not silent. Catch it here: the caller runs on a
    // bare thread procedure with no handler above it, where an escaping exception is
    // std::terminate - the game dying outright, with the log stopping mid-startup.
    bool started = false;
    try {
        started = m_poller.Start(kPollIntervalMs);
    } catch (const std::exception& e) {
        Log::Line("ERROR: HotkeyPoller could not start its thread: %s", e.what());
        return false;
    }
    if (!started) {
        Log::Line("ERROR: HotkeyPoller failed to start");
        return false;
    }

    Log::Line("Hotkeys: toggle=[%s] cycle mode=[%s] yaw mode=[%s]",
              cfg.toggle_key_name.c_str(), cfg.cycle_tracking_mode_key_name.c_str(),
              cfg.yaw_mode_key_name.c_str());

    m_started = true;
    return true;
}

void Hotkeys::Stop() {
    if (!m_started) return;
    m_poller.Stop();
    m_started = false;
}

}
