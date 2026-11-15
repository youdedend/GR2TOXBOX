// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
struct ID3D12Device8;
namespace Gr2Port::D3D12 {
struct CommandList;
struct Pipeline;
struct RootSignature;
class BindlessHeap;
// One padded mip extent. BC formats use blocks as elements. All byte ranges
// must be backed by separate source/destination buffers (no in-place detiling).
struct DetileConstants {
    std::uint32_t pitch, height, depth, texel_count;
    std::uint32_t bytes, micro_mode, thickness, samples;
    std::uint32_t array_mode, pipe_config, bank_width, bank_height;
    std::uint32_t num_banks, tile_split, macro_aspect, bank_swizzle;
};
struct DetileTextures {
    std::uint32_t input, output, input_offset, output_offset;
};
static_assert(sizeof(DetileConstants) == 64);
static_assert(sizeof(DetileTextures) == 16);
bool CreateDetilePipeline(ID3D12Device8* device, const RootSignature& root, Pipeline& output);
// Host validates before dispatch; supports display/thin micro modes and the
// P2/P8 GCN macro layouts. MSAA/thick/PRT/rotated variants are rejected explicitly.
bool ValidateDetile(const DetileConstants& params, const DetileTextures& textures);
// Command list already owns transitions: source NON_PIXEL_SHADER_RESOURCE,
// destination UNORDERED_ACCESS. This call emits the UAV completion barrier.
bool DispatchDetile(CommandList& list, const Pipeline& pipeline,
                    const RootSignature& root, const BindlessHeap& heap,
                    const DetileConstants& params, const DetileTextures& textures,
                    std::uint64_t source_bytes, std::uint64_t destination_bytes);
}
