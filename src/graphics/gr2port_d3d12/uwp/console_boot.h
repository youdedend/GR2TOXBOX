// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
namespace Gr2Port::Uwp {
void InitializeConsoleSettings(const std::filesystem::path& local_state);
void BootConsoleGame(const std::filesystem::path& root);
}
