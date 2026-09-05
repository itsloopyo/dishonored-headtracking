// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

#include <atomic>

namespace DishonoredHeadTracking {

// One frame's processed head pose: rotation in degrees (YPR) and position offset in
// metres, in the CORE's basis: x = right, y = up, and z where NEGATIVE is the forward
// lean. That z sign is what puts the generous LimitZ on leaning in and the restricted
// LimitZBack on pulling away (PositionProcessor clamps to [-limit_z, +limit_z_back]),
// and it is why camera_hook.cpp negates z at the engine boundary rather than here.
//
// has_* report whether each channel produced fresh data this frame.
struct FrameSample {
    bool  has_rotation = false;
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    bool  has_position = false;
    float pos_x = 0.0f, pos_y = 0.0f, pos_z = 0.0f;
};

class TrackingRuntime {
public:
    TrackingRuntime() : m_session(m_receiver) {}

    // Applies @p cfg and brings the UDP receiver up. A port that will not bind yet is
    // not a failure: the receiver retries in the background, so there is no start
    // outcome for a caller to branch on.
    void Start(const Config& cfg);
    void Stop();

    // Runs the per-frame pipeline once and returns the processed pose.
    //
    // Called from the camera hook on the GAME thread, and only for the scene-view
    // caller. That restriction is load-bearing: the frame clock and the session are
    // stateful and not reentrant, and it is what keeps them single-threaded.
    FrameSample SampleFrame();

    // Reports whether the UDP receiver is currently observing packets.
    bool IsReceiving() const { return m_receiver.IsReceiving(); }

    void ToggleEnabled();
    void CycleTrackingMode();
    void ToggleYawMode();

    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(std::memory_order_relaxed); }

private:
    static constexpr float kMaxFrameDtSec = 0.25f;

    void ConfigureRotation();
    void ConfigurePosition();
    void ConfigureSmoothing();

    Config m_cfg{};
    cameraunlock::UdpReceiver m_receiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session;
    cameraunlock::time::FrameClock m_clock{kMaxFrameDtSec};

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_worldSpaceYaw{true};
};

}  // namespace DishonoredHeadTracking
