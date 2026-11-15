// SPDX-License-Identifier: GPL-2.0-or-later
#include "graphics/gr2port_d3d12/detile.h"
#include "graphics/gr2port_d3d12/command.h"
#include "graphics/gr2port_d3d12/pipeline.h"
#include <algorithm>
#include "detile_comp.h"
#include <limits>
#include "graphics/gr2port_d3d12/shader_batch_layout.h"

namespace Gr2Port::D3D12 {
bool CreateDetilePipeline(ID3D12Device8* device, const RootSignature& root, Pipeline& output) {
    return CreateComputePipeline(device, root, Shaders::detile_comp_span, output);
}

bool DispatchDetile(CommandList& list, const Pipeline& pipeline,
                    const RootSignature& root, const BindlessHeap& heap,
                    const DetileConstants& p, const DetileTextures& t,
                    std::uint64_t source_bytes, std::uint64_t destination_bytes) {
    if (!ValidateDetile(p, t) || !list.IsValid() || !pipeline.IsValid() || !root.IsValid()) return false;
    const std::uint64_t bytes = std::uint64_t(p.texel_count) * p.bytes;
    if (std::uint64_t(t.input_offset) + bytes > source_bytes ||
        std::uint64_t(t.output_offset) + bytes > destination_bytes ||
        std::uint64_t(t.input_offset) + bytes > std::numeric_limits<std::uint32_t>::max() ||
        std::uint64_t(t.output_offset) + bytes > std::numeric_limits<std::uint32_t>::max()) return false;
    list.SetRootSignature(root);
    ID3D12DescriptorHeap* heaps[] = {heap.SrvHeap(), heap.SamplerHeap()};
    list.list->SetDescriptorHeaps(2, heaps);
    list.SetDescriptorTables(heap);
    list.SetPipeline(pipeline);
    list.SetDrawConstants(&p, sizeof(p));
    list.SetPassConstants(&t, sizeof(t));
    const auto groups = static_cast<std::uint32_t>((bytes / 4 + 63) / 64);
    list.Dispatch((std::min)(groups, 65535u), (groups + 65534) / 65535, 1);
    D3D12_RESOURCE_BARRIER completion{};
    completion.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    completion.UAV.pResource = nullptr;
    list.list->ResourceBarrier(1, &completion);
    return true;
}
}
