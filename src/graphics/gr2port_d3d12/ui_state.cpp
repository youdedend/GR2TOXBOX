// SPDX-License-Identifier: GPL-2.0-or-later
#include "graphics/gr2port_d3d12/ui_state.h"
namespace Gr2Port::D3D12::UiState {
D3D12_BLEND Factor(u32 value, bool alpha) {
    switch (value) {
    case 1:
        return D3D12_BLEND_ONE;
    case 2:
        return alpha ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_SRC_COLOR;
    case 3:
        return alpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
    case 4:
        return D3D12_BLEND_SRC_ALPHA;
    case 5:
        return D3D12_BLEND_INV_SRC_ALPHA;
    case 6:
        return D3D12_BLEND_DEST_ALPHA;
    case 7:
        return D3D12_BLEND_INV_DEST_ALPHA;
    case 8:
        return alpha ? D3D12_BLEND_DEST_ALPHA : D3D12_BLEND_DEST_COLOR;
    case 9:
        return alpha ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_INV_DEST_COLOR;
    case 10:
        return alpha ? D3D12_BLEND_ONE : D3D12_BLEND_SRC_ALPHA_SAT;
    // The Vulkan UI pass never supplies nonzero blend constants. Both constant
    // colour and replicated constant alpha are therefore the same zero vector.
    case 13:
    case 19:
        return D3D12_BLEND_BLEND_FACTOR;
    case 14:
    case 20:
        return D3D12_BLEND_INV_BLEND_FACTOR;
    default:
        return D3D12_BLEND_ZERO;
    }
}
D3D12_BLEND_OP Operation(u32 value) {
    switch (value) {
    case 1:
        return D3D12_BLEND_OP_SUBTRACT;
    case 2:
        return D3D12_BLEND_OP_MIN;
    case 3:
        return D3D12_BLEND_OP_MAX;
    case 4:
        return D3D12_BLEND_OP_REV_SUBTRACT;
    default:
        return D3D12_BLEND_OP_ADD;
    }
}
D3D12_RENDER_TARGET_BLEND_DESC Blend(u32 control) {
    const u32 alpha = (control & (1u << 29)) ? control >> 16 : control;
    D3D12_RENDER_TARGET_BLEND_DESC d{};
    d.BlendEnable = (control >> 30) & 1;
    d.LogicOpEnable = FALSE;
    d.SrcBlend = Factor(control & 31);
    d.DestBlend = Factor((control >> 8) & 31);
    d.BlendOp = Operation((control >> 5) & 7);
    d.SrcBlendAlpha = Factor(alpha & 31, true);
    d.DestBlendAlpha = Factor((alpha >> 8) & 31, true);
    d.BlendOpAlpha = Operation((alpha >> 5) & 7);
    d.LogicOp = D3D12_LOGIC_OP_NOOP;
    d.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return d;
}
} // namespace Gr2Port::D3D12::UiState
