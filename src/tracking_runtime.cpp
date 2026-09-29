// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "tracking_runtime.h"

#include "logging.h"
#include "ue3_math.h"

#include "cameraunlock/tracking/tracking_mode.h"

#include <cstdint>

namespace DishonoredHeadTracking {

void TrackingRuntime::ConfigurePosition() {
    // The limits and the smoothing copies come from the config; the sensitivities and
    // inversions stay at the identity PositionSettings starts with.
    m_session.GetPositionProcessor().SetSettings(m_cfg.position);
}

void TrackingRuntime::ConfigureSmoothing() {
    // Must run after ConfigurePosition, whose SetSettings would otherwise overwrite the
    // smoothing fields. The session forwards both values to the rotation AND position
    // processors and re-reads the receiver's connection locality inside every Update()
    // to pick the one that applies. Without IsRemoteConnection() on the receiver that
    // selection silently pins to local, so assert the trait.
    static_assert(decltype(m_session)::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection()");
    m_session.SetLocalSmoothing(m_cfg.local_smoothing);
    m_session.SetRemoteSmoothing(m_cfg.remote_smoothing);
}

void TrackingRuntime::Start(const Config& cfg) {
    m_cfg = cfg;

    ConfigurePosition();
    ConfigureSmoothing();

    m_enabled.store(m_cfg.enable_on_startup, std::memory_order_relaxed);
    m_worldSpaceYaw.store(m_cfg.world_space_yaw, std::memory_order_relaxed);
    // The table never loads a pair that names no mode: it reads both as their defaults
    // instead.
    m_session.SetMode(
        cameraunlock::DecodeTrackingMode(m_cfg.rotation_enabled, m_cfg.position_enabled).value());

    m_receiver.SetLog([](const std::string& msg) {
        Log::Line("UDP: %s", msg.c_str());
    });

    const auto port = static_cast<std::uint16_t>(m_cfg.udp_port);
    if (m_receiver.Start(port)) {
        Log::Line("UDP receiver listening on port %u", port);
    } else {
        Log::Line("WARN: UDP receiver did not bind immediately on port %u; background retry active", port);
    }
}

void TrackingRuntime::Stop() {
    m_receiver.Stop();
}

void TrackingRuntime::ToggleEnabled() {
    bool prev = m_enabled.load(std::memory_order_relaxed);
    m_enabled.store(!prev, std::memory_order_relaxed);
    Log::Line("Tracking %s", !prev ? "enabled" : "disabled");
}

cameraunlock::TrackingMode TrackingRuntime::CycleTrackingMode() {
    const cameraunlock::TrackingMode mode = m_session.CycleMode();
    switch (mode) {
        case cameraunlock::TrackingMode::RotationAndPosition:
            Log::Line("Tracking mode: rotation + position (6DOF)");
            break;
        case cameraunlock::TrackingMode::RotationOnly:
            Log::Line("Tracking mode: rotation only");
            break;
        case cameraunlock::TrackingMode::PositionOnly:
            Log::Line("Tracking mode: position only");
            break;
    }
    return mode;
}

bool TrackingRuntime::ToggleYawMode() {
    bool prev = m_worldSpaceYaw.load(std::memory_order_relaxed);
    m_worldSpaceYaw.store(!prev, std::memory_order_relaxed);
    Log::Line("Yaw mode: %s", !prev ? "world-space (horizon-locked)" : "camera-local");
    return !prev;
}

// The pipeline's own finite checks stop at the wire: OpenTrackPacket rejects a non-finite
// datagram, but nothing downstream of it guarantees the processed pose stays finite. From
// here it would reach the FRotator conversion and the camera location. Drop the channel
// instead, and say so once - a silently dropped pose reads in game exactly like a tracker
// that has stopped sending.
namespace {
void ReportNonFinite(const char* channel) {
    static bool s_warned = false;
    if (s_warned) return;
    s_warned = true;
    Log::Line("WARN: the processed %s came out non-finite and is being dropped.", channel);
}
}  // namespace

FrameSample TrackingRuntime::SampleFrame() {
    FrameSample out;

    if (!m_enabled.load(std::memory_order_relaxed)) {
        return out;
    }
    if (!m_receiver.IsReceiving()) {
        return out;
    }

    if (!m_session.Update(m_clock.Tick())) {
        return out;
    }

    // GetRotation reports success in PositionOnly mode too, handing back zeros. Taking
    // that as a rotation channel let the hook run its rotation path with no head input
    // at all, which still rewrote the engine's pitch through the clamp - the mod moving
    // the view while the player had rotation switched off.
    out.has_rotation = m_session.IsRotationActive() &&
                       m_session.GetRotation(out.yaw, out.pitch, out.roll);
    if (out.has_rotation && !AllFinite(out.yaw, out.pitch, out.roll)) {
        out.has_rotation = false;
        ReportNonFinite("head rotation");
    }

    out.has_position = m_session.GetPositionOffset(out.pos_x, out.pos_y, out.pos_z);
    if (out.has_position && !AllFinite(out.pos_x, out.pos_y, out.pos_z)) {
        out.has_position = false;
        ReportNonFinite("head position");
    }
    return out;
}

}  // namespace DishonoredHeadTracking
