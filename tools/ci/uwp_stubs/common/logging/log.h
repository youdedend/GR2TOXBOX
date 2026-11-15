// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// No dependency on fmt/spdlog submodules for the ABI-only check. Evaluate every
// argument, so misspelled member accesses inside logging calls still fail.
namespace Gr2Stub { template<class... T> void Log(T&&...); }
#define LOG_INFO(category, ...) Gr2Stub::Log(__VA_ARGS__)
#define LOG_WARNING(category, ...) Gr2Stub::Log(__VA_ARGS__)
#define LOG_CRITICAL(category, ...) Gr2Stub::Log(__VA_ARGS__)
