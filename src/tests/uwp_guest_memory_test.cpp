// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/uwp_guest_memory.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
int passed = 0, failed = 0;
void Check(bool condition, const char* name) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", name); ++failed; }
    else ++passed;
}
using Core::Xbox::GuestMemoryModel;
constexpr auto P = GuestMemoryModel::PageSize;

void RangeSearchAndCoalescing() {
    GuestMemoryModel m(0x10000, 0x20000, 8 * P); std::string e;
    std::uint64_t a = 0, b = 0, c = 0;
    bool ok = m.Reserve(a, 2 * P, e) && m.Reserve(b, 3 * P, e) && m.Release(a, 2 * P, e) && m.Release(b, 3 * P, e);
    ok = ok && m.Reserve(c, 5 * P, e) && c == 0x10000;
    Check(ok, "first-fit search and adjacent placeholder coalescing");
}
void ProtectRoundTrip() {
    GuestMemoryModel m(0x10000, 0x18000, 4 * P); std::string e; std::uint64_t a = 0;
    std::array<unsigned char, 4> input{1, 3, 5, 7}, output{};
    bool ok = m.Reserve(a, P, e) && m.Commit(a, P, GuestMemoryModel::ReadAccess | GuestMemoryModel::WriteAccess, e) &&
        m.Write(a, input.data(), input.size(), e) && m.Protect(a, P, GuestMemoryModel::ReadAccess, e) &&
        m.Read(a, output.data(), output.size(), e) && output == input;
    const bool write_blocked = !m.Write(a, input.data(), 1, e);
    ok = ok && write_blocked && m.Protect(a, P, GuestMemoryModel::ReadAccess | GuestMemoryModel::ExecuteAccess, e);
    Check(ok, "writable-to-readonly-to-executable protection round trip");
}
void EvictionPreservesProtection() {
    GuestMemoryModel m(0x10000, 0x18000, 4 * P); std::string e; std::uint64_t a = 0;
    std::array<unsigned char, 4> input{9, 8, 7, 6}, output{};
    bool ok = m.Reserve(a, P, e) && m.Commit(a, P, GuestMemoryModel::ReadAccess | GuestMemoryModel::WriteAccess, e) &&
        m.Write(a, input.data(), input.size(), e) && m.Protect(a, P, GuestMemoryModel::ReadAccess, e) &&
        m.EvictCold(P) == P;
    const auto before = m.GetStats();
    ok = ok && before.pages_out == 1 && before.evicted_bytes == P &&
        m.Read(a, output.data(), output.size(), e) && output == input;
    const auto after = m.GetStats();
    ok = ok && after.pages_in == 1 && after.committed_bytes == P &&
        !m.Write(a, input.data(), 1, e);
    Check(ok, "cold-page swap integrity, page-in, and preserved readonly permission");
}
void ExecutablePagesAreNeverEvicted() {
    GuestMemoryModel m(0x10000, 0x18000, 4 * P); std::string e; std::uint64_t a = 0;
    bool ok = m.Reserve(a, P, e) && m.Commit(a, P, GuestMemoryModel::ReadAccess | GuestMemoryModel::ExecuteAccess, e) &&
        m.EvictCold(P) == 0 && m.GetStats().pages_out == 0;
    Check(ok, "executable pages remain resident under pressure");
}
void OutOfRangeAddressNamesTheAddress() {
    GuestMemoryModel m(0x10000, 0x20000, 4 * P); std::string e;
    std::uint64_t a = Core::Xbox::GuestAddressLimit;
    const bool rejected = !m.Reserve(a, P, e) && e.find("0x10000000000") != std::string::npos;
    Check(rejected, "40-bit range refusal includes the offending guest address");
}
void CeilingStopsCommitWithPlainLanguage() {
    GuestMemoryModel m(0x10000, 0x18000, P); std::string e; std::uint64_t a = 0;
    bool ok = m.Reserve(a, 2 * P, e) && !m.Commit(a, 2 * P, GuestMemoryModel::ReadAccess, e);
    const auto stats = m.GetStats();
    ok = ok && e.find("ceiling reached") != std::string::npos && stats.committed_bytes == 0 &&
         m.TelemetryLine().find("ceiling=") != std::string::npos;
    Check(ok, "memory ceiling produces a readable hard stop and telemetry");
}
}
int main() {
    RangeSearchAndCoalescing(); ProtectRoundTrip(); EvictionPreservesProtection();
    ExecutablePagesAreNeverEvicted(); OutOfRangeAddressNamesTheAddress();
    CeilingStopsCommitWithPlainLanguage();
    std::printf("UWP guest memory host tests: %d passed, %d failed\n", passed, failed);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
