// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "graphics/gr2port_d3d12/arena.h"
#include "graphics/gr2port_d3d12/command.h"
#include "graphics/gr2port_d3d12/core_state.h"
#include "graphics/gr2port_d3d12/pipeline.h"
namespace Gr2Port::D3D12 {
struct CoreTargets;
class PointLights {
    CoreTargets* t_ = nullptr;
    ArenaAllocation grid_;
    Pipeline pipeline_;

public:
    bool Start(CoreTargets&);
    void Record(CommandList&, const Scene::Header&, u64);
    CoreState::Ref Grid() const {
        return {grid_.ref.arena, grid_.ref.offset};
    }
    void Stop();
};
} // namespace Gr2Port::D3D12
