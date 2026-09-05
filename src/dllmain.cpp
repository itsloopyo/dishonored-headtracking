// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"
#include "camera_hook.h"
#include "config.h"
#include "crosshair_hook.h"
#include "fov_hook.h"
#include "hotkeys.h"
#include "logging.h"
#include "path_utils.h"
#include "tracking_runtime.h"

#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/memory/pe_fingerprint.h"

#include <windows.h>
#include <process.h>

#include <exception>
#include <string>

namespace {

using namespace DishonoredHeadTracking;

constexpr const char* kModName    = "DishonoredHeadTracking";
constexpr const char* kModVersion = "0.0.0";
constexpr const char* kLogFile    = "HeadTracking.log";
constexpr const char* kIniFile    = "DishonoredHeadTracking.ini";

constexpr int kInitMaxWaitMs = 30000;
constexpr int kInitPollMs    = 100;
constexpr int kHeartbeatMs   = 5000;
// Tracker state changes worth logging before the log stops being a record of the session
// and becomes a record of one flaky link. A tracker that keeps losing the face flips this
// every heartbeat; the first twenty transitions already say so.
constexpr int kMaxHeartbeatReports = 20;

// Deliberately never destroyed.
//
// As namespace-scope objects these had non-trivial destructors, and the CRT runs those
// from DLL_PROCESS_DETACH - under the loader lock, joining the UDP receiver's two
// threads and the hotkey poller's, which is exactly what PinSelf below says must never
// happen here. Pinning stops FreeLibrary reaching detach; it does not stop process exit
// reaching the destructors. Leaking them does: there is nothing to run.
//
// The OS reclaims the sockets, threads and handles at exit regardless.
TrackingRuntime& Tracking() {
    static TrackingRuntime* instance = new TrackingRuntime();
    return *instance;
}

Hotkeys& Input() {
    static Hotkeys* instance = new Hotkeys();
    return *instance;
}

void LogFingerprint() {
    HMODULE hExe = GetModuleHandleA(kGameExeName);
    cameraunlock::memory::PeFingerprint fp{};
    if (hExe && cameraunlock::memory::ReadPeFingerprint(hExe, fp)) {
        Log::Line("PE fingerprint: TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
                  fp.TimeDateStamp, fp.SizeOfImage, fp.CheckSum);
    } else {
        Log::Line("WARN: could not read PE fingerprint of %s", kGameExeName);
    }
}

// The ASI loader can run us before the game module is mapped. Returns false when it
// never appears, which means this is not the process we belong in.
bool WaitForGameModule() {
    for (int waited = 0; waited < kInitMaxWaitMs; waited += kInitPollMs) {
        if (GetModuleHandleA(kGameExeName)) {
            return true;
        }
        Sleep(kInitPollMs);
    }
    return GetModuleHandleA(kGameExeName) != nullptr;
}

bool StartInput(const Config& cfg) {
    return Input().Start(
        cfg,
        [] { Tracking().ToggleEnabled(); },
        [] { Tracking().CycleTrackingMode(); },
        [] { Tracking().ToggleYawMode(); });
}

// The camera hook is the mod; without it there is nothing to run. The other two are
// enhancements to a working camera hook, so a failure to install either is reported and
// the mod carries on.
bool InstallHooks(const BuildProfile& profile, std::uintptr_t moduleBase, const Config& cfg) {
    if (!InstallCameraHook(profile, moduleBase, Tracking(), cfg)) {
        Log::Line("ERROR: Camera hook install failed");
        return false;
    }

    if (cfg.fov > 0.0f && !InstallFovHook(profile, moduleBase, cfg)) {
        Log::Line("WARN: the field of view override did not install; the scene renders at "
                  "the game's own FOV");
    }

    if (cfg.move_crosshair && !InstallCrosshairHook(profile, moduleBase)) {
        Log::Line("WARN: the crosshair hook did not install; the game's crosshair stays "
                  "at screen centre and will not follow the aim point");
    }
    return true;
}

// Reports every change in whether tracker packets are arriving, for the life of the
// process. This is the log a player is asked for when head tracking does nothing in game.
void RunHeartbeat() {
    bool lastReceiving = false;
    bool firstReport = true;
    int reports = 0;
    for (;;) {
        const bool receiving = Tracking().IsReceiving();
        if ((firstReport || receiving != lastReceiving) && reports < kMaxHeartbeatReports) {
            ++reports;
            Log::Line("OpenTrack: %s%s", receiving ? "receiving data" : "no data",
                      reports == kMaxHeartbeatReports
                          ? " (further tracker state changes not logged)" : "");
            lastReceiving = receiving;
            firstReport = false;
        }
        Sleep(kHeartbeatMs);
    }
}

unsigned InitThreadBody() {
    if (!WaitForGameModule()) {
        return 1;
    }

    // Open() truncates, so each launch starts from scratch, and rotates the
    // outgoing file to HeadTracking.prev.log. That one generation is what makes
    // a crash report survive the relaunch the player does straight afterwards.
    //
    // An empty path means the module directory could not be resolved, and every later
    // diagnostic - including the one the config loader writes for this exact case - would
    // go to a log that was never created. The debugger channel is all that is left.
    const std::wstring logPath = GetModulePathW(kLogFile);
    if (logPath.empty()) {
        OutputDebugStringA("DishonoredHeadTracking: could not resolve the directory this "
                           "mod was loaded from; no log will be written\n");
        return 1;
    }
    Log::Open(logPath);
    cameraunlock::diagnostics::InstallCrashHandler();

    Log::Line("%s v%s attached to %s", kModName, kModVersion, kGameExeName);
    LogFingerprint();

    Config cfg;
    if (!cfg.LoadOrCreate(GetModulePath(kIniFile).c_str())) {
        Log::Line("ERROR: Config load failed");
        return 1;
    }
    Log::Line("Config: port=%u enabled=%d smoothing=(local %.2f, remote %.2f) sens=(%.2f,%.2f,%.2f)",
              cfg.udp_port, cfg.enabled_on_startup ? 1 : 0,
              cfg.local_smoothing, cfg.remote_smoothing,
              cfg.sens_yaw, cfg.sens_pitch, cfg.sens_roll);

    const BuildProfile* profile = MatchRunningProfile();
    if (!profile) {
        // No matching build: stay dormant (no hooks). The game runs vanilla.
        // MatchRunningProfile has already logged the mismatch direction.
        return 0;
    }

    Tracking().Start(cfg);

    if (!StartInput(cfg)) {
        Log::Line("ERROR: Hotkeys start failed");
        Tracking().Stop();
        return 1;
    }

    const auto moduleBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(kGameExeName));
    if (!InstallHooks(*profile, moduleBase, cfg)) {
        Input().Stop();
        Tracking().Stop();
        return 1;
    }

    Log::Line("%s ready", kModName);
    RunHeartbeat();
    return 0;
}

// The thread procedure proper. An exception escaping a bare __stdcall thread proc is
// std::terminate - the game dying outright with the log stopping mid-startup - and there
// are three places above that can throw: the UDP receiver and the hotkey poller each
// construct a std::thread, which throws std::system_error when the process cannot spawn
// one, and the config path allocates.
unsigned __stdcall InitThread(void*) {
    try {
        return InitThreadBody();
    } catch (const std::exception& e) {
        Log::Line("ERROR: startup failed with an exception: %s", e.what());
        return 1;
    } catch (...) {
        Log::Line("ERROR: startup failed with an unknown exception");
        return 1;
    }
}

// Makes FreeLibrary on this module a no-op, which is what lets DLL_PROCESS_DETACH do
// nothing at all.
//
// There is no safe teardown from DllMain. Both detach cases hold the loader lock, and
// everything a teardown would have to do is forbidden while holding it: joining the
// hotkey poller and the UDP receiver threads deadlocks, because a thread cannot exit
// without taking the loader lock itself to run DLL_THREAD_DETACH for every other module
// (DisableThreadLibraryCalls only suppressed that for ours), and MinHook's unhook
// suspends every other thread while we hold a lock some of them may be waiting on.
// Meanwhile the detour bodies live in this image, so an in-flight render thread would be
// executing code that is about to be unmapped, and no amount of trampoline preservation
// helps with that.
//
// Pinning removes the whole question: the module stays mapped, the hooks stay live and
// valid for the life of the process, and the OS reclaims everything at exit. An .asi is
// loaded once at startup and is not meant to come back out.
void PinSelf(HMODULE self) {
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       reinterpret_cast<LPCWSTR>(self), &pinned);
}

}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    UNREFERENCED_PARAMETER(lpReserved);
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);
            PinSelf(hModule);
            // The thread cannot run until DllMain returns and the loader lock is
            // released, which is exactly why the real work goes on it.
            const HANDLE thread = reinterpret_cast<HANDLE>(
                _beginthreadex(nullptr, 0, InitThread, nullptr, 0, nullptr));
            if (thread) {
                CloseHandle(thread);
            } else {
                // The log is opened on that thread, so it does not exist yet and this is
                // the only channel left that is safe under the loader lock. Without it a
                // failure here is a mod that loads and then does nothing, silently -
                // indistinguishable from the ASI loader never picking it up.
                OutputDebugStringA(
                    "DishonoredHeadTracking: init thread could not start\n");
            }
            break;
        }

        case DLL_PROCESS_DETACH:
            // Nothing. See PinSelf.
            break;
    }
    return TRUE;
}
