// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
//
// Behaviour locks for the parts of the mod that are pure arithmetic: the INI boundary
// sanitizers, the FOV range every hook shares, the UE3 rotator/matrix conversions the
// camera hook injects through, and the projection that places the game's crosshair on
// the aim point.
//
// Everything here ran inside a detour before it was extracted. These tests pin the
// numbers that came out of it, so a later change to the shape of the code cannot move
// the camera or the crosshair without a test saying so.

#include "aim_projection.h"
#include "camera_collision.h"
#include "config.h"
#include "config_sanitize.h"
#include "fov_range.h"
#include "heap_ptr.h"
#include "ue3_math.h"
#include "zoom_compensation.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

using namespace DishonoredHeadTracking;

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

void CheckNear(float actual, float expected, float eps, const char* what) {
    if (!(std::fabs(actual - expected) <= eps)) {
        std::printf("  FAIL: %s (got %.6f, expected %.6f)\n", what, actual, expected);
        ++g_failures;
    }
}

const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// The shipped default of each smoothing key. They are not the same number, and
// that is the whole point of the fallback argument.
const float kLocalDefault  = 0.0f;
const float kRemoteDefault = 0.15f;

void SanitizeTests() {
    std::printf("config sanitizers\n");

    Check(SanitizeSmoothing(0.0f, kLocalDefault) == 0.0f, "smoothing 0 passes through");
    Check(SanitizeSmoothing(0.5f, kLocalDefault) == 0.5f, "smoothing in range passes through");
    Check(SanitizeSmoothing(1.0f, kLocalDefault) == 1.0f, "smoothing 1 passes through");

    // A configured zero is a real setting - track me with no added latency - and it has
    // to reach the processor as written. Nothing here may raise it, least of all on the
    // remote key, whose fallback is 0.15.
    Check(SanitizeSmoothing(0.0f, kRemoteDefault) == 0.0f,
          "a configured 0 survives verbatim on the remote key, never floored to 0.15");

    Check(SanitizeSmoothing(-1.0f, kLocalDefault) == 0.0f, "smoothing below range saturates at 0");
    Check(SanitizeSmoothing(4.0f, kLocalDefault) == 1.0f, "smoothing above range saturates at 1");

    // A malformed value lands on the default of the key it was read for, not on a
    // shared one: a broken RemoteSmoothing must not hand a phone-over-WiFi user the
    // local "no smoothing at all".
    Check(SanitizeSmoothing(kNan, kRemoteDefault) == kRemoteDefault,
          "NaN smoothing falls back to that key's own default");
    Check(SanitizeSmoothing(kInf, kLocalDefault) == kLocalDefault,
          "Inf smoothing falls back to that key's own default");

    // Sensitivities, position limits and PositionScale are only finite-checked.
    // Magnitude and sign are legitimate tuning, so nothing about them is clamped.
    Check(SanitizeFinite(2.5f, 1.0f) == 2.5f, "sensitivity above 1 is not clamped");
    Check(SanitizeFinite(-1.0f, 1.0f) == -1.0f, "a negative sensitivity is left alone");
    Check(SanitizeFinite(kNan, 1.0f) == 1.0f, "NaN sensitivity falls back to 1");
    Check(SanitizeFinite(-kInf, 1.0f) == 1.0f, "Inf sensitivity falls back to 1");

    // PositionScale multiplies the clamped lean into the camera location, so its own
    // fallback has to hold: a NaN here would move the viewpoint to nowhere every frame.
    Check(SanitizeFinite(kInf, 100.0f) == 100.0f, "Inf PositionScale falls back to 100");

    Check(SanitizeFinite(3.0f, 9.0f) == 3.0f, "finite value passes through");
    Check(SanitizeFinite(kNan, 9.0f) == 9.0f, "NaN takes the fallback");
}

void FovRangeTests() {
    std::printf("fov range\n");

    Check(IsUsableFov(kMinFovDegrees), "the lower bound is usable");
    Check(IsUsableFov(kMaxFovDegrees), "the upper bound is usable");
    Check(IsUsableFov(75.0f), "a normal field of view is usable");
    Check(!IsUsableFov(kMinFovDegrees - 0.1f), "just under the lower bound is not");
    Check(!IsUsableFov(kMaxFovDegrees + 0.1f), "just over the upper bound is not");
    Check(!IsUsableFov(0.0f), "zero is not a renderable field of view");
    Check(!IsUsableFov(kNan), "NaN is not usable");
    Check(!IsUsableFov(kInf), "Inf is not usable");
}

void RotatorUnitTests() {
    std::printf("UE3 rotator units\n");

    // 65536 units to the revolution is the whole basis of the FRotator arithmetic the
    // camera hook does.
    Check(DegToUnits(0.0f) == 0, "0 degrees is 0 units");
    Check(DegToUnits(90.0f) == 16384, "90 degrees is a quarter turn");
    Check(DegToUnits(-90.0f) == -16384, "-90 degrees is a quarter turn the other way");
    Check(DegToUnits(360.0f) == 65536, "360 degrees is a full turn");
    Check(DegToUnits(1.0f) == 182, "a degree rounds to 182 units");

    // Stated values, not the implementation's own expression: an assertion that
    // recomputes the conversion cannot notice the conversion being wrong.
    Check(DegToUnits(-45.5f) == -8283, "an ordinary negative angle converts exactly");
    Check(DegToUnits(45.5f) == 8283, "and so does its mirror");
    Check(DegToUnits(180.0f) == 32768, "half a turn");
    Check(DegToUnits(-360.0f) == -65536, "a full turn the other way");

    // lround rounds half away from zero. 180/65536 degrees is exactly half a unit and
    // is exactly representable, so this pins the rounding mode rather than sampling it.
    constexpr float kHalfUnitDeg = 180.0f / 65536.0f;
    Check(DegToUnits(kHalfUnitDeg) == 1, "half a unit rounds away from zero");
    Check(DegToUnits(-kHalfUnitDeg) == -1, "and does so symmetrically");

    // The reduction guard, pinned on both sides of the threshold it documents.
    Check(DegToUnits(32767.9f) == 5965214, "just below the threshold nothing is reduced");
    Check(DegToUnits(32768.0f) == 1456, "at the threshold the angle reduces by whole turns");
    Check(DegToUnits(1.0e30f) >= -65536 && DegToUnits(1.0e30f) <= 65536,
          "the reduced result stays within one turn of units");
    Check(DegToUnits(720.0f) == 131072, "two turns below the threshold are not reduced");

    // Degrees survive the trip into units and back to within the resolution of a unit.
    const float kRoundTrip[] = { 0.0f, 1.0f, 12.34f, -7.5f, 89.9f };
    for (float deg : kRoundTrip) {
        CheckNear(DegToUnits(deg) * 360.0f / 65536.0f, deg, 0.0028f,
                  "degrees round-trip through units");
    }

    Check(RadToUnits(0.0f) == 0, "0 radians is 0 units");
    Check(RadToUnits(static_cast<float>(cameraunlock::math::kPi)) == 32768,
          "pi radians is half a turn");
    Check(RadToUnits(-static_cast<float>(cameraunlock::math::kPi) * 0.5f) == -16384,
          "a negative quarter turn keeps its sign");

    // A rotator is modular and UE3 hands back both representations. The pitch bound is
    // the one place that compares against a limit, so it has to see the signed form:
    // against the positive-wrapped one, every downward look reads as far past the limit.
    Check(NormalizeUnits(0) == 0, "zero normalises to zero");
    Check(NormalizeUnits(16384) == 16384, "a quarter turn up is already signed");
    Check(NormalizeUnits(60000) == -5536, "a positive-wrapped downward look comes back negative");
    Check(NormalizeUnits(65536) == 0, "a whole turn is no rotation");
    Check(NormalizeUnits(-5536) == -5536, "an already-signed value is untouched");

    // The head only gets the pitch the view has room for. Bounding the CONTRIBUTION is
    // what lets both yaw modes share one limit.
    Check(BoundedPitchContribution(0, 30.0f) == DegToUnits(30.0f),
          "a level view has room for the whole head pitch");
    Check(BoundedPitchContribution(16000, 30.0f) == 384,
          "a view already near vertical only has room for the rest");
    Check(BoundedPitchContribution(60000, 0.0f) == 0,
          "a positive-wrapped clean pitch is bounded against its signed value");

    // The pitch clamp is what stops the view snapping upside-down and backwards past a
    // quarter turn, where cos(pitch) goes negative.
    Check(ClampPitchUnits(0) == 0, "an upright pitch is untouched");
    Check(ClampPitchUnits(kMaxPitchUnits) == kMaxPitchUnits, "the limit itself is untouched");
    Check(ClampPitchUnits(kMaxPitchUnits + 1) == kMaxPitchUnits, "looking further up is pinned");
    Check(ClampPitchUnits(-kMaxPitchUnits - 1) == -kMaxPitchUnits, "looking further down is pinned");
}

void FiniteSampleTests() {
    std::printf("engine boundary finiteness\n");

    // What the camera hook is allowed to hand the engine. DegToUnits truncates to
    // int32 and the position offset is added straight into the camera location, so one
    // non-finite component is a black screen with nothing in the log.
    Check(AllFinite(0.0f, 0.0f, 0.0f), "a zero pose is finite");
    Check(AllFinite(-12.5f, 3.0f, 0.25f), "an ordinary pose is finite");
    Check(!AllFinite(kNan, 0.0f, 0.0f), "a NaN yaw is rejected");
    Check(!AllFinite(0.0f, kInf, 0.0f), "an infinite pitch is rejected");
    Check(!AllFinite(0.0f, 0.0f, -kInf), "a negatively infinite roll is rejected");
}

void HeapPointerTests() {
    std::printf("heap pointer guard\n");

    // Both the camera hook's menu walk and the FOV hook's PlayerCamera read follow raw
    // engine pointers. Anything that is not a plausible, aligned UE3 heap object is a
    // wild read on the game thread, so both go through this one predicate.
    Check(LooksLikeHeapPtr(kMinHeapAddress), "the low bound is a plausible object");
    Check(LooksLikeHeapPtr(0x0A123454u), "a normal aligned heap address is plausible");
    Check(!LooksLikeHeapPtr(0u), "null is not");
    Check(!LooksLikeHeapPtr(kMinHeapAddress - 4u), "below the heap span is not");
    Check(!LooksLikeHeapPtr(0xFFFFFFFCu), "a kernel-range value is not");

    // The upper bound comes from the OS, never a constant. Dishonored.exe is linked
    // LARGE_ADDRESS_AWARE, so its allocations run past the 2 GB line; a hard-coded
    // 0x7F000000 ceiling called every one of those a wild pointer and silently switched
    // head tracking off for the rest of the session once the low half fragmented.
    //
    // This binary is linked /LARGEADDRESSAWARE too (see CMakeLists.txt), so the bound it
    // reads is the same one the game gets. Without that the test would sit at the 2 GB
    // ceiling and pass no matter what.
    Check(MaxHeapAddress() > 0x80000000u,
          "the bound reflects a large-address-aware process, not the 2 GB default");
    Check(LooksLikeHeapPtr(0x7F000000u),
          "an address above the old hard-coded ceiling is a plausible object");
    Check(LooksLikeHeapPtr(0xC0000000u),
          "and so is one well into the upper half of the address space");
    Check(LooksLikeHeapPtr(MaxHeapAddress() & ~3u),
          "the top accepted address is usable");
    // The ceiling stops short of the true maximum because every caller dereferences
    // ptr + offset without validating again.
    Check(!LooksLikeHeapPtr(0xFFFEFFFCu),
          "the last page before the OS ceiling is refused, leaving room for the offsets "
          "the hooks add to a validated pointer");
    // A half-written pointer field is the case that matters: it lands in range but
    // unaligned, which a bare `cam != 0` check let straight through.
    Check(!LooksLikeHeapPtr(0x0A123456u), "an unaligned address in range is not");
}

void RotationMatrixTests() {
    std::printf("UE3 rotation matrices\n");

    const Mat3 identity = RotatorToMatrix(UE3Rotator{ 0, 0, 0 });
    CheckNear(identity.m[0][0], 1.0f, 1e-6f, "identity forward is +X");
    CheckNear(identity.m[1][1], 1.0f, 1e-6f, "identity right is +Y");
    CheckNear(identity.m[2][2], 1.0f, 1e-6f, "identity up is +Z");

    // Row 0 is the forward axis. A quarter turn of yaw swings it from +X to +Y, which is
    // what makes plain FRotator yaw addition horizon-locked.
    const Mat3 yawed = RotatorToMatrix(UE3Rotator{ 0, 16384, 0 });
    CheckNear(yawed.m[0][0], 0.0f, 1e-5f, "yaw 90 leaves no forward X");
    CheckNear(yawed.m[0][1], 1.0f, 1e-5f, "yaw 90 points forward along +Y");

    // Positive pitch looks up: the forward axis gains +Z.
    const Mat3 pitched = RotatorToMatrix(UE3Rotator{ 8192, 0, 0 });
    CheckNear(pitched.m[0][2], std::sin(static_cast<float>(cameraunlock::math::kPi) * 0.25f),
              1e-5f, "pitch 45 lifts the forward axis by sin(45)");

    // Every basis the injection composes has to stay orthonormal, or the aim direction
    // resolved through it stops being a direction.
    const Mat3 M = RotatorToMatrix(UE3Rotator{ 3000, -12000, 5000 });
    for (int row = 0; row < 3; ++row) {
        const float len = std::sqrt(M.m[row][0] * M.m[row][0] + M.m[row][1] * M.m[row][1] +
                                    M.m[row][2] * M.m[row][2]);
        CheckNear(len, 1.0f, 1e-5f, "each basis row is unit length");
    }
    CheckNear(M.m[0][0] * M.m[1][0] + M.m[0][1] * M.m[1][1] + M.m[0][2] * M.m[1][2],
              0.0f, 1e-5f, "forward and right stay perpendicular");
    CheckNear(M.m[0][0] * M.m[2][0] + M.m[0][1] * M.m[2][1] + M.m[0][2] * M.m[2][2],
              0.0f, 1e-5f, "forward and up stay perpendicular");

    // The marker projects the clean aim through RotatorForward rather than building a
    // whole basis for it. It has to stay exactly row 0 of the matrix the injection
    // composes through, or the crosshair and the camera part company.
    const UE3Rotator kForwardCases[] = { { 0, 0, 0 }, { 3000, -12000, 5000 },
                                         { -16384, 30000, -20000 } };
    for (const UE3Rotator& r : kForwardCases) {
        const Mat3 basis = RotatorToMatrix(r);
        float fwd[3];
        RotatorForward(r, fwd);
        CheckNear(fwd[0], basis.m[0][0], 1e-6f, "RotatorForward X matches the basis forward");
        CheckNear(fwd[1], basis.m[0][1], 1e-6f, "RotatorForward Y matches the basis forward");
        CheckNear(fwd[2], basis.m[0][2], 1e-6f, "RotatorForward Z matches the basis forward");
    }

    // Composing with an identity head rotation must return the game's own rotator
    // unchanged: camera-local yaw mode runs this path on every frame, and any drift here
    // would ratchet the view around while the player holds still.
    const UE3Rotator clean{ 2000, -9000, 1500 };
    const Mat3 head = RotatorToMatrix(0.0f, 0.0f, 0.0f);
    UE3Rotator out{};
    MatrixToRotator(MatMul(head, RotatorToMatrix(clean)), &out);
    Check(std::abs(out.Pitch - clean.Pitch) <= 1, "identity head rotation preserves pitch");
    Check(std::abs(out.Yaw - clean.Yaw) <= 1, "identity head rotation preserves yaw");
    Check(std::abs(out.Roll - clean.Roll) <= 1, "identity head rotation preserves roll");

    // A head yaw applied camera-local, on a level view, is the same quarter turn plain
    // addition would have produced.
    const Mat3 headYaw = RotatorToMatrix(0.0f, 90.0f * kDegToRad, 0.0f);
    MatrixToRotator(MatMul(headYaw, RotatorToMatrix(UE3Rotator{ 0, 0, 0 })), &out);
    Check(std::abs(out.Yaw - 16384) <= 1, "a 90 degree head yaw is a quarter turn");
    Check(std::abs(out.Pitch) <= 1, "a pure head yaw adds no pitch");
    Check(std::abs(out.Roll) <= 1, "a pure head yaw adds no roll");
}

void ViewRectTests() {
    std::printf("view rect\n");

    const ViewRect full = ComputeViewRect(1920.0f, 1080.0f, 0.0f);
    Check(full.x == 0.0f && full.y == 0.0f && full.w == 1920.0f && full.h == 1080.0f,
          "an unconstrained camera renders into the whole viewport");

    // A constrained ratio WIDER than the viewport takes height and centres the result:
    // letterbox bars top and bottom.
    const ViewRect wide = ComputeViewRect(1920.0f, 1080.0f, 2.39f);
    CheckNear(wide.w, 1920.0f, 1e-3f, "a wider constrained ratio keeps the full width");
    CheckNear(wide.h, 1920.0f / 2.39f, 1e-3f, "a wider constrained ratio loses height");
    CheckNear(wide.y, (1080.0f - 1920.0f / 2.39f) * 0.5f, 1e-3f, "and is centred vertically");
    CheckNear(wide.x, 0.0f, 1e-6f, "with no horizontal offset");

    // A NARROWER ratio takes width instead: pillarbox bars left and right.
    const ViewRect narrow = ComputeViewRect(1920.0f, 1080.0f, 1.0f);
    CheckNear(narrow.h, 1080.0f, 1e-3f, "a narrower constrained ratio keeps the full height");
    CheckNear(narrow.w, 1080.0f, 1e-3f, "a narrower constrained ratio loses width");
    CheckNear(narrow.x, (1920.0f - 1080.0f) * 0.5f, 1e-3f, "and is centred horizontally");

    // Below kMinConstrainedAspect, and for a non-finite ratio, the camera is not
    // constraining anything.
    const ViewRect zero = ComputeViewRect(1920.0f, 1080.0f, 0.001f);
    Check(zero.w == 1920.0f && zero.h == 1080.0f, "a near-zero ratio is not a constraint");
    const ViewRect nan = ComputeViewRect(1920.0f, 1080.0f, kNan);
    Check(nan.w == 1920.0f && nan.h == 1080.0f, "a non-finite ratio is not a constraint");

    // A ratio outside the range a real constrained view uses is a mis-read of the camera
    // rather than a cinematic. Believing one collapses the rect to a sliver and pins the
    // crosshair inside a band across the middle of the screen.
    const ViewRect absurdWide = ComputeViewRect(1920.0f, 1080.0f, 100.0f);
    Check(absurdWide.w == 1920.0f && absurdWide.h == 1080.0f,
          "an implausibly wide ratio is not believed");
    const ViewRect absurdNarrow = ComputeViewRect(1920.0f, 1080.0f, 0.02f);
    Check(absurdNarrow.w == 1920.0f && absurdNarrow.h == 1080.0f,
          "nor an implausibly narrow one");
    const ViewRect negative = ComputeViewRect(1920.0f, 1080.0f, -2.39f);
    Check(negative.w == 1920.0f && negative.h == 1080.0f, "nor a negative one");
    const ViewRect equal = ComputeViewRect(1920.0f, 1080.0f, 1920.0f / 1080.0f);
    CheckNear(equal.w, 1920.0f, 1e-3f, "a ratio equal to the viewport takes the full width");
    CheckNear(equal.h, 1080.0f, 1e-3f, "and the full height");

    Check(IsUsableViewport(1920.0f, 1080.0f), "a normal resolution is usable");
    Check(!IsUsableViewport(kMinViewportPixels - 1.0f, 1080.0f), "an impossibly small width is not");
    Check(!IsUsableViewport(1920.0f, kMaxViewportPixels + 1.0f), "an impossibly large height is not");
    Check(!IsUsableViewport(kNan, 1080.0f), "a non-finite width is not");
}

void AimProjectionTests() {
    std::printf("aim projection\n");

    const ViewRect rect = ComputeViewRect(1920.0f, 1080.0f, 0.0f);
    AimPixel px{};

    // Aim straight down the view axis: the crosshair belongs at the centre of the rect,
    // which is where the game draws it unmodified.
    Check(ProjectAimToPixels(rect, 75.0f, 0.0f, 0.0f, 1.0f, &px), "a centred aim projects");
    CheckNear(px.x, 960.0f, 1e-3f, "a centred aim sits at half the width");
    CheckNear(px.y, 540.0f, 1e-3f, "a centred aim sits at half the height");
    CheckNear(px.ndc_x, 0.0f, 1e-6f, "a centred aim is NDC 0 horizontally");
    CheckNear(px.ndc_y, 0.0f, 1e-6f, "a centred aim is NDC 0 vertically");
    Check(!px.clamped, "a centred aim is not clamped");

    // An aim exactly half the horizontal field to the right is NDC +1: the right edge,
    // which is outside the inset and therefore reported as clamped.
    const float halfFov = 75.0f * 0.5f * static_cast<float>(cameraunlock::math::kPi) / 180.0f;
    Check(ProjectAimToPixels(rect, 75.0f, std::tan(halfFov), 0.0f, 1.0f, &px),
          "an aim at the edge of the field projects");
    CheckNear(px.ndc_x, 1.0f, 1e-4f, "half the horizontal field is NDC 1");
    Check(px.clamped, "an aim at the frame edge is reported clamped");
    CheckNear(px.x, 1920.0f - 1080.0f * kEdgeInsetFraction, 1e-3f,
              "and is pinned inside the right edge by the inset");

    // Screen y runs downward, so looking up moves the crosshair up the screen.
    Check(ProjectAimToPixels(rect, 75.0f, 0.0f, 0.1f, 1.0f, &px), "an upward aim projects");
    Check(px.y < 540.0f, "a positive up component moves the crosshair up the screen");
    CheckNear(px.x, 960.0f, 1e-3f, "and leaves it horizontally centred");

    // The vertical scale comes from the rect's own aspect, not the viewport's, so a
    // letterboxed cinematic camera still lands the crosshair on the shot.
    const ViewRect wide = ComputeViewRect(1920.0f, 1080.0f, 2.39f);
    AimPixel widePx{};
    Check(ProjectAimToPixels(wide, 75.0f, 0.0f, 0.1f, 1.0f, &widePx),
          "an aim projects inside a constrained view");
    Check(widePx.ndc_y > px.ndc_y,
          "the same upward aim reads higher in NDC when the view rect is shorter");
    CheckNear(widePx.x, 960.0f, 1e-3f, "a constrained view is still centred horizontally");

    // An aim off the FRAME is pinned to the edge, never refused. Refusing it is not
    // neutral: the HUD eases the crosshair back to viewport centre every frame, so
    // declining to place it parks it in the middle of the screen asserting the shot
    // lands dead ahead while it lands up to 180 degrees away.
    Check(ProjectAimToPixels(rect, 75.0f, 1.0f, 0.0f, 0.05f, &px),
          "an aim past the forward guard still places the crosshair");
    CheckNear(px.x, 1920.0f - 16.2f, 1e-3f, "pinned inside the right edge");
    CheckNear(px.y, 540.0f, 1e-3f, "at the height the aim points to");
    Check(px.clamped, "and reported as clamped");

    Check(ProjectAimToPixels(rect, 75.0f, -1.0f, 0.0f, -0.5f, &px),
          "an aim BEHIND the view still places the crosshair on the side it points to");
    CheckNear(px.x, 16.2f, 1e-3f, "pinned inside the left edge");
    Check(px.clamped, "and reported as clamped");

    Check(ProjectAimToPixels(rect, 75.0f, 0.0f, 1.0f, 0.0f, &px),
          "an aim exactly across the view still places the crosshair");
    CheckNear(px.x, 960.0f, 1e-3f, "horizontally centred");
    CheckNear(px.y, 16.2f, 1e-3f, "and pinned inside the top edge");

    // The pinned position has to be the LIMIT of the on-frame projection, not a
    // separately-derived direction. Normalising (right, up) instead drops the FOV and
    // aspect terms, which moved the crosshair hundreds of pixels along the edge as a
    // head turn swept the aim across the guard.
    {
        AimPixel justInside{}, justOutside{};
        const float f = kMinForwardComponent;
        Check(ProjectAimToPixels(rect, 75.0f, 0.05f, 0.9937f, f * 1.0005f, &justInside),
              "an aim just inside the forward guard projects");
        Check(ProjectAimToPixels(rect, 75.0f, 0.05f, 0.9937f, f, &justOutside),
              "and so does the same aim at the guard");
        CheckNear(justOutside.x, justInside.x, 1.0f,
                  "the crosshair does not jump horizontally as the aim crosses the guard");
        CheckNear(justOutside.y, justInside.y, 1.0f,
                  "nor vertically");

        // A mostly-horizontal aim pins to the side edge at the height it points to, and
        // a mostly-vertical one to the top edge at the width it points to. Normalising
        // the direction instead drove both into the corner, because it dropped the FOV
        // and aspect terms that decide how far off centre a given component really is.
        AimPixel sideways{}, upward{};
        Check(ProjectAimToPixels(rect, 75.0f, 0.99f, 0.01f, f, &sideways), "projects");
        Check(ProjectAimToPixels(rect, 75.0f, 0.01f, 0.99f, f, &upward), "projects");
        CheckNear(sideways.x, 1920.0f - 16.2f, 1e-3f, "a sideways aim pins to the side edge");
        Check(sideways.y > 16.2f && sideways.y < 1080.0f - 16.2f,
              "at the height it points to, not driven into a corner");
        CheckNear(upward.y, 16.2f, 1e-3f, "an upward aim pins to the top edge");
        Check(upward.x > 16.2f && upward.x < 1920.0f - 16.2f,
              "at the width it points to, not driven into a corner");
    }

    // The pin is to the rendered image, not the viewport: on a letterboxed view the
    // crosshair must never be parked in the black bar.
    const ViewRect wideRect = ComputeViewRect(1920.0f, 1080.0f, 2.39f);
    Check(ProjectAimToPixels(wideRect, 75.0f, 0.0f, -1.0f, 0.05f, &px),
          "a downward off-frame aim projects inside a letterboxed view");
    const float wideInset = (wideRect.w < wideRect.h ? wideRect.w : wideRect.h) * kEdgeInsetFraction;
    CheckNear(px.y, wideRect.y + wideRect.h - wideInset, 1e-3f,
              "pinned to the bottom of the image, not the bottom of the viewport");
    Check(px.y < 1080.0f - 16.2f, "which is strictly above the viewport's own edge");

    // A rect too narrow to hold the inset twice must not invert the clamp bounds and
    // place the crosshair outside the rect entirely.
    const ViewRect sliver = { 954.06f, 0.0f, 11.88f, 1080.0f };
    Check(ProjectAimToPixels(sliver, 75.0f, 0.0f, 0.0f, 1.0f, &px),
          "a centred aim projects even on a sliver of a rect");
    Check(px.x >= sliver.x && px.x <= sliver.x + sliver.w,
          "and stays inside it rather than being pushed out by the inset");

    // Refusals. Each one leaves the game's own centred crosshair alone rather than
    // placing a marker from a number that cannot be trusted.
    Check(!ProjectAimToPixels(rect, 0.0f, 0.0f, 0.0f, 1.0f, &px),
          "a field of view outside the usable range does not project");
    Check(!ProjectAimToPixels(rect, kNan, 0.0f, 0.0f, 1.0f, &px),
          "a non-finite field of view does not project");
    Check(!ProjectAimToPixels(rect, 75.0f, 0.0f, 0.0f, kMinForwardComponent, &px),
          "an aim at the forward-component guard does not project");
    Check(!ProjectAimToPixels(rect, 75.0f, 0.0f, 0.0f, -1.0f, &px),
          "an aim EXACTLY behind has no screen direction to pin to, so it does not project");
    Check(!ProjectAimToPixels(rect, 75.0f, kNan, 0.0f, 1.0f, &px),
          "a non-finite aim direction does not project");
}

// The AGENTS.md reticle litmus tests. These are the ones that matter: the projection
// downstream of them only turns a direction into a pixel, so every question about WHICH
// WAY the crosshair moves is answered here.
void AimDirectionTests() {
    std::printf("aim direction (reticle litmus)\n");

    const ViewRect full = ComputeViewRect(1920.0f, 1080.0f, 0.0f);
    const UE3Rotator level{ 0, 0, 0 };
    float ruf[3];
    AimPixel px{};

    // (a) Pure roll leaves the aim dead centre, at any roll. UE3 rolls about the view's
    // own forward axis, so the clean forward and the tracked forward stay identical.
    for (float rollDeg : { 10.0f, 30.0f, -45.0f }) {
        const UE3Rotator tracked{ 0, 0, DegToUnits(rollDeg) };
        ResolveAimInTrackedView(level, tracked, ruf);
        CheckNear(ruf[0], 0.0f, 1e-6f, "pure roll adds no horizontal offset");
        CheckNear(ruf[1], 0.0f, 1e-6f, "pure roll adds no vertical offset");
        CheckNear(ruf[2], 1.0f, 1e-6f, "pure roll leaves the aim straight ahead");
        Check(ProjectAimToPixels(full, 75.0f, ruf[0], ruf[1], ruf[2], &px), "and it projects");
        CheckNear(px.x, 960.0f, 1e-3f, "so the crosshair stays at screen centre");
        CheckNear(px.y, 540.0f, 1e-3f, "on both axes");
    }

    // (b) Pure pitch moves it purely vertically, and the sign is the point: looking UP
    // leaves the level gun pointing BELOW the centre of the view.
    ResolveAimInTrackedView(level, UE3Rotator{ DegToUnits(15.0f), 0, 0 }, ruf);
    CheckNear(ruf[0], 0.0f, 1e-6f, "pure pitch adds no horizontal offset");
    CheckNear(ruf[1], -0.258850f, 1e-5f, "looking up puts the aim below the view axis");
    Check(ProjectAimToPixels(full, 75.0f, ruf[0], ruf[1], ruf[2], &px), "it projects");
    CheckNear(px.x, 960.0f, 1e-3f, "the crosshair stays horizontally centred");
    CheckNear(px.y, 875.2731f, 1e-3f, "and drops below centre");

    ResolveAimInTrackedView(level, UE3Rotator{ DegToUnits(-15.0f), 0, 0 }, ruf);
    CheckNear(ruf[1], 0.258850f, 1e-5f, "looking down puts the aim above the view axis");
    Check(ProjectAimToPixels(full, 75.0f, ruf[0], ruf[1], ruf[2], &px), "it projects");
    CheckNear(px.y, 204.7269f, 1e-3f, "and it rises by the same amount");

    // (c) Pitch and roll together. UE3 composes roll OUTERMOST (about the view forward
    // axis), so the offset rotates rigidly about the centre rather than staying vertical.
    // A "no horizontal wander" expectation belongs to roll-innermost engines and would be
    // asserting the wrong behaviour here. What must hold is that the distance off centre
    // does not change - that is what keeps the crosshair glued to the aim point.
    struct RollCase { float roll, r, u, x, y; };
    const RollCase kRollCases[] = {
        {  0.0f, 0.000000f, -0.258850f,  960.0000f, 875.2731f },
        { 30.0f, 0.129418f, -0.224175f, 1127.6273f, 830.3604f },
        { 60.0f, 0.224175f, -0.129418f, 1250.3604f, 707.6273f },
        { 90.0f, 0.258850f,  0.000000f, 1295.2731f, 540.0000f },
    };
    for (const RollCase& c : kRollCases) {
        const UE3Rotator tracked{ DegToUnits(15.0f), 0, DegToUnits(c.roll) };
        ResolveAimInTrackedView(level, tracked, ruf);
        CheckNear(ruf[0], c.r, 1e-5f, "roll rotates the horizontal part of the offset");
        CheckNear(ruf[1], c.u, 1e-5f, "and the vertical part with it");
        CheckNear(std::sqrt(ruf[0] * ruf[0] + ruf[1] * ruf[1]), 0.258850f, 1e-5f,
                  "without changing how far off centre the aim sits");
        CheckNear(ruf[2], 0.965918f, 1e-5f, "or the forward component");
        Check(ProjectAimToPixels(full, 75.0f, ruf[0], ruf[1], ruf[2], &px), "it projects");
        CheckNear(px.x, c.x, 1e-3f, "the crosshair follows it horizontally");
        CheckNear(px.y, c.y, 1e-3f, "and vertically");
    }

    // (d) World-space yaw looking straight down. Head yaw is then a pure spin about the
    // view axis, so the world turns and the crosshair must not move at all.
    const UE3Rotator down{ -16384, 0, 0 };
    for (float yawDeg : { 0.0f, 45.0f, 90.0f }) {
        const UE3Rotator tracked{ -16384, DegToUnits(yawDeg), 0 };
        ResolveAimInTrackedView(down, tracked, ruf);
        CheckNear(ruf[0], 0.0f, 1e-5f, "yawing while looking down moves nothing sideways");
        CheckNear(ruf[1], 0.0f, 1e-5f, "nor vertically");
        CheckNear(ruf[2], 1.0f, 1e-6f, "the aim stays straight ahead");
        Check(ProjectAimToPixels(full, 75.0f, ruf[0], ruf[1], ruf[2], &px), "it projects");
        CheckNear(px.x, 960.0f, 1e-3f, "so the crosshair stays pinned at centre");
        CheckNear(px.y, 540.0f, 1e-3f, "on both axes");
    }

    // Turning the head right leaves the gun pointing to the LEFT of the rendered view.
    ResolveAimInTrackedView(level, UE3Rotator{ 0, DegToUnits(20.0f), 0 }, ruf);
    CheckNear(ruf[0], -0.342030f, 1e-5f, "a head yaw puts the aim on the opposite side");
    Check(ProjectAimToPixels(full, 75.0f, ruf[0], ruf[1], ruf[2], &px), "it projects");
    Check(px.x < 960.0f, "so the crosshair sits left of centre");
}

void CameraLocalCompositionTests() {
    std::printf("camera-local composition\n");

    // Composing an unbounded head pitch onto a view already near vertical used to throw
    // yaw and roll half a turn: the view snapped upside-down and backwards. The world-yaw
    // branch was clamped; this one was not.
    // The test is on the composed BASIS, not on the rotator it decomposes to. Right at
    // the pole yaw and roll are degenerate - the forward axis is vertical, so there is no
    // well-defined heading - and asserting on them would be measuring float noise. Row 2
    // is the camera's up axis, and its z component going NEGATIVE is precisely what
    // "the view snapped upside-down" means.
    const UE3Rotator clean{ 16000, 4000, 0 };

    const Mat3 unbounded = MatMul(RotatorToMatrix(30.0f * kDegToRad, 0.0f, 0.0f),
                                  RotatorToMatrix(clean));
    Check(unbounded.m[2][2] < 0.0f,
          "composing an unbounded head pitch past vertical really does invert the view");

    const std::int32_t pitchUnits = BoundedPitchContribution(clean.Pitch, 30.0f);
    const Mat3 bounded = MatMul(RotatorToMatrix(static_cast<float>(pitchUnits) * kUnitsToRad,
                                                0.0f, 0.0f),
                                RotatorToMatrix(clean));
    // At the limit the view looks straight up, so its up axis is horizontal and lands on
    // either side of zero in float. The flip it has to be told apart from is -0.47.
    Check(bounded.m[2][2] > -1e-4f, "bounding the contribution keeps the view upright");
    CheckNear(bounded.m[0][2], 1.0f, 1e-4f, "and stops it looking straight up");

    // The same thing through the boundary function the hook actually calls, so a revert
    // at the CALL SITE is caught and not just a change to the helper. Reverting the
    // bound to a plain DegToUnits used to pass the whole suite.
    {
        UE3Rotator viaBoundary = clean;
        ComposeHeadRotation(clean, 30.0f, 0.0f, 0.0f, /*worldSpaceYaw=*/false, &viaBoundary);
        // Assert on the FORWARD axis, not on yaw and roll. The result sits exactly at the
        // pole, where the Euler decomposition is degenerate and those two are float noise;
        // pitch comes through asin and stays well conditioned. Bounded the view looks
        // straight up (forward.z == 1); unbounded it tips back over to 0.88 and the world
        // is upside-down behind it.
        CheckNear(RotatorToMatrix(viaBoundary).m[0][2], 1.0f, 1e-3f,
                  "the composition the hook calls stops at straight up instead of "
                  "tipping past vertical");
    }

    // The tracker and UE3 agree on the roll sign, so a POSITIVE tracker roll reaches the
    // engine positive. The first build negated it here and the view tilted the wrong way
    // in game.
    {
        UE3Rotator rolled{ 0, 0, 0 };
        ComposeHeadRotation(UE3Rotator{ 0, 0, 0 }, 0.0f, 0.0f, 20.0f, true, &rolled);
        Check(rolled.Roll == DegToUnits(20.0f),
              "a positive tracker roll reaches the engine unchanged");
        Check(rolled.Pitch == 0 && rolled.Yaw == 0, "and touches nothing else");
    }

    // World-space yaw is plain rotator addition, which is what makes it horizon-locked.
    {
        UE3Rotator yawed{ 1000, 4000, 0 };
        ComposeHeadRotation(UE3Rotator{ 1000, 4000, 0 }, 0.0f, 30.0f, 0.0f, true, &yawed);
        Check(yawed.Yaw == 4000 + DegToUnits(30.0f), "world-space yaw adds to the game's yaw");
        Check(yawed.Pitch == 1000, "leaving pitch alone");
    }

    // Well short of the limit the composition is exact, so the bound cannot be quietly
    // eating pitch during ordinary play.
    const UE3Rotator gentle{ 8192, 4000, 0 };
    const std::int32_t gentleUnits = BoundedPitchContribution(gentle.Pitch, 10.0f);
    UE3Rotator out{};
    MatrixToRotator(MatMul(RotatorToMatrix(static_cast<float>(gentleUnits) * kUnitsToRad,
                                           0.0f, 0.0f),
                           RotatorToMatrix(gentle)), &out);
    Check(std::abs(out.Pitch - 10012) <= 1, "an ordinary look composes to the exact sum");
    Check(std::abs(out.Yaw - 4000) <= 1, "leaving yaw alone");
    Check(std::abs(out.Roll) <= 1, "and adding no roll");
}

void ConfigDefaultTests() {
    std::printf("shipped config defaults\n");

    // The doctrine's numbers, pinned where the mod actually reads them. These used to be
    // duplicated between an anonymous namespace in config.cpp and the Config struct, with
    // nothing checking the two agreed.
    Check(kDefaultLocalSmoothing == 0.0f, "LocalSmoothing ships at 0");
    Check(kDefaultRemoteSmoothing == 0.15f, "RemoteSmoothing ships at 0.15");
    Check(kDefaultPosLimitX == 0.30f, "LimitX ships at 0.30");
    Check(kDefaultPosLimitY == 0.20f, "LimitY ships at 0.20");
    Check(kDefaultPosLimitZ == 0.40f, "LimitZ ships at 0.40");
    Check(kDefaultPosLimitZBack == 0.10f, "LimitZBack ships at 0.10");
    Check(kDefaultPositionScale == 100.0f, "PositionScale ships at 100");
    Check(kDefaultFov == 0.0f, "the FOV override ships off");
    Check(kDefaultPort == 4242, "the port is the OpenTrack standard");
    Check(kDefaultPosLimitZ > kDefaultPosLimitZBack,
          "the generous z budget sits on leaning in, not on pulling back");

    // The engine's own sign conventions are handled at the engine boundary, so every
    // user-facing inversion ships off. A default of true here would make a conversion
    // look like a preference, and a user who turned the three off would get a mirror.
    Check(kDefaultInvert == false, "the invert flags ship off");

    Config c{};
    Check(c.pos_limit_x == kDefaultPosLimitX, "the struct default cannot drift from LimitX");
    Check(c.pos_limit_y == kDefaultPosLimitY, "nor from LimitY");
    Check(c.pos_limit_z == kDefaultPosLimitZ, "nor from LimitZ");
    Check(c.pos_limit_z_back == kDefaultPosLimitZBack, "nor from LimitZBack");
    Check(c.local_smoothing == kDefaultLocalSmoothing, "nor from LocalSmoothing");
    Check(c.remote_smoothing == kDefaultRemoteSmoothing, "nor from RemoteSmoothing");
    Check(c.position_scale == kDefaultPositionScale, "nor from PositionScale");
    Check(c.invert_roll == false, "and roll is not inverted in the pipeline");
    Check(c.vk_toggle == 0x23 && c.vk_cycle_mode == 0x21 && c.vk_yaw_mode == 0x22,
          "the nav-cluster bindings are End / Page Up / Page Down");

    // A limit is a distance. A negative one does not widen or narrow the range, it
    // inverts the processor's clamp into a pair of constants and the camera stops
    // answering the tracker at all, so it is rejected rather than passed through.
    Check(SanitizePositiveLimit(0.5f, kDefaultPosLimitX) == 0.5f, "a wider limit is tuning");
    Check(SanitizePositiveLimit(-0.4f, kDefaultPosLimitZ) == kDefaultPosLimitZ,
          "a negative limit falls back rather than inverting the clamp");
    Check(SanitizePositiveLimit(0.0f, kDefaultPosLimitZ) == kDefaultPosLimitZ,
          "and so does a zero one");
    Check(SanitizePositiveLimit(kNan, kDefaultPosLimitY) == kDefaultPosLimitY,
          "a non-finite limit takes the default");
}

// The geometry the collision clamp stops a lean with. It runs inside a detour with a
// live world trace behind it, so these pin the arithmetic on its own.
void CollisionClampTests() {
    std::printf("collision clamp geometry\n");

    // A wall 30 units out, holding 20 off it: the eye may travel 10.
    CheckNear(AllowedLeanDistance(30.0f, 1.0f, 20.0f), 10.0f, 1e-5f,
              "a lean that meets a wall stops a margin short of it");

    // What it may travel does NOT depend on how hard the head is pushing - there is no
    // lean length in the expression at all. That is the hard stop: lean further into a
    // wall and the eye does not move, rather than creeping in by a shrinking fraction.
    CheckNear(AllowedLeanDistance(30.0f, 1.0f, 20.0f),
              AllowedLeanDistance(30.0f, 1.0f, 20.0f), 0.0f,
              "and the stop distance is a property of the world, not of the pose");

    // The margin is measured along the surface normal, so meeting the same wall at
    // 60 degrees doubles the distance the lean has to stop short by.
    CheckNear(AllowedLeanDistance(100.0f, 0.5f, 20.0f), 60.0f, 1e-5f,
              "an oblique hit stops further back along the lean than a square one");

    // A hit shallower than the floor takes the floored pull-back rather than one that
    // runs away to infinity as the lean turns parallel to the surface.
    CheckNear(AllowedLeanDistance(200.0f, 0.001f, 20.0f), 100.0f, 1e-5f,
              "a glancing hit uses the floored approach rather than an unbounded one");

    // Closer than the margin, and there is nothing to give.
    Check(AllowedLeanDistance(10.0f, 1.0f, 20.0f) == 0.0f,
          "a wall inside the margin blocks the lean outright");
    Check(AllowedLeanDistance(0.0f, 1.0f, 20.0f) == 0.0f, "and so does one on the eye");

    // The ray must outrun the lean by a whole pull-back, or a wall just past the lean's
    // end stays invisible until the lean has already gone through it - which is exactly
    // the "through the wall, then popped back out" the first version of this shipped.
    for (float cos : { 1.0f, 0.5f, 0.2f, 0.01f, 0.0f }) {
        const float pullBack =
            20.0f / (cos > kMinApproachCos ? cos : kMinApproachCos);
        Check(pullBack <= TraceOverreach(20.0f) + 1e-5f,
              "the trace reaches past the lean by at least any pull-back it can demand");
    }

    // A margin of zero would put the eye exactly on the surface, where the near clip
    // plane renders through it, so the INI boundary refuses one.
    Check(SanitizePositiveLimit(0.0f, kDefaultCollisionMargin) == kDefaultCollisionMargin,
          "a zero margin falls back to the shipped one");
}

// Drives a real INI through the real parser. The sanitizers were already covered; what
// was not was that ReadLimit actually calls the right one - swapping it back to the
// finite-only check passed the entire suite before these existed.
void ConfigParseTests() {
    std::printf("config parsing\n");

    // An ABSOLUTE path: the reader goes through GetPrivateProfileString, which resolves
    // a bare filename against the Windows directory rather than the working directory,
    // so a relative path here would silently read nothing and every assertion below
    // would "pass" by matching the default. The mod always passes an absolute path
    // (GetModulePath returns a full directory), so this only bites tests.
    char pathBuf[MAX_PATH];
    if (GetFullPathNameA("dishonored_tests_config.ini", MAX_PATH, pathBuf, nullptr) == 0) {
        Check(false, "the test INI path can be resolved");
        return;
    }
    const char* path = pathBuf;
    std::remove(path);

    {
        std::FILE* f = std::fopen(path, "wb");
        Check(f != nullptr, "the test INI can be created");
        if (!f) return;
        std::fputs("[General]\nPort=4243\n"
                   "[Position]\n"
                   "LimitX=-0.3\n"      // negative: inverts the clamp, must be refused
                   "LimitY=0\n"         // zero: same
                   "LimitZ=0.5\n"       // a widened limit is legitimate tuning
                   "LimitZBack=nan\n"   // non-finite
                   "[Collision]\n"
                   "Enabled=false\n"
                   "Margin=-5\n"       // negative: would seat the eye in the surface
                   "[Sensitivity]\n"
                   "Yaw=2.5\nPitch=-1\nRoll=inf\n",
                   f);
        std::fclose(f);
    }

    Config cfg;
    Check(cfg.LoadOrCreate(path), "an existing INI loads");
    Check(cfg.udp_port == 4243, "a port inside the bindable range is taken as written");

    Check(cfg.pos_limit_x == kDefaultPosLimitX,
          "a NEGATIVE position limit is refused and the default used, because it would "
          "invert the processor's clamp into a pair of constants");
    Check(cfg.pos_limit_y == kDefaultPosLimitY, "and so is a zero one");
    Check(cfg.pos_limit_z == 0.5f, "a widened limit is tuning and passes through");
    Check(cfg.pos_limit_z_back == kDefaultPosLimitZBack, "a non-finite limit takes the default");

    Check(cfg.collision_enabled == false, "collision can be turned off from the INI");
    Check(cfg.collision_margin == kDefaultCollisionMargin,
          "and a negative margin takes the default rather than seating the eye in the wall");

    Check(cfg.sens_yaw == 2.5f, "a sensitivity above 1 is not clamped");
    Check(cfg.sens_pitch == -1.0f, "a negative sensitivity is left alone");
    Check(cfg.sens_roll == kDefaultSensitivity, "a non-finite sensitivity takes the default");

    // A key the mod no longer has must not resurrect anything, and an unknown key must
    // not stop the file loading - that is what keeps an existing user's INI working.
    Check(cfg.world_space_yaw == kDefaultWorldSpaceYaw,
          "a key absent from the file keeps its shipped default");

    std::remove(path);

    // A port outside the bindable range is the one config error the mod refuses to
    // start on, rather than silently binding something else.
    {
        std::FILE* f = std::fopen(path, "wb");
        if (f) {
            std::fputs("[General]\nPort=80\n", f);
            std::fclose(f);
        }
    }
    Config bad;
    Check(!bad.LoadOrCreate(path), "a port below the bindable range fails the load");
    std::remove(path);

    Check(!cfg.LoadOrCreate(""), "an unresolvable path is a hard error, not a CWD fallback");
}

void FovOverrideTests() {
    std::printf("fov override\n");

    // The override is an OFFSET, which is what keeps the game's own zooms intact.
    Check(ApplyFovOffset(85.0f, 85.0f, 100.0f) == 100.0f,
          "at the default zoom the scene renders at the configured value");
    Check(ApplyFovOffset(45.0f, 85.0f, 100.0f) == 60.0f,
          "a 40 degree zoom still removes 40 degrees");
    Check(ApplyFovOffset(85.0f, 85.0f, 0.0f) == 85.0f, "no override configured, no change");
    Check(ApplyFovOffset(45.0f, 85.0f, 0.0f) == 45.0f, "including under zoom");

    // Whatever it produces has to stay inside the range the crosshair projection
    // accepts, or the crosshair silently stops being placed.
    Check(ApplyFovOffset(160.0f, 85.0f, 100.0f) == kMaxFovDegrees, "a wide result clamps");
    Check(ApplyFovOffset(20.0f, 120.0f, 30.0f) == kMinFovDegrees, "a narrow result clamps");
    Check(IsUsableFov(ApplyFovOffset(160.0f, 85.0f, 100.0f)),
          "so the projection can always still use it");

    // An unusable pair leaves the game's own field of view alone rather than guessing.
    Check(ApplyFovOffset(75.0f, 0.0f, 100.0f) == 75.0f,
          "a DefaultFOV that has not been initialised is not an anchor");
    Check(ApplyFovOffset(75.0f, 999.0f, 100.0f) == 75.0f, "nor is an implausible one");
    Check(ApplyFovOffset(75.0f, 85.0f, -1.0f) == 75.0f, "a negative override is not one");
}

// Where an offset @p angleDeg away from where the camera points lands on screen, as a
// fraction of the way to the edge of the frame, at a field of view of @p fovDeg. This is
// the projection the zoom compensation exists to hold constant.
float ScreenFraction(float angleDeg, float fovDeg) {
    const float degToRad = static_cast<float>(cameraunlock::math::kPi / 180.0);
    return std::tan(angleDeg * degToRad) / std::tan(fovDeg * 0.5f * degToRad);
}

void ZoomCompensationTests() {
    std::printf("zoom compensation\n");

    Check(ZoomCompensation(75.0f, 75.0f) == 1.0f, "unzoomed, the head is applied in full");
    Check(ZoomCompensation(48.6f, 75.0f) < 1.0f, "a zoom scales the head down");
    Check(ZoomCompensation(30.0f, 75.0f) < ZoomCompensation(48.6f, 75.0f),
          "and further the tighter the zoom is");

    // The one thing the mod will never do.
    Check(ZoomCompensation(90.0f, 75.0f) == 1.0f,
          "a field of view wider than the unzoomed one does not amplify the head");
    Check(ZoomCompensation(kMaxFovDegrees, 30.0f) == 1.0f, "however much wider it is");

    // What the scale is for: a head turn is worth the same distance on screen whatever
    // the game has done to the field of view. The tangent ratio is a single number
    // applied to an angle, so the parity it holds is exact in the limit and drifts as
    // the turn grows - within a percent out to the 10 degrees a head holds comfortably.
    for (float fov : { 60.0f, 48.6f, 35.0f, 25.0f }) {
        for (float head : { 2.0f, 5.0f, 10.0f }) {
            const float scaled = ScreenFraction(head * ZoomCompensation(fov, 75.0f), fov);
            CheckNear(scaled / ScreenFraction(head, 75.0f), 1.0f, 0.01f,
                      "a head turn covers the same screen distance under zoom");
        }
    }

    // Past that the drift only ever falls short of parity, never over it, so the far end
    // of a big turn under a deep zoom is the one thing it cannot become: amplified.
    for (float fov : { 60.0f, 48.6f, 35.0f, 25.0f }) {
        for (float head : { 25.0f, 35.0f, 45.0f }) {
            const float ratio =
                ScreenFraction(head * ZoomCompensation(fov, 75.0f), fov) /
                ScreenFraction(head, 75.0f);
            Check(ratio <= 1.0f && ratio > 0.75f,
                  "a large head turn under zoom falls short of parity rather than over it");
        }
    }

    // Neither field of view can be read, so no zoom can be measured from them.
    Check(ZoomCompensation(kNan, 75.0f) == 1.0f, "a NaN field of view measures no zoom");
    Check(ZoomCompensation(48.6f, kNan) == 1.0f, "nor does a NaN baseline");
    Check(ZoomCompensation(48.6f, 0.0f) == 1.0f,
          "nor a DefaultFOV that has not been initialised");
    Check(ZoomCompensation(0.0f, 75.0f) == 1.0f, "nor a field of view outside the usable range");
}

}  // namespace

int main() {
    std::printf("DishonoredHeadTracking tests\n");
    std::printf("============================\n");

    SanitizeTests();
    FovRangeTests();
    RotatorUnitTests();
    FiniteSampleTests();
    HeapPointerTests();
    RotationMatrixTests();
    CameraLocalCompositionTests();
    ViewRectTests();
    AimProjectionTests();
    AimDirectionTests();
    CollisionClampTests();
    ConfigDefaultTests();
    ConfigParseTests();
    FovOverrideTests();
    ZoomCompensationTests();

    if (g_failures == 0) {
        std::printf("All tests passed\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", g_failures);
    return 1;
}
