// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/// A logging back end for the dependency-free self-tests.
///
/// `Core::Xbox` and `Uwp::Chords` log through `Common::Log`, which is spdlog +
/// fmt. Linking the real `log.cpp` into a self-test would drag in file sinks,
/// the emulator settings singleton and path handling -- most of the emulator --
/// just to print a line.
///
/// The two symbols `log.h` actually requires are `g_class_levels` and `VLog`.
/// Defining them here keeps a test binary to: the unit under test, its
/// dependencies that are pure computation, fmt, and this file.
///
/// Levels are held at `Off` for the default `g_class_levels`, so a noisy code
/// path cannot flood CI output; a test can raise a class to `Info` by writing to
/// the array directly, and `CapturedMessages()` returns what would have been
/// logged.

#include <cstdio>
#include <string>
#include <vector>

// fmt/base.h is enough to *declare* the logging surface, but vformat lives with
// the formatted-argument machinery in format.h. The real log.cpp includes
// <fmt/std.h> and spdlog on top of that; a test does not need either.
#include <fmt/format.h>

#include "common/logging/log.h"

namespace Common::Log {

// The real definition lives in log.cpp, which the self-tests do not link.
std::array<Level, NUM_LOG_CLASSES> g_class_levels = [] {
    std::array<Level, NUM_LOG_CLASSES> levels{};
    levels.fill(Level::Off);
    return levels;
}();

namespace {

/// Messages this process would have logged, for a test that wants to assert on
/// one. Deliberately a plain vector behind a function-local static: the log path
/// has no locking requirement here, and a self-test is single-threaded.
std::vector<std::string>& CapturedMessagesStorage() {
    static std::vector<std::string> messages;
    return messages;
}

const char* LevelName(Level level) {
    switch (level) {
    case Level::Trace:
        return "trace";
    case Level::Debug:
        return "debug";
    case Level::Info:
        return "info";
    case Level::Warning:
        return "warning";
    case Level::Error:
        return "error";
    case Level::Critical:
        return "critical";
    case Level::Off:
        break;
    }
    return "off";
}

} // Anonymous namespace

void VLog(Class log_class, Level level, const char* file, int line, const char* func,
          fmt::string_view format, fmt::format_args args) {
    // Class is deliberately not rendered: the tests assert on the message, and
    // the class name table in classes.cpp is not linked either.
    (void)log_class;
    (void)func;
    std::string text = fmt::vformat(format, args);
    CapturedMessagesStorage().push_back(text);
    std::fprintf(stderr, "[%s] %s:%d: %s\n", LevelName(level), file, line, text.c_str());
}

// Declared in log.h under the self-test macros.
const std::vector<std::string>& CapturedMessages() {
    return CapturedMessagesStorage();
}

void ClearCapturedMessages() {
    CapturedMessagesStorage().clear();
}

} // namespace Common::Log
