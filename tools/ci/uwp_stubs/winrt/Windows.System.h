// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace winrt::Windows::System {
struct MemoryManager { static std::uint64_t AppMemoryUsageLimit(); };
}
