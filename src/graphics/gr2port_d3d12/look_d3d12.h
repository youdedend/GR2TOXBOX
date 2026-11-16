// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "graphics/gr2port_d3d12/world_d3d12.h"
namespace Gr2Port::D3D12 {
class LookRenderer {
    CoreTargets* t_ = nullptr;
    Pipeline sky_, luminance_, exposure_, tonemap_, antialias_, composite_;

  public:
    bool Start(CoreTargets& targets);
    void RecordSky(CommandList& list, const Scene::Header& h, u64 address);
    void RecordPost(CommandList& list, u64 address);
    void RecordComposite(CommandList& list, u64 address, u32 width, u32 height);
    void Stop();
};
} // namespace Gr2Port::D3D12
