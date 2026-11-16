// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "graphics/gr2port_d3d12/pipeline.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include <d3d12sdklayers.h>

// The descriptor tables the root signature declares. They are shared with the
// shader header of the same name, so the spaces and counts cannot drift.
#include "graphics/gr2port_d3d12/descriptors.h"

#include "common/logging/log.h"
#include "uwp/blackbox_log.h"

namespace Gr2Port::D3D12 {

namespace {

// FNV-1a, the same function upstream uses for its pipeline cache keys and for
// its fault report's shader identities, so a hash in a log line is comparable
// between the two backends.
constexpr u64 FnvOffset = 1469598103934665603ULL;
constexpr u64 FnvPrime = 1099511628211ULL;

u64 HashBytes(u64 hash, std::span<const u8> bytes) {
    for (const u8 byte : bytes) {
        hash ^= byte;
        hash *= FnvPrime;
    }
    return hash;
}

/// Opens a wide-character path for binary I/O. The CRT's _wfopen_s on Windows
/// (which is where this runs); fopen with a converted name everywhere else, so
/// the file still compiles in the compile-only check.
FILE* OpenWide(const wchar_t* path, const wchar_t* mode) {
#if defined(_MSC_VER)
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, mode) != 0) {
        return nullptr;
    }
    return file;
#else
    char narrow_path[1024]{};
    char narrow_mode[16]{};
    for (size_t i = 0; i < 1023 && path[i] != 0; ++i) {
        narrow_path[i] = path[i] < 128 ? static_cast<char>(path[i]) : '?';
    }
    for (size_t i = 0; i < 15 && mode[i] != 0; ++i) {
        narrow_mode[i] = static_cast<char>(mode[i]);
    }
    return std::fopen(narrow_path, narrow_mode);
#endif
}

u64 HashScalar(u64 hash, u64 value) {
    for (int i = 0; i < 8; ++i) {
        hash ^= (value >> (i * 8)) & 0xFFu;
        hash *= FnvPrime;
    }
    return hash;
}

/// The root parameters: three blocks of 32-bit constants and one descriptor
/// table per descriptor kind.
///
/// The tables are here because the console caps at Shader Model 6.4, which has no
/// ResourceDescriptorHeap[] -- and a 6.6 module does not load at all on that
/// driver, so this is not a fallback path but the only path. Each table is bound
/// to the same range of the same heap (its base), with a different register
/// space; the shader's declared element type is what decides how a descriptor in
/// that range is read. See shaders/common/descriptors.hlsli, which declares the
/// same arrays at the same spaces and sizes.
struct RootParameterSet {
    /// b0, b1, b2, then one table per descriptor kind.
    static constexpr u32 kRootConstantCount = 3;
    static constexpr u32 kTableCount = DescriptorTables::kCount;
    static constexpr u32 kParameterCount = kRootConstantCount + kTableCount;

    D3D12_ROOT_PARAMETER parameters[kParameterCount];
    D3D12_DESCRIPTOR_RANGE ranges[kTableCount];
    D3D12_ROOT_SIGNATURE_DESC desc;
};

void BuildRootParameters(RootParameterSet& set) {
    // b0: the arena table.
    set.parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    set.parameters[0].Constants.ShaderRegister = 0;
    set.parameters[0].Constants.RegisterSpace = 0;
    set.parameters[0].Constants.Num32BitValues = RootConstants::ArenaBytes / 4;
    set.parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // b1: per-draw constants.
    set.parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    set.parameters[1].Constants.ShaderRegister = 1;
    set.parameters[1].Constants.RegisterSpace = 0;
    set.parameters[1].Constants.Num32BitValues = RootConstants::DrawBytes / 4;
    set.parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // b2: per-pass constants (texture handles).
    set.parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    set.parameters[2].Constants.ShaderRegister = 2;
    set.parameters[2].Constants.RegisterSpace = 0;
    set.parameters[2].Constants.Num32BitValues = RootConstants::PassBytes / 4;
    set.parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // One descriptor table per kind, each over the whole slot space of its heap
    // and in its own register space. The ranges are identical in offset and count
    // because they address the same heap; that is intentional, and D3D12 permits
    // it -- two ranges over one region are two typed views of it, which is
    // exactly what a handle-indexed descriptor space needs when the shader model
    // cannot pick the type at run time.
    for (u32 i = 0; i < DescriptorTables::kCount; ++i) {
        const DescriptorTables::Descriptor& table = DescriptorTables::kTables[i];
        D3D12_DESCRIPTOR_RANGE& range = set.ranges[i];
        range.RangeType = table.range_type;
        range.NumDescriptors = table.count;
        range.BaseShaderRegister = 0;
        range.RegisterSpace = table.space;
        range.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER& parameter = set.parameters[RootParameterSet::kRootConstantCount + i];
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.NumDescriptorRanges = 1;
        parameter.DescriptorTable.pDescriptorRanges = &range;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    set.desc.NumParameters = RootParameterSet::kParameterCount;
    set.desc.pParameters = set.parameters;
    // Static samplers are not used: the shaders take their samplers out of the
    // sampler table by index, because a pass picks a different one (repeating,
    // clamping, comparison) and a static sampler would have to be baked into the
    // root signature for it.
    set.desc.NumStaticSamplers = 0;
    set.desc.pStaticSamplers = nullptr;
    // The shaders index the heaps visibly, and the backend writes descriptors
    // from the CPU while the GPU may be reading the rest of the heap.
    // Only the input-assembler flag. The shaders still declare no input layout
    // and pull their vertices from the arena, but the flag has to be set for a
    // pipeline with a vertex shader and no vertex buffer to be valid.
    set.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
}

D3D12_BLEND_DESC BlendFor(const GraphicsPipelineDesc& desc) {
    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    for (auto& target : blend.RenderTarget) {
        target.BlendEnable = FALSE;
        target.LogicOpEnable = FALSE;
        target.SrcBlend = D3D12_BLEND_ONE;
        target.DestBlend = D3D12_BLEND_ZERO;
        target.BlendOp = D3D12_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D12_BLEND_ONE;
        target.DestBlendAlpha = D3D12_BLEND_ZERO;
        target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        target.LogicOp = D3D12_LOGIC_OP_NOOP;
        target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    if (!desc.alpha_blend && !desc.premultiplied_blend) {
        return blend;
    }

    // The Vulkan port's blend modes, translated. Its
    // premultiplied_blend is src = ONE, dst = ONE_MINUS_SRC_ALPHA on the colour
    // and the target keeps its alpha; its alpha_blend is the usual SRC_ALPHA /
    // ONE_MINUS_SRC_ALPHA.
    auto& target = blend.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.BlendOp = D3D12_BLEND_OP_ADD;
    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (desc.premultiplied_blend) {
        target.SrcBlend = D3D12_BLEND_ONE;
        target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    } else {
        target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    }
    if (desc.keep_target_alpha) {
        // Keep the target's alpha: the alpha blend is a no-op.
        target.SrcBlendAlpha = D3D12_BLEND_ZERO;
        target.DestBlendAlpha = D3D12_BLEND_ONE;
    } else if (desc.clear_target_alpha) {
        target.SrcBlendAlpha = D3D12_BLEND_ZERO;
        target.DestBlendAlpha = D3D12_BLEND_ZERO;
    } else {
        target.SrcBlendAlpha = D3D12_BLEND_ONE;
        target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    }
    return blend;
}

D3D12_DEPTH_STENCIL_DESC DepthStencilFor(const GraphicsPipelineDesc& desc) {
    D3D12_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = desc.depth_test ? TRUE : FALSE;
    depth.DepthWriteMask = desc.depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = desc.depth_compare;
    depth.StencilEnable = desc.stencil ? TRUE : FALSE;
    depth.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depth.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

    depth.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depth.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depth.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    if (desc.stencil_replace) {
        // Fragments that pass write the reference; the game uses this to stamp
        // the character's outline mask.
        depth.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    } else if (desc.stencil_equal) {
        // Only fragments where the stencil equals the reference pass.
        depth.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        depth.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
    } else {
        depth.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    }
    // Back faces match the front: the game's meshes are authored with consistent
    // winding and the port does not cull differently on the two sides.
    depth.BackFace = depth.FrontFace;
    return depth;
}

} // namespace

// ---------------------------------------------------------------------------
// Root signature
// ---------------------------------------------------------------------------

bool CreateRootSignature(Device& device, RootSignature& out) {
    if (!device.device) {
        LOG_CRITICAL(Graphics, "D3D12 pipeline: no device for the root signature.");
        return false;
    }

    RootParameterSet set{};
    BuildRootParameters(set);

    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&set.desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                            out.blob.GetAddressOf(), error.GetAddressOf());
    if (FAILED(hr)) {
        const char* text = error ? static_cast<const char*>(error->GetBufferPointer()) : "no detail";
        LOG_CRITICAL(Graphics, "D3D12 pipeline: root signature serialisation failed: {}", text);
        return false;
    }

    hr = device.device->CreateRootSignature(0, out.blob->GetBufferPointer(),
                                            out.blob->GetBufferSize(),
                                            IID_PPV_ARGS(out.signature.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_CRITICAL(Graphics, "D3D12 pipeline: CreateRootSignature failed: {}",
                     HresultToString(hr));
        return false;
    }
    LOG_INFO(Graphics,
             "D3D12 pipeline: root signature created ({} bytes of root constants, {} "
             "descriptor tables)",
             RootConstants::ArenaBytes + RootConstants::DrawBytes + RootConstants::PassBytes,
             DescriptorTables::kCount);
    return true;
}

// ---------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------

u64 HashShader(std::span<const u8> dxil) {
    return HashBytes(FnvOffset, dxil);
}

u64 PipelineCache::HashGraphics(const GraphicsPipelineDesc& desc) {
    u64 hash = FnvOffset;
    hash = HashBytes(hash, desc.vs);
    hash = HashBytes(hash, desc.ps);
    hash = HashBytes(hash, desc.gs);
    hash = HashBytes(hash, desc.hs);
    hash = HashBytes(hash, desc.ds);
    for (const DXGI_FORMAT format : desc.color_formats) {
        hash = HashScalar(hash, static_cast<u64>(format));
    }
    hash = HashScalar(hash, static_cast<u64>(desc.depth_format));
    hash = HashScalar(hash, desc.sample_count);
    hash = HashScalar(hash, static_cast<u64>(desc.topology));
    hash = HashScalar(hash, desc.patch_control_points);
    hash = HashScalar(hash, static_cast<u64>(desc.cull));
    hash = HashScalar(hash, desc.front_counter_clockwise ? 1u : 0u);
    hash = HashScalar(hash, (desc.depth_test ? 1u : 0u) | (desc.depth_write ? 2u : 0u) |
                                (desc.depth_bias ? 4u : 0u) | (desc.stencil ? 8u : 0u));
    hash = HashScalar(hash, static_cast<u64>(desc.depth_compare));
    hash = HashScalar(hash, (desc.stencil_replace ? 1u : 0u) | (desc.stencil_equal ? 2u : 0u));
    hash = HashScalar(hash, (desc.alpha_blend ? 1u : 0u) | (desc.keep_target_alpha ? 2u : 0u) |
                                (desc.premultiplied_blend ? 4u : 0u) |
                                (desc.clear_target_alpha ? 8u : 0u));
    return hash;
}

u64 PipelineCache::HashCompute(std::span<const u8> cs) {
    return HashBytes(FnvOffset, cs);
}

// ---------------------------------------------------------------------------
// Creation
// ---------------------------------------------------------------------------

bool CreateGraphicsPipeline(ID3D12Device8* device, const RootSignature& root,
                            const GraphicsPipelineDesc& desc, Pipeline& out) {
    if (!device || !root.IsValid()) {
        return false;
    }
    if (desc.vs.empty() && desc.cs.empty()) {
        LOG_CRITICAL(Graphics, "D3D12 pipeline: a graphics pipeline needs a vertex shader.");
        return false;
    }

    if(desc.hs.empty()!=desc.ds.empty() || (!desc.hs.empty() &&
       (desc.patch_control_points<1 || desc.patch_control_points>32)) || desc.color_formats.size()>8) {
        LOG_ERROR(Graphics,"D3D12 pipeline: invalid hull/domain pair, patch size or RTV count");
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
    state.pRootSignature = root.signature.Get();

    D3D12_SHADER_BYTECODE stages[5]{};
    stages[0].pShaderBytecode = desc.vs.empty() ? nullptr : desc.vs.data();
    stages[0].BytecodeLength = desc.vs.size();
    stages[1].pShaderBytecode = desc.ps.empty() ? nullptr : desc.ps.data();
    stages[1].BytecodeLength = desc.ps.size();
    stages[2].pShaderBytecode = desc.gs.empty() ? nullptr : desc.gs.data();
    stages[2].BytecodeLength = desc.gs.size();
    stages[3].pShaderBytecode = desc.hs.empty() ? nullptr : desc.hs.data();
    stages[3].BytecodeLength = desc.hs.size();
    stages[4].pShaderBytecode = desc.ds.empty() ? nullptr : desc.ds.data();
    stages[4].BytecodeLength = desc.ds.size();
    state.VS = stages[0];
    state.PS = stages[1];
    state.GS = stages[2];
    state.HS = stages[3];
    state.DS = stages[4];

    state.BlendState = BlendFor(desc);
    state.SampleMask = UINT_MAX;
    state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    state.RasterizerState.CullMode = desc.cull;
    state.RasterizerState.FrontCounterClockwise = desc.front_counter_clockwise ? TRUE : FALSE;
    state.RasterizerState.DepthBias = desc.depth_bias_constant;
    state.RasterizerState.DepthBiasClamp = desc.depth_bias_clamp;
    state.RasterizerState.SlopeScaledDepthBias = desc.depth_bias_slope;
    state.RasterizerState.DepthClipEnable = TRUE;
    state.RasterizerState.MultisampleEnable = FALSE;
    state.RasterizerState.AntialiasedLineEnable = FALSE;
    state.RasterizerState.ForcedSampleCount = 0;
    state.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    state.DepthStencilState = DepthStencilFor(desc);

    // No vertex input: every vertex shader pulls its attributes out of the arena
    // through SV_VertexID, which is the D3D12 equivalent of upstream's
    // buffer_reference pulling and needs no input layout at all.
    state.InputLayout = {nullptr, 0};
    state.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    state.PrimitiveTopologyType = desc.hs.empty() ? desc.topology : D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    state.NumRenderTargets = static_cast<UINT>(desc.color_formats.size());
    for (size_t i = 0; i < desc.color_formats.size() && i < 8; ++i) {
        state.RTVFormats[i] = desc.color_formats[i];
    }
    state.DSVFormat = desc.depth_format;
    state.SampleDesc.Count = desc.sample_count;
    state.SampleDesc.Quality = 0;
    // Patch arity is an IA topology, not a PSO stream subobject. The HS/DS
    // bytecode carries its own control-point contracts; SetPipeline binds the IA.

    const HRESULT hr = device->CreateGraphicsPipelineState(
        &state, IID_PPV_ARGS(out.state.GetAddressOf()));
    if (FAILED(hr)) {
        BlackboxRecordDeviceRemoved(device);
        LOG_CRITICAL(Graphics,
                     "D3D12 pipeline: CreateGraphicsPipelineState failed ({}): vs {} ps {} "
                     "targets {} depth {}",
                     HresultToString(hr), desc.vs.size(), desc.ps.size(),
                     desc.color_formats.size(), static_cast<int>(desc.depth_format));
        return false;
    }

    out.patch_control_points=desc.hs.empty()?0:desc.patch_control_points;
    out.vs = desc.vs;
    out.ps = desc.ps;
    out.gs = desc.gs;
    out.hs = desc.hs;
    out.ds = desc.ds;
    out.cs = {};
    out.hash = PipelineCache::HashGraphics(desc);
    Blackbox::PsoCreated(out.hash);
    return true;
}

bool CreateComputePipeline(ID3D12Device8* device, const RootSignature& root,
                           std::span<const u8> cs, Pipeline& out) {
    if (!device || !root.IsValid() || cs.empty()) {
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC state{};
    state.pRootSignature = root.signature.Get();
    state.CS.pShaderBytecode = cs.data();
    state.CS.BytecodeLength = cs.size();
    state.NodeMask = 0;
    state.CachedPSO = {nullptr, 0};
    state.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    const HRESULT hr =
        device->CreateComputePipelineState(&state, IID_PPV_ARGS(out.state.GetAddressOf()));
    if (FAILED(hr)) {
        BlackboxRecordDeviceRemoved(device);
        LOG_CRITICAL(Graphics, "D3D12 pipeline: CreateComputePipelineState failed: {}",
                     HresultToString(hr));
        return false;
    }
    out.vs = {};
    out.ps = {};
    out.gs = {};
    out.hs = {};
    out.ds = {};
    out.cs = cs;
    out.hash = PipelineCache::HashCompute(cs);
    Blackbox::PsoCreated(out.hash);
    return true;
}

// ---------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------

bool PipelineCache::Create(Device& device, const RootSignature& root) {
    device_ = &device;
    root_ = &root;
    // The driver's own library: D3D12 recompiles a pipeline state from scratch
    // without one, which for several thousand variants is most of the game's
    // start-up.
    //
    // Not on Xbox in UWP. `SraKmd_arden` removes the device as soon as
    // CreatePipelineLibrary is called -- no error return, a device removal -- so
    // the call itself is the crash. The DXIL blobs are still cached on disk below,
    // which is what saves the shader translation; what is lost is the driver's
    // own PSO cache, and states are then created each run.
#if defined(GR2_XBOX_UWP)
    LOG_INFO(Graphics,
             "D3D12 pipeline: no driver pipeline library on this platform (it removes "
             "the device); DXIL is cached on disk and states are created each run.");
#else
    if (device.device) {
        const HRESULT hr = device.device->CreatePipelineLibrary(
            nullptr, 0, IID_PPV_ARGS(library_.GetAddressOf()));
        if (SUCCEEDED(hr)) {
            LOG_INFO(Graphics, "D3D12 pipeline: driver pipeline library available.");
        } else {
            LOG_INFO(Graphics,
                     "D3D12 pipeline: driver pipeline library unavailable ({}); states will be "
                     "recreated each run.",
                     HresultToString(hr));
            library_.Reset();
        }
    }
#endif
    return true;
}

Pipeline* PipelineCache::GetOrCreateGraphics(const GraphicsPipelineDesc& desc) {
    const u64 key = HashGraphics(desc);
    std::lock_guard lock{mutex_};
    if (const auto it = pipelines_.find(key); it != pipelines_.end()) {
        ++hits_;
        return &it->second;
    }
    if (!device_ || !root_) {
        return nullptr;
    }
    // D3D12 pipeline creation is not thread-safe against one device, and the
    // game requests pipelines from several threads during its start; the lock
    // covers creation as well as the map.
    auto [it, inserted] = pipelines_.try_emplace(key);
    if (!inserted) {
        ++hits_;
        return &it->second;
    }
    if (!CreateGraphicsPipeline(device_->device.Get(), *root_, desc, it->second)) {
        pipelines_.erase(it);
        return nullptr;
    }
    return &it->second;
}

Pipeline* PipelineCache::GetOrCreateCompute(std::span<const u8> cs) {
    const u64 key = HashCompute(cs);
    std::lock_guard lock{mutex_};
    if (const auto it = pipelines_.find(key); it != pipelines_.end()) {
        ++hits_;
        return &it->second;
    }
    if (!device_ || !root_) {
        return nullptr;
    }
    auto [it, inserted] = pipelines_.try_emplace(key);
    if (!inserted) {
        ++hits_;
        return &it->second;
    }
    if (!CreateComputePipeline(device_->device.Get(), *root_, cs, it->second)) {
        pipelines_.erase(it);
        return nullptr;
    }
    return &it->second;
}

bool PipelineCache::SaveLibrary(const wchar_t* path) const {
    if (!library_ || !path) {
        return false;
    }
    const SIZE_T size = library_->GetSerializedSize();
    if (size == 0) {
        LOG_INFO(Graphics, "D3D12 pipeline: the library holds nothing to serialise.");
        return false;
    }
    std::vector<u8> bytes(size);
    if (FAILED(library_->Serialize(bytes.data(), size))) {
        return false;
    }
    // A plain fopen rather than a Win32 handle: this runs on both the desktop and
    // the console package, and the UWP AllowedPaths for the app's own local
    // folder cover the path the caller passes.
    //
    // _wfopen_s where the CRT has it, fopen elsewhere, rather than a call to
    // _wfopen_s throughout: the compile-only check in local_precheck.sh builds
    // this file on Linux, and a #include <fcntl.h> for the prototype would shadow
    // the system header that spdlog's file sink needs.
    FILE* file = OpenWide(path, L"wb");
    if (file == nullptr) {
        LOG_WARNING(Graphics, "D3D12 pipeline: could not write the pipeline library.");
        return false;
    }
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (written != bytes.size()) {
        LOG_WARNING(Graphics, "D3D12 pipeline: short write of the pipeline library.");
        return false;
    }
    LOG_INFO(Graphics, "D3D12 pipeline: wrote {} bytes of pipeline library.", bytes.size());
    return true;
}

bool PipelineCache::LoadLibrary(const wchar_t* path) {
    if (!library_ || !path) {
        return false;
    }
    FILE* file = OpenWide(path, L"rb");
    if (file == nullptr) {
        return false;
    }
    std::vector<u8> bytes;
    std::array<u8, 64 * 1024> chunk{};
    for (;;) {
        const size_t read = std::fread(chunk.data(), 1, chunk.size(), file);
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<ptrdiff_t>(read));
        if (read < chunk.size()) {
            break;
        }
    }
    std::fclose(file);
    if (bytes.empty()) {
        return false;
    }
    // A pipeline library is loaded by handing its blob back to the device.
    // ID3D12PipelineLibrary has Serialize but no Deserialize: the interface was
    // designed so that deserialisation happens where the library is created.
    ComPtr<ID3D12PipelineLibrary> loaded;
#if defined(GR2_XBOX_UWP)
    // Loading is the same call the driver objects to, and it objects to the call
    // rather than to what is passed to it; see Create() above. Report the cache as
    // inapplicable so the caller rebuilds, which is where a library-less run ends
    // up anyway.
    const HRESULT hr = E_NOTIMPL;
#else
    const HRESULT hr = device_->device->CreatePipelineLibrary(
        bytes.data(), bytes.size(), IID_PPV_ARGS(loaded.GetAddressOf()));
#endif
    if (SUCCEEDED(hr)) {
        std::lock_guard lock{mutex_};
        library_ = std::move(loaded);
    }
    if (FAILED(hr)) {
        // A library written by a different driver or a different shader set is
        // not an error: it simply does not apply, and the states are rebuilt.
        LOG_INFO(Graphics, "D3D12 pipeline: the cached library did not apply ({}).",
                 HresultToString(hr));
        return false;
    }
    LOG_INFO(Graphics, "D3D12 pipeline: loaded {} bytes of cached pipeline library.", bytes.size());
    return true;
}

void PipelineCache::Destroy() {
    std::lock_guard lock{mutex_};
    pipelines_.clear();
    library_.Reset();
    device_ = nullptr;
    root_ = nullptr;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

std::string DescribePipeline(const Pipeline& pipeline) {
    char text[96]{};
    if (!pipeline.cs.empty()) {
        std::snprintf(text, sizeof(text), "cs %016llx",
                      static_cast<unsigned long long>(HashShader(pipeline.cs)));
        return text;
    }
    const u64 vs = pipeline.vs.empty() ? 0 : HashShader(pipeline.vs);
    const u64 ps = pipeline.ps.empty() ? 0 : HashShader(pipeline.ps);
    std::snprintf(text, sizeof(text), "vs %016llx ps %016llx",
                  static_cast<unsigned long long>(vs), static_cast<unsigned long long>(ps));
    return text;
}

void EnableDebugLayer(bool gpu_validation) {
    // Device creation already enables the layer in Debug builds (device.cpp);
    // this adds GPU-based validation and the object-lifetime checks, which are
    // far slower and are therefore opt-in.
    if (!gpu_validation) {
        return;
    }
    ComPtr<ID3D12Debug> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf())))) {
        LOG_WARNING(Graphics, "D3D12: the debug layer is not installed; GPU validation is off.");
        return;
    }
    debug->EnableDebugLayer();
    ComPtr<ID3D12Debug1> debug1;
    if (SUCCEEDED(debug.As(&debug1))) {
        debug1->SetEnableGPUBasedValidation(TRUE);
        debug1->SetEnableSynchronizedCommandQueueValidation(TRUE);
        LOG_INFO(Graphics, "D3D12: GPU-based validation enabled.");
    }
}

} // namespace Gr2Port::D3D12
