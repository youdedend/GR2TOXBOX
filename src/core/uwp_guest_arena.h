// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace Core::Xbox {
inline constexpr std::uintptr_t EarlyGuestBase = 0x100000000ull;
inline constexpr std::uint64_t EarlyGuestBytes = 5ull << 30;
// Reserve a replaceable, uncommitted placeholder before WinRT/SDL/D3D12 startup.
// AddressSpace takes ownership; never release/re-reserve (that opens a VA race).
bool ReserveEarlyGuestArena() noexcept;
void* TakeEarlyGuestArena() noexcept;
}
