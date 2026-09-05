// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "aim_marker.h"

namespace DishonoredHeadTracking {

AimMarker& GetAimMarker() {
    static AimMarker marker;
    return marker;
}

}  // namespace DishonoredHeadTracking
