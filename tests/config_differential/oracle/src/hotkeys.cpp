// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hotkeys.h"

#include "logging.h"

#include "cameraunlock/input/chord_hotkeys.h"

#include <exception>

namespace DishonoredHeadTracking {

namespace {
// Poll interval for the hotkey thread, in milliseconds. ~60Hz: fast enough that a
// deliberate keypress is never missed, slow enough to cost nothing.
constexpr int kPollIntervalMs = 16;
}

bool Hotkeys::Start(const Config& cfg, Action onToggle,
                    Action onCycleMode, Action onYawMode) {
    if (m_started) return true;

    using cameraunlock::input::ChordGuarded;
    using cameraunlock::input::NavGuarded;

    // Nav-cluster keys are suppressed while Ctrl+Shift is held so the chord
    // path is the sole trigger for Ctrl+Shift+<nav> combos - a single keypress
    // never fires an action twice.
    m_poller.SetToggleKey(cfg.vk_toggle, NavGuarded(onToggle));
    m_poller.AddHotkey(cfg.vk_cycle_mode, NavGuarded(onCycleMode));
    m_poller.AddHotkey(cfg.vk_yaw_mode, NavGuarded(onYawMode));

    // Chord alternatives (Ctrl+Shift+Y / Ctrl+Shift+G / Ctrl+Shift+H)
    // on the same poller; ChordGuarded gates each action on the modifier state.
    if (cfg.chord_toggle)     m_poller.AddHotkey('Y', ChordGuarded(std::move(onToggle)));
    if (cfg.chord_cycle_mode) m_poller.AddHotkey('G', ChordGuarded(std::move(onCycleMode)));
    if (cfg.chord_yaw_mode)   m_poller.AddHotkey('H', ChordGuarded(std::move(onYawMode)));

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

    Log::Line("Hotkeys: toggle=0x%02X cyclemode=0x%02X yawmode=0x%02X",
              cfg.vk_toggle, cfg.vk_cycle_mode, cfg.vk_yaw_mode);

    m_started = true;
    return true;
}

void Hotkeys::Stop() {
    if (!m_started) return;
    m_poller.Stop();
    m_started = false;
}

}
