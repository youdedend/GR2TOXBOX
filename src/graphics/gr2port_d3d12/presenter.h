// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Gr2Port::Renderer {
// UWP calls Initialize/Tick only on the CoreWindow thread. Desktop Start owns
// a render worker. Shutdown joins it before releasing queue-owned resources.
bool Initialize(void* core_window);
bool Tick();
void RequestResize();
void Shutdown();
}
