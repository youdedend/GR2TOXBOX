// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/types.h"
#include "graphics/gr2port_d3d12/descriptors.h"
#include "graphics/gr2port_d3d12/device.h"

namespace Gr2Port::D3D12 {

/// The backend's root signature, shared by every pipeline it creates.
///
/// Upstream's Vulkan pipelines take their descriptor sets and push constants as
/// push constants, with `descriptorBindingPartiallyBound` and
/// `runtimeDescriptorArray` letting one draw reach any texture. This backend
/// reproduces that shape in D3D12 with **one** root signature for the whole
/// renderer:
///
///   b0  ArenaConstants   ArenaTable g_arena, 5 uints (20 bytes)
///   b1  root constants   the per-draw block (up to 64 bytes, see DrawBytes)
///   b2  root constants   texture handles and the like (16 bytes)
///   12x descriptor tables, one per descriptor kind, each over the whole heap and
///       in its own register space
///
/// The descriptor tables are the part that replaces upstream's runtime descriptor
/// array. Shader Model 6.4, which is the console driver's ceiling, has no
/// `ResourceDescriptorHeap[]`, so a shader cannot index the heap directly; it
/// declares `Texture2D` / `SamplerState` / `ByteAddressBuffer` arrays in distinct
/// `register(..., spaceN)` spaces and indexes them with
/// `NonUniformResourceIndex(...)`. That is Resource Binding Tier 3's answer to the
/// same problem, and it keeps a handle a single u32 index into the heap.
///
/// The root constants are raw 32-bit values, not a cbuffer, so a draw sets them
/// with SetGraphicsRoot32BitConstants and they cost no descriptor.
struct RootSignature {
    ComPtr<ID3D12RootSignature> signature;
    ComPtr<ID3DBlob> blob;

    [[nodiscard]] bool IsValid() const noexcept {
        return signature != nullptr;
    }
};

/// How large each root-constant block is. The shaders declare these as cbuffers
/// with `register(b0..b2)`; a mismatch is caught at pipeline creation, because
/// D3D12 validates the root signature against the DXIL's declared resource
/// bindings.
namespace RootConstants {
// The authoritative register layout. Every shader and every caller has to agree
// with this, and tools/shaders/check_cbuffer_registers.py checks the HLSL half of
// it.
//
//   b0  ArenaTable g_arena      5 DWORDs   the arena table, SetArenaTable
//   b1  per-draw / per-pass     8 DWORDs   SetDrawConstants
//   b2  texture handles         4 DWORDs   SetPassConstants
//   descriptor tables          kCount      one root parameter each
//
// b0 is 20 bytes and not one byte more: SetArenaTable passes
// ArenaSet::RootArenaTable::srv_index, a five-element array on the stack, and the
// count it hands D3D12 is ArenaBytes / 4. Make ArenaBytes bigger than that struct
// and SetArenaTable reads past the end of it -- which is what happened when this
// was raised to 128 to accommodate ShadowConstants. A 128-byte b0 over a 20-byte
// source is a 108-byte stack overread on every pass, fed straight into the root
// signature. The static_assert in command.cpp is what now prevents it.

/// b0: ArenaKind::Count uints, the same five the shaders' ArenaTable declares.
inline constexpr u32 ArenaBytes = 20;

/// b1: at most 16 DWORDs. Bulk matrices live in arenas, never inline.
inline constexpr u32 DrawBytes = 64;
static_assert(DrawBytes <= 16 * sizeof(u32));

/// b2: texture and sampler handles for the passes that are not per-draw. The
/// largest block is four handles, in TonemapTextures and FogApplyTextures.
inline constexpr u32 PassBytes = 16;
} // namespace RootConstants

// D3D12 caps a root signature at 64 DWORDs on the hardware path. Each 32-bit
// constant parameter contributes its size in DWORDs and each descriptor table
// contributes one, so this is the whole budget:
//
//   5 (b0) + 16 (b1) + 4 (b2) + 12 (tables) = 37 of 64.
//
// Exceeding it fails root signature creation at device start with no useful
// message, so it is checked here rather than discovered on a console.
static_assert((RootConstants::ArenaBytes / 4) + (RootConstants::DrawBytes / 4) +
                      (RootConstants::PassBytes / 4) + DescriptorTables::kCount <=
                  64,
              "the root signature exceeds D3D12's 64-DWORD limit");

/// Serialises the root signature the shaders were compiled against and creates
/// it. Called once, after the device exists.
///
/// The signature is built from HLSL source rather than from the C++
/// `CD3DX12_ROOT_SIGNATURE_DESC` helpers, because those live in d3dx12.h, which
/// is samples-and-specs code rather than SDK API, and this project does not
/// vendor it.
bool CreateRootSignature(Device& device, RootSignature& out);

// ---------------------------------------------------------------------------
// Describing a pipeline
// ---------------------------------------------------------------------------

/// The state a pipeline is created from, in the port's own terms rather than
/// D3D12's. The D3D12 backend needs far less of it than the Vulkan one: there is
/// no render pass object, no dynamic state (every field below is baked into the
/// PSO), and no vertex input layout, because every shader pulls its vertices out
/// of the arena.
struct GraphicsPipelineDesc {
    /// The compiled DXIL container for each stage. Empty when the stage is
    /// absent; `cs` must be the only non-empty one for a compute pipeline.
    std::span<const u8> vs;
    std::span<const u8> ps;
    std::span<const u8> gs;
    std::span<const u8> hs;
    std::span<const u8> ds;
    std::span<const u8> cs;

    /// The entry point in each stage's DXIL. dxc was invoked with -E main and no
    /// renaming, so this is "main" unless a shader says otherwise.
    const char* entry = "main";

    /// Render target formats, in RTV order. Empty for a depth-only pass.
    std::span<const DXGI_FORMAT> color_formats;
    /// The DSV format, or DXGI_FORMAT_UNKNOWN for no depth attachment.
    DXGI_FORMAT depth_format = DXGI_FORMAT_UNKNOWN;
    /// A single-sample target. MSAA is not used by this port: the game's own
    /// antialiasing is a compute pass (antialias_off.comp).
    u32 sample_count = 1;

    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    /// One for triangles and lines; the tessellator's patch size for a hull
    /// shader; 1 for points.
    u32 patch_control_points = 0;

    D3D12_CULL_MODE cull = D3D12_CULL_MODE_NONE;
    bool front_counter_clockwise = false;

    bool depth_test = false;
    bool depth_write = true;
    /// The renderer runs reverse-Z, so the default is GREATER: nearer is greater.
    D3D12_COMPARISON_FUNC depth_compare = D3D12_COMPARISON_FUNC_GREATER;

    bool depth_bias = false;
    s32 depth_bias_constant = 0;
    float depth_bias_slope = 0.0f;
    float depth_bias_clamp = 0.0f;

    /// The stencil operations, or none. The backend's only use is the game's
    /// stencil-replace and stencil-equal passes.
    bool stencil = false;
    bool stencil_replace = false;
    bool stencil_equal = false;
    u8 stencil_reference = 0;

    /// The blend mode, by the meanings the Vulkan port's GraphicsPipelineInfo
    /// already gives these.
    bool alpha_blend = false;
    bool keep_target_alpha = false;
    bool premultiplied_blend = false;
    bool clear_target_alpha = false;

    /// A compute pipeline: `cs` is set and everything render-target-related is
    /// ignored.
    [[nodiscard]] bool IsCompute() const noexcept {
        return !cs.empty();
    }
};

/// A created pipeline and the root signature it was created against, plus the
/// metadata the debug overlay and the crash report want.
struct Pipeline {
    ComPtr<ID3D12PipelineState> state;
    /// The serialised DXIL of each stage, kept so the fault report can name the
    /// shader that was running when the device was lost.
    std::span<const u8> vs;
    std::span<const u8> ps;
    std::span<const u8> gs;
    std::span<const u8> hs;
    std::span<const u8> ds;
    std::span<const u8> cs;
    u64 hash = 0;

    [[nodiscard]] bool IsValid() const noexcept {
        return state != nullptr;
    }
};

// ---------------------------------------------------------------------------
// Creation and caching
// ---------------------------------------------------------------------------

/// Creates a graphics pipeline.
///
/// Pipeline creation in D3D12 is synchronous and cannot be done from several
/// threads against one device without a mutex, so this takes the state object's
/// lock. Upstream builds its Vulkan pipelines on background threads during the
/// game's start; here the equivalent is the cache below, which makes the second
/// and every later creation of the same state free.
bool CreateGraphicsPipeline(ID3D12Device8* device, const RootSignature& root,
                            const GraphicsPipelineDesc& desc, Pipeline& out);

/// Creates a compute pipeline.
bool CreateComputePipeline(ID3D12Device8* device, const RootSignature& root,
                           std::span<const u8> cs, Pipeline& out);

/// A cached set of pipeline state objects, keyed by everything that affects the
/// compiled state.
///
/// This is the D3D12 counterpart of upstream's `vk::PipelineCache`: the game
/// creates several thousand pipelines during its start (one per shader variant),
/// and creating each twice costs seconds. The key is a hash of the desc's
/// scalars and the shader hashes, so adding a stage or changing a blend mode
/// produces a distinct entry rather than a stale hit.
///
/// D3D12 also has a driver-level binary cache (`ID3D12PipelineLibrary`), and the
/// backend uses that too when the device supports it; this cache is the part
/// that also saves the CPU-side creation work.
class PipelineCache {
public:
    bool Create(Device& device, const RootSignature& root);

    /// Creates or returns the cached pipeline for `desc`. The returned pointer
    /// stays valid until Destroy(), so a caller may hold it for the frame.
    Pipeline* GetOrCreateGraphics(const GraphicsPipelineDesc& desc);
    /// The same for a compute pipeline, keyed by the shader's hash.
    Pipeline* GetOrCreateCompute(std::span<const u8> cs);

    /// Writes the driver pipeline library to `path`, so the next run creates the
    /// same states without re-running the driver's compiler. Called when the
    /// game shuts down, and safe to call with caching unsupported.
    bool SaveLibrary(const wchar_t* path) const;
    /// Loads a library written by SaveLibrary. A missing or corrupt file is not
    /// an error: the cache simply starts empty.
    bool LoadLibrary(const wchar_t* path);

    void Destroy();

    [[nodiscard]] u32 Count() const {
        return static_cast<u32>(pipelines_.size());
    }
    /// How many GetOrCreate calls were answered from the cache, for the overlay.
    [[nodiscard]] u64 Hits() const {
        return hits_;
    }

    /// The cache key for a description. Public because the free creation
    /// functions stamp a pipeline with the same hash it would be cached under,
    /// so a pipeline's identity is one function rather than two copies.
    static u64 HashGraphics(const GraphicsPipelineDesc& desc);
    static u64 HashCompute(std::span<const u8> cs);

private:

    Device* device_ = nullptr;
    const RootSignature* root_ = nullptr;
    ComPtr<ID3D12PipelineLibrary> library_;
    std::mutex mutex_;
    /// std::unordered_map, not a vector: the game's several thousand variants
    /// made a linear scan the second-most-expensive thing in the backend during
    /// start-up.
    std::unordered_map<u64, Pipeline> pipelines_;
    u64 hits_ = 0;
};

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// The FNV-1a hash of a DXIL container. Used as the pipeline's identity in
/// logs, in the fault report and in the library's names, so a shader change is
/// visible in a log line without embedding the container.
u64 HashShader(std::span<const u8> dxil);

/// A human-readable name for a pipeline, for the fault report and the overlay:
/// `<vs>+<ps>` by shader hash, truncated.
std::string DescribePipeline(const Pipeline& pipeline);

/// Called once per process, to enable the D3D12 debug layer and the GPU-based
/// validation when the build is a debug one. Errors are logged, not fatal: the
/// layer is not installed on a retail console.
void EnableDebugLayer(bool gpu_validation);

} // namespace Gr2Port::D3D12
