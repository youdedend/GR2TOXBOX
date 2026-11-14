// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <string>

#include "common/types.h"
#include "graphics/gr2port_d3d12/arena.h"
#include "graphics/gr2port_d3d12/device.h"
#include "graphics/gr2port_d3d12/pipeline.h"

namespace Gr2Port::D3D12 {

/// One frame's command recording: the D3D12 counterpart of upstream's
/// `vk::CommandBuffer` use in the renderer.
///
/// The structure is forced by the API. A D3D12 command allocator cannot be reset
/// while any command list recorded from it is still executing, so the backend
/// keeps one allocator, list and fence value **per frame in flight** and rotates
/// through them (`FramesInFlight` = 3, the same triple buffering upstream's Vulkan
/// path uses). BeginFrame waits on that slot's fence before resetting it, which is
/// the only place the CPU ever blocks on the GPU.
///
/// A `CommandList` is a thin, non-owning view over the slot of the frame being
/// recorded. It exists so the renderer reads as a sequence of named passes rather
/// than as a wall of `->`:
///
///     list.BeginRenderPass(pass);
///     list.SetPipeline(pipeline);
///     list.Draw(vertex_count, instance_count, base_instance);
///     list.EndRenderPass();
///
/// Barriers are explicit. The backend does not track resource states
/// automatically: with a scene capture that changes every frame, implicit state
/// tracking costs more than it saves and hides bugs, so a pass states its
/// transitions with Transition().
struct CommandList {
    ID3D12GraphicsCommandList7* list = nullptr;
    /// The allocator the list was reset from, for the owning Frame's bookkeeping.
    ID3D12CommandAllocator* allocator = nullptr;
    /// The swapchain's dimensions, so SetFullViewport has a target size without
    /// the renderer having to pass it to every pass.
    u32 viewport_width_ = 1920;
    u32 viewport_height_ = 1080;

    bool IsValid() const noexcept {
        return list != nullptr;
    }

    // -----------------------------------------------------------------------
    // Resource state
    // -----------------------------------------------------------------------

    /// A single transition. `subresource` is D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
    /// for the whole resource.
    void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after,
                    u32 subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) const;

    /// The two directions the renderer actually needs, named so a pass does not
    /// have to spell out D3D12's state sets.
    void ToRenderTarget(ID3D12Resource* resource, D3D12_RESOURCE_STATES before) const;
    void ToShaderRead(ID3D12Resource* resource, D3D12_RESOURCE_STATES before) const;
    /// Depth is a separate state from colour in D3D12, and a depth buffer read as
    /// a texture has to be in DEPTH_READ | PIXEL_SHADER_RESOURCE, not just the
    /// latter.
    void ToDepthWrite(ID3D12Resource* resource, D3D12_RESOURCE_STATES before) const;
    void ToDepthRead(ID3D12Resource* resource, D3D12_RESOURCE_STATES before) const;

    // -----------------------------------------------------------------------
    // Root signature and pipeline
    // -----------------------------------------------------------------------

    /// Sets the one root signature every pipeline in the backend uses, and the
    /// heap the shaders index directly. Called once per list, at Begin.
    void SetRootSignature(const RootSignature& root) const;

    /// Binds a pipeline state. The root signature stays set: D3D12 keeps it
    /// across SetPipelineState, which is why the backend can have one.
    void SetPipeline(const Pipeline& pipeline) const;

    /// The arena table (b0). Every shader that touches the arena needs it, so
    /// this is called by BeginRenderPass rather than by each pass.
    void SetArenaTable(const ArenaSet& arenas, const BindlessHeap& descriptors) const;

    /// Per-draw constants (b1).
    void SetDrawConstants(const void* data, u32 bytes) const;
    /// Per-pass constants (b2).
    void SetPassConstants(const void* data, u32 bytes) const;

    // -----------------------------------------------------------------------
    // Render passes
    // -----------------------------------------------------------------------

    /// A render target: the resource, the CPU view the pass binds, the state it
    /// is in when the pass begins, and whether to clear it.
    ///
    /// The view is passed in rather than looked up: the backend's render-target
    /// views are owned by whoever created the texture (presenter.cpp for the
    /// swapchain, renderer.cpp for the offscreen chain), and a lookup table from
    /// resource to view would be a cache to keep coherent for no benefit.
    struct RenderTarget {
        ID3D12Resource* resource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE view{};
        D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        /// The clear colour, used only when `do_clear` is true.
        float clear_colour[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        bool do_clear = true;
    };

    struct DepthTarget {
        ID3D12Resource* resource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE view{};
        D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        /// Reverse-Z clears to 0, which is far.
        float clear = 0.0f;
        /// A pass that only reads depth (the world's pixel stages) does not
        /// clear it and binds it read-only, so the depth test still works and
        /// the contents survive.
        bool read_only = false;
        u8 stencil_reference = 0;
    };

    /// Begins a pass: transitions the targets, clears those that ask for it,
    /// binds them, sets the root signature, the arena table and the viewport.
    ///
    /// Nothing is inherited: a pass that gives no depth target unbinds depth,
    /// which is what D3D12's OMSetRenderTargets(nullptr) does. Inheriting state
    /// across passes is how a depth buffer ends up accidentally writable in a
    /// post-processing pass.
    void BeginRenderPass(const RenderTarget* targets, u32 target_count,
                         const DepthTarget* depth, const RootSignature& root,
                         const ArenaSet& arenas, const BindlessHeap& descriptors);
    void BeginRenderPass(const RenderTarget& target, const DepthTarget* depth,
                         const RootSignature& root, const ArenaSet& arenas,
                         const BindlessHeap& descriptors);

    /// Transitions every target the pass wrote to the state its readers expect,
    /// by default both to shader-readable. Called for the targets the pass was
    /// begun with rather than the ones it happened to touch: a pass that leaves a
    /// target in RENDER_TARGET and reads it next frame is a bug the debug layer
    /// catches on the next bind, and this makes that impossible.
    void EndRenderPass(const RenderTarget* targets, u32 target_count,
                       const DepthTarget* depth,
                       D3D12_RESOURCE_STATES target_after = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATES depth_after = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // -----------------------------------------------------------------------
    // Draws
    // -----------------------------------------------------------------------

    /// A draw with no vertex buffer: the vertex shader pulls everything out of
    /// the arena. `base_instance` is what the shaders call the draw index.
    void Draw(u32 vertex_count, u32 instance_count = 1, u32 base_instance = 0) const;
    /// An indexed draw over an index buffer view the caller set.
    void DrawIndexed(u32 index_count, u32 instance_count = 1, u32 base_instance = 0) const;
    /// A full-screen triangle, which every post-processing pass uses:
    /// DrawInstanced(3, 1, 0, 0) and nothing else.
    void DrawFullscreen() const;

    void SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& view) const;
    /// A scissor that is not the whole target, for the UI's clipped regions.
    void SetScissor(const D3D12_RECT& rect) const;
    void SetFullViewport() const;
    /// An offscreen pass's own size, which is not the swapchain's (the bloom
    /// chain runs at a quarter of it).
    void SetViewport(u32 width, u32 height) const;

    // -----------------------------------------------------------------------
    // Compute
    // -----------------------------------------------------------------------

    /// A dispatch, after the caller sets its pipeline and constants. The
    /// groups are `ceil(extent / groupsize)` in each axis.
    void Dispatch(u32 groups_x, u32 groups_y, u32 groups_z) const;
    void DispatchForExtent(u32 width, u32 height, u32 group_size_x, u32 group_size_y) const;

    // -----------------------------------------------------------------------
    // Transfers
    // -----------------------------------------------------------------------

    void CopyBuffer(const D3D12_TEXTURE_COPY_LOCATION& dst,
                    const D3D12_TEXTURE_COPY_LOCATION& src) const;

    /// Closes the list. No command may be recorded after this, and the frame
    /// submits it immediately.
    void Close() const;
};

/// One frame in flight: its allocator, its list and the fence value that marks
/// the GPU having finished it.
struct Frame {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList7> list;
    /// The value the frame's last submit signalled; the frame may be reset only
    /// once the GPU has passed it.
    u64 fence_value = 0;
};

/// The frame ring: `FramesInFlight` frames, rotated by BeginFrame.
///
/// Deliberately not double-buffered beyond `FramesInFlight`: three is what
/// upstream's Vulkan path uses and what a 60 Hz console frame needs to keep the
/// CPU ahead of the GPU without adding latency the game's input would notice.
class CommandRing {
public:
    bool Create(Device& device);
    void Destroy();

    /// Waits for the GPU to finish the frame that this slot last recorded, resets
    /// that frame, and returns a view of it for the caller to record into.
    ///
    /// This is the backend's only CPU block on the GPU, and it blocks on the
    /// frame `FramesInFlight` ago rather than on the last one, so the CPU stays
    /// ahead by design.
    CommandList BeginFrame();

    /// Closes the frame's list, executes it and has the graphics queue signal the
    /// frame's fence. Returns the signalled value.
    u64 EndFrame(CommandList& list);

    /// The frame being recorded, for the diagnostics that want its fence value.
    [[nodiscard]] u32 CurrentFrameIndex() const noexcept {
        return current_;
    }
    [[nodiscard]] u64 FramesSubmitted() const noexcept {
        return submitted_;
    }
    /// The number of times a frame's start had to wait for the GPU, and how long
    /// the waits took in total. A healthy frame waits rarely; a large count means
    /// the CPU is producing more than the GPU can retire.
    struct WaitStats {
        u64 count = 0;
        u64 total_microseconds = 0;
        u64 longest_microseconds = 0;
    };
    [[nodiscard]] const WaitStats& Waits() const noexcept {
        return waits_;
    }

private:
    Device* device_ = nullptr;
    std::array<Frame, FramesInFlight> frames_{};
    u32 current_ = 0;
    u64 submitted_ = 0;
    WaitStats waits_{};
};

} // namespace Gr2Port::D3D12
