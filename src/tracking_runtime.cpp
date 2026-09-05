// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "tracking_runtime.h"

#include "logging.h"
#include "ue3_math.h"

namespace DishonoredHeadTracking {

void TrackingRuntime::ConfigureRotation() {
    cameraunlock::SensitivitySettings sens;
    sens.yaw = m_cfg.sens_yaw;
    sens.pitch = m_cfg.sens_pitch;
    sens.roll = m_cfg.sens_roll;
    sens.invert_yaw = m_cfg.invert_yaw;
    sens.invert_pitch = m_cfg.invert_pitch;
    sens.invert_roll = m_cfg.invert_roll;
    m_session.GetProcessor().SetSensitivity(sens);
}

void TrackingRuntime::ConfigurePosition() {
    cameraunlock::PositionSettings pos;
    pos.sensitivity_x = m_cfg.pos_sens_x;
    pos.sensitivity_y = m_cfg.pos_sens_y;
    pos.sensitivity_z = m_cfg.pos_sens_z;
    pos.limit_x = m_cfg.pos_limit_x;
    pos.limit_y = m_cfg.pos_limit_y;
    // The INI exposes one vertical limit, so mirror it into the downward bound the way
    // PositionSettings::Symmetric does. Leaving limit_y_down at its struct default made
    // a raised LimitY grow the upward budget only, silently and with nothing in the INI
    // to explain the asymmetry.
    pos.limit_y_down = m_cfg.pos_limit_y;
    pos.limit_z = m_cfg.pos_limit_z;
    pos.limit_z_back = m_cfg.pos_limit_z_back;
    pos.invert_x = m_cfg.invert_pos_x;
    pos.invert_y = m_cfg.invert_pos_y;
    pos.invert_z = m_cfg.invert_pos_z;
    m_session.GetPositionProcessor().SetSettings(pos);
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

    ConfigureRotation();
    ConfigurePosition();
    ConfigureSmoothing();

    m_enabled.store(m_cfg.enabled_on_startup, std::memory_order_relaxed);
    m_worldSpaceYaw.store(m_cfg.world_space_yaw, std::memory_order_relaxed);
    m_session.SetMode(m_cfg.position_enabled
                          ? cameraunlock::TrackingMode::RotationAndPosition
                          : cameraunlock::TrackingMode::RotationOnly);

    m_receiver.SetLog([](const std::string& msg) {
        Log::Line("UDP: %s", msg.c_str());
    });

    if (m_receiver.Start(m_cfg.udp_port)) {
        Log::Line("UDP receiver listening on port %u", m_cfg.udp_port);
    } else {
        Log::Line("WARN: UDP receiver did not bind immediately on port %u; background retry active", m_cfg.udp_port);
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

void TrackingRuntime::CycleTrackingMode() {
    switch (m_session.CycleMode()) {
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
}

void TrackingRuntime::ToggleYawMode() {
    bool prev = m_worldSpaceYaw.load(std::memory_order_relaxed);
    m_worldSpaceYaw.store(!prev, std::memory_order_relaxed);
    Log::Line("Yaw mode: %s", !prev ? "world-space (horizon-locked)" : "camera-local");
}

// The pipeline's own finite checks stop at the wire: OpenTrackPacket rejects a
// non-finite datagram, but the sensitivities the processor multiplies it by come from
// the INI, where magnitude is deliberately unbounded because a large one is legitimate
// tuning. A large enough sensitivity overflows the product to infinity, and from here
// it would reach the FRotator conversion and the camera location. Drop the channel
// instead, and say so once - a silently dropped pose reads in game exactly like a
// tracker that has stopped sending.
namespace {
void ReportNonFinite(const char* channel) {
    static bool s_warned = false;
    if (s_warned) return;
    s_warned = true;
    Log::Line("WARN: the processed %s came out non-finite and is being dropped. Check "
              "the Sensitivity and Position values in the INI: a large enough one "
              "overflows the pose it multiplies.", channel);
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
