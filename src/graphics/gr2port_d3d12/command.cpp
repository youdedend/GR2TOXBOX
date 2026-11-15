// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "graphics/gr2port_d3d12/command.h"

#include <chrono>

#include <d3d12.h>

#include "common/logging/log.h"
#include "graphics/gr2port_d3d12/arena.h"

namespace Gr2Port::D3D12 {

namespace {

/// A resource barrier, named so the three-element array below reads as one thing.
D3D12_RESOURCE_BARRIER Barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                               D3D12_RESOURCE_STATES after, u32 subresource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = subresource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

} // namespace

// ---------------------------------------------------------------------------
// Resource state
// ---------------------------------------------------------------------------

void CommandList::Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                             D3D12_RESOURCE_STATES after, u32 subresource) const {
    if (!list || !resource || before == after) {
        return;
    }
    const D3D12_RESOURCE_BARRIER barrier = Barrier(resource, before, after, subresource);
    list->ResourceBarrier(1, &barrier);
}

void CommandList::ToRenderTarget(ID3D12Resource* resource, D3D12_RESOURCE_STATES before) const {
    Transition(resource, before, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void CommandList::ToShaderRead(ID3D12Resource* resource, D3D12_RESOURCE_STATES before) const {
    Transition(resource, before, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void CommandList::ToDepthWrite(ID3D12Resource* resource, D3D12_RESOURCE_STATES before) const {
    Transition(resource, before, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void CommandList::ToDepthRead(ID3D12Resource* resource, D3D12_RESOURCE_STATES before) const {
    // The SDK defines operator| for this enum, but naming the cast keeps the
    // expression valid on a toolchain that does not.
    const auto read = static_cast<D3D12_RESOURCE_STATES>(
        D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(resource, before, read);
}

// ---------------------------------------------------------------------------
// Root signature and pipeline
// ---------------------------------------------------------------------------

void CommandList::SetRootSignature(const RootSignature& root) const {
    if (list && root.IsValid()) {
        list->SetGraphicsRootSignature(root.signature.Get());
        list->SetComputeRootSignature(root.signature.Get());
    }
}

void CommandList::SetPipeline(const Pipeline& pipeline) const {
    if (list && pipeline.IsValid()) {
        list->SetPipelineState(pipeline.state.Get());
    }
}

void CommandList::SetArenaTable(const ArenaSet& arenas, const BindlessHeap& descriptors) const {
    if (!list) {
        return;
    }
    (void)descriptors;
    // The arena table is the SRV index of each arena, packed as five uints at b0
    // (ArenaSet::BuildRootTable). Its layout is the shaders' ArenaTable struct.
    const ArenaSet::RootArenaTable table = arenas.BuildRootTable();
    list->SetGraphicsRoot32BitConstants(0, RootConstants::ArenaBytes / 4, table.srv_index, 0);
    list->SetComputeRoot32BitConstants(0, RootConstants::ArenaBytes / 4, table.srv_index, 0);
}

void CommandList::SetDescriptorTables(const BindlessHeap& descriptors) const {
    if (!list) {
        return;
    }
    // All view tables address the same heap range; only the register space
    // differs, and that is baked into the root signature. So the GPU handles are
    // the heap bases, and binding each table is the same two values repeated.
    const D3D12_GPU_DESCRIPTOR_HANDLE srv_base = descriptors.SrvTableGpu();
    const D3D12_GPU_DESCRIPTOR_HANDLE sampler_base = descriptors.SamplerTableGpu();

    for (u32 i = 0; i < DescriptorTables::kCount; ++i) {
        const u32 parameter = DescriptorTables::kFirstTableParameter + i;
        const bool is_sampler = DescriptorTables::kTables[i].range_type ==
                                D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        const D3D12_GPU_DESCRIPTOR_HANDLE base = is_sampler ? sampler_base : srv_base;
        list->SetGraphicsRootDescriptorTable(parameter, base);
        list->SetComputeRootDescriptorTable(parameter, base);
    }
}

void CommandList::SetDrawConstants(const void* data, u32 bytes) const {
    if (!list || !data) {
        return;
    }
    const u32 count = bytes / 4 < RootConstants::DrawBytes / 4 ? bytes / 4
                                                               : RootConstants::DrawBytes / 4;
    list->SetGraphicsRoot32BitConstants(1, count, data, 0);
    list->SetComputeRoot32BitConstants(1, count, data, 0);
}

void CommandList::SetPassConstants(const void* data, u32 bytes) const {
    if (!list || !data) {
        return;
    }
    const u32 count = bytes / 4 < RootConstants::PassBytes / 4 ? bytes / 4
                                                               : RootConstants::PassBytes / 4;
    list->SetGraphicsRoot32BitConstants(2, count, data, 0);
    list->SetComputeRoot32BitConstants(2, count, data, 0);
}

// ---------------------------------------------------------------------------
// Render passes
// ---------------------------------------------------------------------------

void CommandList::BeginRenderPass(const RenderTarget* targets, u32 target_count,
                                  const DepthTarget* depth, const RootSignature& root,
                                  const ArenaSet& arenas, const BindlessHeap& descriptors) {
    if (!list) {
        return;
    }
    SetRootSignature(root);
    SetArenaTable(arenas, descriptors);

    // The heaps the shaders reach through the descriptor tables. Set once per
    // list; D3D12 keeps them across pipeline changes and the backend never
    // changes heaps.
    ID3D12DescriptorHeap* heaps[] = {descriptors.SrvHeap(), descriptors.SamplerHeap()};
    list->SetDescriptorHeaps(2, heaps);
    // The tables themselves. SetRootSignature above installed the signature that
    // declares them, and every one of them addresses this heap from its base.
    SetDescriptorTables(descriptors);

    for (u32 i = 0; i < target_count; ++i) {
        if (targets[i].resource) {
            ToRenderTarget(targets[i].resource, targets[i].before);
        }
    }
    if (depth && depth->resource) {
        if (depth->read_only) {
            ToDepthRead(depth->resource, depth->before);
        } else {
            ToDepthWrite(depth->resource, depth->before);
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handles[8]{};
    for (u32 i = 0; i < target_count && i < 8; ++i) {
        handles[i] = targets[i].view;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE depth_handle{};
    if (depth && depth->resource) {
        depth_handle = depth->view;
    }
    list->OMSetRenderTargets(target_count, handles, FALSE, (depth && depth->resource) ? &depth_handle
                                                                                   : nullptr);

    SetViewport(viewport_width_, viewport_height_);

    // Clears happen after the targets are bound. A pass that says clear = false
    // loads what was there, which the game's layered and accumulation passes rely
    // on.
    for (u32 i = 0; i < target_count && i < 8; ++i) {
        if (targets[i].resource && targets[i].do_clear) {
            list->ClearRenderTargetView(handles[i], targets[i].clear_colour, 0, nullptr);
        }
    }
    if (depth && depth->resource) {
        list->OMSetStencilRef(depth->stencil_reference);
        if (!depth->read_only) {
            // A bitwise OR of two enumerators is an int, so the cast is required
            // rather than cosmetic.
            const D3D12_CLEAR_FLAGS flags =
                depth->stencil_reference != 0
                    ? static_cast<D3D12_CLEAR_FLAGS>(D3D12_CLEAR_FLAG_DEPTH |
                                                     D3D12_CLEAR_FLAG_STENCIL)
                    : D3D12_CLEAR_FLAG_DEPTH;
            list->ClearDepthStencilView(depth_handle, flags, depth->clear,
                                        depth->stencil_reference, 0, nullptr);
        }
    }
}

void CommandList::BeginRenderPass(const RenderTarget& target, const DepthTarget* depth,
                                  const RootSignature& root, const ArenaSet& arenas,
                                  const BindlessHeap& descriptors) {
    BeginRenderPass(&target, 1, depth, root, arenas, descriptors);
}

void CommandList::EndRenderPass(const RenderTarget* targets, u32 target_count,
                                const DepthTarget* depth, D3D12_RESOURCE_STATES target_after,
                                D3D12_RESOURCE_STATES depth_after) {
    if (!list) {
        return;
    }
    // Unbind first: a transition of a resource that is still bound as a render
    // target is a validation error, and on a console it is undefined behaviour.
    list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);

    for (u32 i = 0; i < target_count; ++i) {
        if (targets[i].resource) {
            Transition(targets[i].resource, D3D12_RESOURCE_STATE_RENDER_TARGET, target_after);
        }
    }
    if (depth && depth->resource && !depth->read_only) {
        Transition(depth->resource, D3D12_RESOURCE_STATE_DEPTH_WRITE, depth_after);
    }
}

// ---------------------------------------------------------------------------
// Draws
// ---------------------------------------------------------------------------

void CommandList::Draw(u32 vertex_count, u32 instance_count, u32 base_instance) const {
    if (list && vertex_count > 0) {
        list->DrawInstanced(vertex_count, instance_count, 0, base_instance);
    }
}

void CommandList::DrawIndexed(u32 index_count, u32 instance_count, u32 base_instance) const {
    if (list && index_count > 0) {
        list->DrawIndexedInstanced(index_count, instance_count, 0, 0, base_instance);
    }
}

void CommandList::DrawFullscreen() const {
    // The full-screen triangle from fullscreen.vert.hlsl: three vertices, no
    // buffer, no index buffer.
    Draw(3, 1, 0);
}

void CommandList::SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& view) const {
    if (list) {
        list->IASetIndexBuffer(&view);
    }
}

void CommandList::SetScissor(const D3D12_RECT& rect) const {
    if (list) {
        list->RSSetScissorRects(1, &rect);
    }
}

void CommandList::SetFullViewport() const {
    SetViewport(viewport_width_, viewport_height_);
}

void CommandList::SetViewport(u32 width, u32 height) const {
    if (!list) {
        return;
    }
    // Reverse depth needs MinDepth 0 / MaxDepth 1 and no flip: D3D's clip space
    // is already the half-inverted one the port's projection produces.
    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    list->RSSetViewports(1, &viewport);
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetScissorRects(1, &scissor);
}

// ---------------------------------------------------------------------------
// Compute
// ---------------------------------------------------------------------------

void CommandList::Dispatch(u32 groups_x, u32 groups_y, u32 groups_z) const {
    if (list && groups_x > 0 && groups_y > 0 && groups_z > 0) {
        list->Dispatch(groups_x, groups_y, groups_z);
    }
}

void CommandList::DispatchForExtent(u32 width, u32 height, u32 group_size_x,
                                    u32 group_size_y) const {
    const u32 groups_x = (width + group_size_x - 1) / group_size_x;
    const u32 groups_y = (height + group_size_y - 1) / group_size_y;
    Dispatch(groups_x, groups_y, 1);
}

// ---------------------------------------------------------------------------
// Transfers
// ---------------------------------------------------------------------------

void CommandList::CopyBuffer(const D3D12_TEXTURE_COPY_LOCATION& dst,
                             const D3D12_TEXTURE_COPY_LOCATION& src) const {
    if (list) {
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
}

void CommandList::Close() const {
    if (list) {
        list->Close();
    }
}

// ---------------------------------------------------------------------------
// The frame ring
// ---------------------------------------------------------------------------

bool CommandRing::Create(Device& device) {
    device_ = &device;
    if (!device.device) {
        return false;
    }
    for (auto& frame : frames_) {
        if (FAILED(device.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                         IID_PPV_ARGS(frame.allocator.GetAddressOf())))) {
            LOG_CRITICAL(Graphics, "D3D12 command: CreateCommandAllocator failed.");
            return false;
        }
        // ID3D12GraphicsCommandList7: the backend needs Enhanced Barriers and
        // DispatchMesh on 12_2 hardware; both are 7's interface.
        if (FAILED(device.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    frame.allocator.Get(), nullptr,
                                                    IID_PPV_ARGS(frame.list.GetAddressOf())))) {
            LOG_CRITICAL(Graphics, "D3D12 command: CreateCommandList failed.");
            return false;
        }
        // A freshly created list is open. Close it so BeginFrame's reset works.
        frame.list->Close();
        frame.fence_value = 0;
    }
    LOG_INFO(Graphics, "D3D12 command: {} frames in flight.", FramesInFlight);
    return true;
}

void CommandRing::Destroy() {
    for (auto& frame : frames_) {
        frame.list.Reset();
        frame.allocator.Reset();
    }
    device_ = nullptr;
}

CommandList CommandRing::BeginFrame() {
    if (!device_) {
        return {};
    }
    Frame& frame = frames_[current_];

    // Wait for the GPU to finish the frame this slot last recorded. This is the
    // only CPU block on the GPU in the whole backend; at FramesInFlight = 3 it
    // waits on the work of three frames ago, so a frame that is slightly heavy
    // does not stall the one being recorded now.
    if (frame.fence_value != 0 && device_->timeline_fence) {
        if (device_->timeline_fence->GetCompletedValue() < frame.fence_value) {
            const auto start = std::chrono::steady_clock::now();
            WaitForFence(*device_, frame.fence_value);
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            ++waits_.count;
            waits_.total_microseconds += static_cast<u64>(elapsed);
            waits_.longest_microseconds =
                waits_.longest_microseconds > static_cast<u64>(elapsed)
                    ? waits_.longest_microseconds
                    : static_cast<u64>(elapsed);
        }
    }

    // Now the slot is free: reset the allocator, and therefore the list.
    if (FAILED(frame.allocator->Reset())) {
        LOG_CRITICAL(Graphics, "D3D12 command: allocator reset failed.");
        return {};
    }
    if (FAILED(frame.list->Reset(frame.allocator.Get(), nullptr))) {
        LOG_CRITICAL(Graphics, "D3D12 command: list reset failed.");
        return {};
    }

    CommandList list;
    list.list = frame.list.Get();
    list.allocator = frame.allocator.Get();
    list.viewport_width_ = device_->swapchain_width != 0 ? device_->swapchain_width : 1920;
    list.viewport_height_ = device_->swapchain_height != 0 ? device_->swapchain_height : 1080;
    return list;
}

u64 CommandRing::EndFrame(CommandList& list) {
    if (!device_ || !list.list) {
        return 0;
    }
    list.list->Close();

    ID3D12CommandList* lists[] = {list.list};
    device_->graphics_queue->ExecuteCommandLists(1, lists);

    const u64 value = SignalFence(*device_);
    frames_[current_].fence_value = value;
    ++submitted_;
    current_ = (current_ + 1) % FramesInFlight;
    return value;
}

} // namespace Gr2Port::D3D12
