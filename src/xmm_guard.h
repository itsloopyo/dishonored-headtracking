// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

// Frame setup and XMM preservation for the naked detour thunks.
//
// This binary is LTCG-built and its callers keep non-ABI register state live across
// calls: hooking a leaf here once destroyed the XMM6/XMM7 the caller was using to
// compute the screen fade, and the screen stuck part-faded. Save and restore the whole
// XMM file around any detour body rather than re-learning that per hook site. movups
// needs no alignment, so no stack realignment is required.
//
// DHT_DETOUR_ENTER establishes the classic ebp frame and reserves 160 bytes of locals -
// 128 for the XMM file, the rest free for a thunk to stash a register in - then saves
// xmm0-xmm7. DHT_DETOUR_RESTORE_XMM reloads them. Each thunk supplies its own argument
// marshalling between the two, and its own teardown and return afterwards, because
// those are what differ between the functions being detoured.

#define DHT_DETOUR_ENTER            \
    __asm push ebp                  \
    __asm mov  ebp, esp             \
    __asm sub  esp, 160             \
    __asm movups [ebp-16],  xmm0    \
    __asm movups [ebp-32],  xmm1    \
    __asm movups [ebp-48],  xmm2    \
    __asm movups [ebp-64],  xmm3    \
    __asm movups [ebp-80],  xmm4    \
    __asm movups [ebp-96],  xmm5    \
    __asm movups [ebp-112], xmm6    \
    __asm movups [ebp-128], xmm7

#define DHT_DETOUR_RESTORE_XMM      \
    __asm movups xmm0, [ebp-16]     \
    __asm movups xmm1, [ebp-32]     \
    __asm movups xmm2, [ebp-48]     \
    __asm movups xmm3, [ebp-64]     \
    __asm movups xmm4, [ebp-80]     \
    __asm movups xmm5, [ebp-96]     \
    __asm movups xmm6, [ebp-112]    \
    __asm movups xmm7, [ebp-128]
