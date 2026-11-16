// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
namespace Gr2Port::GameCheck { struct Game; }
namespace Gr2Port::Uwp {
void InitializeConsoleSettings(const std::filesystem::path& local_state);
bool BootConsoleGame(const GameCheck::Game& validated_game);
}
