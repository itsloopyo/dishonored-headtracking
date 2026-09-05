// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

namespace DishonoredHeadTracking {

std::string GetModuleDirectory();
std::string GetModulePath(const char* filename);

// Wide variant for APIs that take wide paths (core logging::Open). Reads the module
// path with the WIDE API rather than converting the ANSI one: GetModuleFileNameA
// renders anything the active ANSI codepage cannot represent as '?', which turns a
// non-ASCII install path into a directory that does not exist.
//
// Both variants return an empty string when the path does not fit MAX_PATH, rather
// than the truncated path GetModuleFileName leaves in the buffer.
std::wstring GetModulePathW(const char* filename);

}
