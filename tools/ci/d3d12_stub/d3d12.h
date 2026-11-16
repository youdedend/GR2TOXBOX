#pragma once
#ifndef DEFINE_ENUM_FLAG_OPERATORS
#define DEFINE_ENUM_FLAG_OPERATORS(x)
#endif
#include <cstdint>
#include <cstddef>
using UINT = unsigned int;
using INT = int;
using LONG = long;
using SIZE_T = size_t;
using HRESULT = long;
using BOOL = int;
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
struct GUID {};
using REFIID = const GUID&;
using HANDLE = void*;
using ULONG = unsigned long;
using UINT32 = unsigned int;
using UINT16 = unsigned short;
using WCHAR = wchar_t;
using LPCWSTR = const wchar_t*;
struct IDXGIAdapter;
struct D3D12_FEATURE_DATA_D3D12_OPTIONS { BOOL ResourceBindingTier_unused; };
struct D3D12_HEAP_DESC_unused {};
struct D3D12_RESOURCE_ALLOCATION_INFO;
using UINT64 = unsigned long long;
using UINT8 = unsigned char;
using INT64 = long long;
enum D3D12_HEAP_FLAGS { D3D12_HEAP_FLAG_NONE = 0 };
enum D3D12_RESOURCE_DIMENSION { D3D12_RESOURCE_DIMENSION_TEXTURE2D=3, D3D12_RESOURCE_DIMENSION_BUFFER = 1 };
enum D3D12_TEXTURE_LAYOUT { D3D12_TEXTURE_LAYOUT_UNKNOWN=0, D3D12_TEXTURE_LAYOUT_ROW_MAJOR = 1 };
enum D3D12_FEATURE_UNUSED { D3D12_FEATURE_UNUSED_0 = 0 };
enum D3D12_DESCRIPTOR_HEAP_TYPE { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV = 0, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER = 1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV = 2, D3D12_DESCRIPTOR_HEAP_TYPE_DSV = 3 };
enum D3D12_DESCRIPTOR_HEAP_FLAGS { D3D12_DESCRIPTOR_HEAP_FLAG_NONE = 0, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE = 1 };
struct D3D12_RESOURCE_ALLOCATION_INFO { UINT64 SizeInBytes; UINT64 Alignment; };
struct D3D12_RESOURCE_ALLOCATION_INFO1 { UINT64 Offset; UINT64 Alignment; UINT64 Size; };
struct D3D12_RANGE { SIZE_T Begin; SIZE_T End; };

using IID = GUID;
#define __uuidof(x) GUID{}
#define STDMETHODCALLTYPE
struct IUnknown {
    virtual HRESULT QueryInterface(const GUID&, void**) = 0;
    virtual unsigned long AddRef() = 0;
    virtual unsigned long Release() = 0;
};
#define IID_PPV_ARGS(p) GUID{}, reinterpret_cast<void**>(p)

enum D3D_FEATURE_LEVEL { D3D_FEATURE_LEVEL_12_0 = 0xc000, D3D_FEATURE_LEVEL_12_1 = 0xc100 };
enum DXGI_COLOR_SPACE_TYPE { DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 = 12 };
enum DXGI_ADAPTER_FLAG3 { DXGI_ADAPTER_FLAG3_NONE = 0 };
enum DXGI_FEATURE { DXGI_FEATURE_PRESENT_ALLOW_TEARING = 0 };
struct DXGI_ADAPTER_DESC3 { wchar_t Description[128]; SIZE_T DedicatedVideoMemory; };
struct D3D12_FEATURE_DATA_D3D12_OPTIONS4 { BOOL Native16BitShaderOpsSupported; };
enum D3D12_FEATURE { D3D12_FEATURE_D3D12_OPTIONS4 = 18 };
enum DXGI_FORMAT_R8G8B8A8_UNORM_unused { DXGI_FORMAT_R8G8B8A8_UNORM_X = 28, DXGI_FORMAT_R16G16B16A16_FLOAT_X = 10 };

enum DXGI_FORMAT {
DXGI_FORMAT_UNKNOWN=0,
DXGI_FORMAT_R32G32B32A32_FLOAT=2,
DXGI_FORMAT_R16G16B16A16_FLOAT=10,
DXGI_FORMAT_R16G16B16A16_UNORM=11,
DXGI_FORMAT_R16G16B16A16_SNORM=13,
DXGI_FORMAT_R32G32_FLOAT=16,
DXGI_FORMAT_R10G10B10A2_UNORM=24,
DXGI_FORMAT_R11G11B10_FLOAT=26,
DXGI_FORMAT_R8G8B8A8_UNORM=28,
DXGI_FORMAT_R8G8B8A8_UNORM_SRGB=29,
DXGI_FORMAT_R8G8B8A8_SNORM=31,
DXGI_FORMAT_R16G16_FLOAT=34,
DXGI_FORMAT_R16G16_UNORM=35,
DXGI_FORMAT_R16G16_SNORM=37,
DXGI_FORMAT_R16_TYPELESS=53,DXGI_FORMAT_D16_UNORM=55,DXGI_FORMAT_R32_TYPELESS=39,DXGI_FORMAT_R8G8B8A8_TYPELESS=27,DXGI_FORMAT_R16_UINT=57,
DXGI_FORMAT_D32_FLOAT=40,
DXGI_FORMAT_R32_FLOAT=41,
DXGI_FORMAT_R32_UINT=42,
DXGI_FORMAT_R8G8_UNORM=49,
DXGI_FORMAT_R8G8_SNORM=51,
DXGI_FORMAT_R16_FLOAT=54,
DXGI_FORMAT_R16_UNORM=56,
DXGI_FORMAT_R16_SNORM=58,
DXGI_FORMAT_R8_UNORM=61,
DXGI_FORMAT_R8_SNORM=63,
DXGI_FORMAT_BC1_UNORM=71,
DXGI_FORMAT_BC1_UNORM_SRGB=72,
DXGI_FORMAT_BC2_UNORM=74,
DXGI_FORMAT_BC2_UNORM_SRGB=75,
DXGI_FORMAT_BC3_UNORM=77,
DXGI_FORMAT_BC3_UNORM_SRGB=78,
DXGI_FORMAT_BC4_UNORM=80,
DXGI_FORMAT_BC4_SNORM=81,
DXGI_FORMAT_BC5_UNORM=83,
DXGI_FORMAT_BC5_SNORM=84,
DXGI_FORMAT_B8G8R8A8_UNORM=87,
DXGI_FORMAT_B8G8R8A8_UNORM_SRGB=91,
DXGI_FORMAT_BC6H_UF16=95,
DXGI_FORMAT_BC6H_SF16=96,
DXGI_FORMAT_BC7_UNORM=98,
DXGI_FORMAT_BC7_UNORM_SRGB=99
};
enum D3D12_COMMAND_LIST_TYPE { D3D12_COMMAND_LIST_TYPE_DIRECT = 0 };
enum D3D12_RESOURCE_STATES {
    D3D12_RESOURCE_STATE_COMMON = 0, D3D12_RESOURCE_STATE_PRESENT=0, D3D12_RESOURCE_STATE_GENERIC_READ=2755,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE = 1,
    D3D12_RESOURCE_STATE_RENDER_TARGET = 2,
    D3D12_RESOURCE_STATE_DEPTH_WRITE = 4,
    D3D12_RESOURCE_STATE_DEPTH_READ = 8,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS = 16,
    D3D12_RESOURCE_STATE_COPY_SOURCE=2048, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE=64, D3D12_RESOURCE_STATE_COPY_DEST = 32
};
enum D3D12_RESOURCE_BARRIER_TYPE { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION = 0, D3D12_RESOURCE_BARRIER_TYPE_UAV = 2 };
enum D3D12_RESOURCE_BARRIER_FLAGS { D3D12_RESOURCE_BARRIER_FLAG_NONE = 0 };
enum D3D12_HEAP_TYPE { D3D12_HEAP_TYPE_DEFAULT = 0, D3D12_HEAP_TYPE_UPLOAD = 1, D3D12_HEAP_TYPE_READBACK = 2 };
struct D3D12_HEAP_PROPERTIES { D3D12_HEAP_TYPE Type; UINT CreationNodeMask; UINT VisibleNodeMask; };
struct D3D12_HEAP_DESC { UINT64 SizeInBytes; D3D12_HEAP_PROPERTIES Properties; UINT64 Alignment; D3D12_HEAP_FLAGS Flags; };
enum D3D12_CPU_PAGE_PROPERTY { D3D12_CPU_PAGE_PROPERTY_UNKNOWN = 0 };
enum D3D12_MEMORY_POOL { D3D12_MEMORY_POOL_UNKNOWN = 0 };
enum D3D12_RESOURCE_FLAGS { D3D12_RESOURCE_FLAG_NONE = 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET=1,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL=2, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS = 4 };
enum D3D12_PRIMITIVE_TOPOLOGY_TYPE { D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH=4,D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE = 0 };
enum D3D12_CULL_MODE { D3D12_CULL_MODE_NONE = 0, D3D12_CULL_MODE_FRONT = 1, D3D12_CULL_MODE_BACK = 2 };
enum D3D12_COMPARISON_FUNC {
    D3D12_COMPARISON_FUNC_NEVER = 1,
    D3D12_COMPARISON_FUNC_EQUAL = 3,
    D3D12_COMPARISON_FUNC_GREATER = 5,
    D3D12_COMPARISON_FUNC_GREATER_EQUAL=7,D3D12_COMPARISON_FUNC_LESS_EQUAL=4,
    D3D12_COMPARISON_FUNC_ALWAYS = 8
};
enum D3D12_BLEND {D3D12_BLEND_ZERO=1,D3D12_BLEND_ONE=2,D3D12_BLEND_SRC_COLOR=3,D3D12_BLEND_INV_SRC_COLOR=4,D3D12_BLEND_SRC_ALPHA=5,D3D12_BLEND_INV_SRC_ALPHA=6,D3D12_BLEND_DEST_ALPHA=7,D3D12_BLEND_INV_DEST_ALPHA=8,D3D12_BLEND_DEST_COLOR=9,D3D12_BLEND_INV_DEST_COLOR=10,D3D12_BLEND_SRC_ALPHA_SAT=11,D3D12_BLEND_BLEND_FACTOR=14,D3D12_BLEND_INV_BLEND_FACTOR=15};
enum D3D12_BLEND_OP {D3D12_BLEND_OP_ADD=1,D3D12_BLEND_OP_SUBTRACT=2,D3D12_BLEND_OP_REV_SUBTRACT=3,D3D12_BLEND_OP_MIN=4,D3D12_BLEND_OP_MAX=5};
enum D3D12_LOGIC_OP { D3D12_LOGIC_OP_NOOP = 0 };
enum D3D12_COLOR_WRITE_ENABLE { D3D12_COLOR_WRITE_ENABLE_ALL = 0xf };
enum D3D12_DEPTH_WRITE_MASK { D3D12_DEPTH_WRITE_MASK_ZERO = 0, D3D12_DEPTH_WRITE_MASK_ALL = 1 };
enum D3D12_STENCIL_OP { D3D12_STENCIL_OP_KEEP = 1, D3D12_STENCIL_OP_REPLACE = 3 };
enum D3D12_FILL_MODE { D3D12_FILL_MODE_SOLID = 0 };
enum D3D12_CONSERVATIVE_RASTERIZATION_MODE { D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF = 0 };
enum D3D12_INDEX_BUFFER_STRIP_CUT_VALUE { D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED = 0 };
enum D3D12_ROOT_PARAMETER_TYPE {
    D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE = 0,
    D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS = 2
};
// The four range kinds the descriptor tables use. Adding them here is what makes
// the root-signature build of the port's table path checkable on this host: the
// values are the real ones, and DescriptorTables' range types are compared against
// them in descriptors.h.
enum D3D12_DESCRIPTOR_RANGE_TYPE {
    D3D12_DESCRIPTOR_RANGE_TYPE_SRV = 0,
    D3D12_DESCRIPTOR_RANGE_TYPE_UAV = 1,
    D3D12_DESCRIPTOR_RANGE_TYPE_CBV = 2,
    D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER = 3
};
enum D3D12_SHADER_VISIBILITY { D3D12_SHADER_VISIBILITY_ALL = 0 };
enum D3D12_ROOT_SIGNATURE_FLAGS { D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT = 1 };
enum D3D_ROOT_SIGNATURE_VERSION { D3D_ROOT_SIGNATURE_VERSION_1 = 1 };
enum D3D12_TEXTURE_COPY_TYPE { D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT=1, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX = 0 };
enum D3D12_PIPELINE_STATE_FLAGS { D3D12_PIPELINE_STATE_FLAG_NONE = 0 };
enum D3D12_CLEAR_FLAGS {
    D3D12_CLEAR_FLAG_DEPTH = 1,
    D3D12_CLEAR_FLAG_STENCIL = 2
};
#define D3D12_DEFAULT_STENCIL_READ_MASK 0xff
#define D3D12_DEFAULT_STENCIL_WRITE_MASK 0xff
#define D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES 0xffffffff
#define UINT_MAX 0xffffffff

struct ID3D12Object;
struct ID3D12RootSignature;
struct ID3D12Resource;
struct ID3D12DescriptorHeap;
struct ID3D12PipelineState;
struct D3D12_CPU_DESCRIPTOR_HANDLE { SIZE_T ptr; };
struct D3D12_GPU_DESCRIPTOR_HANDLE { unsigned long long ptr; };
struct D3D12_RECT { LONG left, top, right, bottom; };
struct D3D12_VIEWPORT { float TopLeftX, TopLeftY, Width, Height, MinDepth, MaxDepth; };
struct DXGI_SAMPLE_DESC {UINT Count,Quality;};
struct D3D12_RESOURCE_DESC {D3D12_RESOURCE_DIMENSION Dimension;UINT64 Alignment,Width;UINT Height;UINT16 DepthOrArraySize,MipLevels;DXGI_FORMAT Format;DXGI_SAMPLE_DESC SampleDesc;D3D12_TEXTURE_LAYOUT Layout;D3D12_RESOURCE_FLAGS Flags;};
struct D3D12_SUBRESOURCE_FOOTPRINT {DXGI_FORMAT Format;UINT Width,Height,Depth,RowPitch;};
struct D3D12_PLACED_SUBRESOURCE_FOOTPRINT {UINT64 Offset;D3D12_SUBRESOURCE_FOOTPRINT Footprint;};
struct D3D12_DESCRIPTOR_HEAP_DESC {D3D12_DESCRIPTOR_HEAP_TYPE Type;UINT NumDescriptors;D3D12_DESCRIPTOR_HEAP_FLAGS Flags;UINT NodeMask;};
struct D3D12_RENDER_TARGET_VIEW_DESC {};
BOOL CloseHandle(HANDLE);

struct D3D12_BOX {UINT left,top,front,right,bottom,back;};
struct D3D12_CLEAR_VALUE { int Format; float Color[4]; };
enum D3D12_SRV_DIMENSION {D3D12_SRV_DIMENSION_BUFFER=1,D3D12_SRV_DIMENSION_TEXTURE2D=4,D3D12_SRV_DIMENSION_TEXTURE2DARRAY=5,D3D12_SRV_DIMENSION_TEXTURECUBE=9};
enum D3D12_BUFFER_SRV_FLAGS {D3D12_BUFFER_SRV_FLAG_NONE=0,D3D12_BUFFER_SRV_FLAG_RAW=1};
enum D3D12_BUFFER_UAV_FLAGS {D3D12_BUFFER_UAV_FLAG_NONE=0,D3D12_BUFFER_UAV_FLAG_RAW=1};
enum D3D12_UAV_DIMENSION {D3D12_UAV_DIMENSION_BUFFER=1};
constexpr UINT D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING=5768;
enum D3D12_DSV_DIMENSION {D3D12_DSV_DIMENSION_TEXTURE2D=3,D3D12_DSV_DIMENSION_TEXTURE2DARRAY=4};
struct D3D12_DEPTH_STENCIL_VIEW_DESC {DXGI_FORMAT Format;D3D12_DSV_DIMENSION ViewDimension;UINT Flags;struct {UINT MipSlice;} Texture2D;struct {UINT MipSlice,FirstArraySlice,ArraySize,PlaneSlice;} Texture2DArray;};
struct D3D12_SHADER_RESOURCE_VIEW_DESC {DXGI_FORMAT Format;D3D12_SRV_DIMENSION ViewDimension;UINT Shader4ComponentMapping;struct {UINT MostDetailedMip,MipLevels,FirstArraySlice,ArraySize,PlaneSlice;float ResourceMinLODClamp;} Texture2DArray;struct {UINT MostDetailedMip,MipLevels,PlaneSlice;float ResourceMinLODClamp;} Texture2D;struct {UINT MostDetailedMip,MipLevels;float ResourceMinLODClamp;} TextureCube;struct {UINT64 FirstElement;UINT NumElements,StructureByteStride;D3D12_BUFFER_SRV_FLAGS Flags;} Buffer;};
struct D3D12_UNORDERED_ACCESS_VIEW_DESC {DXGI_FORMAT Format;D3D12_UAV_DIMENSION ViewDimension;struct {UINT64 FirstElement;UINT NumElements,StructureByteStride;UINT64 CounterOffsetInBytes;D3D12_BUFFER_UAV_FLAGS Flags;} Buffer;};
enum D3D12_FILTER {D3D12_FILTER_MIN_MAG_MIP_POINT=0,D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR=1,D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT=0x94,D3D12_FILTER_MIN_MAG_MIP_LINEAR=0x15};
enum D3D12_TEXTURE_ADDRESS_MODE {D3D12_TEXTURE_ADDRESS_MODE_CLAMP=3,D3D12_TEXTURE_ADDRESS_MODE_WRAP=1};
constexpr float D3D12_FLOAT32_MAX=3.402823466e+38f;
struct D3D12_SAMPLER_DESC {D3D12_FILTER Filter;D3D12_TEXTURE_ADDRESS_MODE AddressU,AddressV,AddressW;float MipLODBias;UINT MaxAnisotropy;D3D12_COMPARISON_FUNC ComparisonFunc;float BorderColor[4],MinLOD,MaxLOD;};
struct D3D12_RESOURCE_BARRIER {
    D3D12_RESOURCE_BARRIER_TYPE Type;
    D3D12_RESOURCE_BARRIER_FLAGS Flags;
    struct {
        ID3D12Resource* pResource;
        UINT Subresource;
        D3D12_RESOURCE_STATES StateBefore;
        D3D12_RESOURCE_STATES StateAfter;
    } Transition;
    struct { ID3D12Resource* pResource; } UAV;
};
struct D3D12_SHADER_BYTECODE { const void* pShaderBytecode; SIZE_T BytecodeLength; };
struct D3D12_RENDER_TARGET_BLEND_DESC {
    BOOL BlendEnable, LogicOpEnable;
    D3D12_BLEND SrcBlend, DestBlend;
    D3D12_BLEND_OP BlendOp;
    D3D12_BLEND SrcBlendAlpha, DestBlendAlpha;
    D3D12_BLEND_OP BlendOpAlpha;
    D3D12_LOGIC_OP LogicOp;
    unsigned char RenderTargetWriteMask;
};
struct D3D12_BLEND_DESC { BOOL AlphaToCoverageEnable; BOOL IndependentBlendEnable; D3D12_RENDER_TARGET_BLEND_DESC RenderTarget[8]; };
struct D3D12_RASTERIZER_DESC {
    D3D12_FILL_MODE FillMode;
    D3D12_CULL_MODE CullMode;
    BOOL FrontCounterClockwise;
    INT DepthBias;
    float DepthBiasClamp;
    float SlopeScaledDepthBias;
    BOOL DepthClipEnable;
    BOOL MultisampleEnable;
    BOOL AntialiasedLineEnable;
    UINT ForcedSampleCount;
    D3D12_CONSERVATIVE_RASTERIZATION_MODE ConservativeRaster;
};
struct D3D12_DEPTH_STENCILOP_DESC {
    D3D12_STENCIL_OP StencilFailOp, StencilDepthFailOp, StencilPassOp;
    D3D12_COMPARISON_FUNC StencilFunc;
};
struct D3D12_DEPTH_STENCIL_DESC {
    BOOL DepthEnable;
    D3D12_DEPTH_WRITE_MASK DepthWriteMask;
    D3D12_COMPARISON_FUNC DepthFunc;
    BOOL StencilEnable;
    unsigned char StencilReadMask, StencilWriteMask;
    D3D12_DEPTH_STENCILOP_DESC FrontFace, BackFace;
};
struct D3D12_INPUT_LAYOUT_DESC { const void* pInputElementDescs; UINT NumElements; };
struct D3D12_INDEX_BUFFER_VIEW { unsigned long long BufferLocation; UINT SizeInBytes; DXGI_FORMAT Format; };
struct D3D12_STREAM_OUTPUT_DESC { const void* pSODeclaration; UINT NumEntries; const UINT* pBufferStrides; UINT NumStrides; UINT RasterizedStream; };
struct D3D12_CACHED_PIPELINE_STATE { const void* pCachedBlob; SIZE_T CachedBlobSizeInBytes; };
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC {
    void* pRootSignature;
    D3D12_SHADER_BYTECODE VS, PS, DS, HS, GS;
    D3D12_STREAM_OUTPUT_DESC StreamOutput;
    D3D12_BLEND_DESC BlendState;
    UINT SampleMask;
    D3D12_RASTERIZER_DESC RasterizerState;
    D3D12_DEPTH_STENCIL_DESC DepthStencilState;
    D3D12_INPUT_LAYOUT_DESC InputLayout;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE PrimitiveTopologyType;
    UINT NumRenderTargets;
    DXGI_FORMAT RTVFormats[8];
    DXGI_FORMAT DSVFormat;
    DXGI_SAMPLE_DESC SampleDesc;
    UINT NodeMask;
    D3D12_CACHED_PIPELINE_STATE CachedPSO;
    D3D12_PIPELINE_STATE_FLAGS Flags;
};
// D3D12_GRAPHICS_PIPELINE_STATE_DESC deliberately has no PatchControlPoints: the
// patch size lives in the pipeline state *stream*
// (D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PATCH_CONTROL_POINTS). An earlier version
// of this stub declared it here, which let pipeline.cpp compile against a member
// the real SDK does not have and cost a Windows run to find.
struct D3D12_PIPELINE_STATE_STREAM_DESC {
    SIZE_T SizeInBytes;
    void* pPipelineStateSubobjectStream;
};
enum D3D12_PIPELINE_STATE_SUBOBJECT_TYPE {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS,
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS,
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS,
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PATCH_CONTROL_POINTS,
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID,
};
struct D3D12_COMPUTE_PIPELINE_STATE_DESC {
    void* pRootSignature;
    D3D12_SHADER_BYTECODE CS;
    UINT NodeMask;
    D3D12_CACHED_PIPELINE_STATE CachedPSO;
    D3D12_PIPELINE_STATE_FLAGS Flags;
};
struct D3D12_ROOT_CONSTANTS { UINT ShaderRegister, RegisterSpace, Num32BitValues; };
struct D3D12_DESCRIPTOR_RANGE {
    D3D12_DESCRIPTOR_RANGE_TYPE RangeType;
    UINT NumDescriptors;
    UINT BaseShaderRegister;
    UINT RegisterSpace;
    UINT OffsetInDescriptorsFromTableStart;
};
struct D3D12_ROOT_DESCRIPTOR_TABLE {
    UINT NumDescriptorRanges;
    const D3D12_DESCRIPTOR_RANGE* pDescriptorRanges;
};
struct D3D12_ROOT_PARAMETER {
    D3D12_ROOT_PARAMETER_TYPE ParameterType;
    D3D12_ROOT_DESCRIPTOR_TABLE DescriptorTable;
    D3D12_ROOT_CONSTANTS Constants;
    D3D12_SHADER_VISIBILITY ShaderVisibility;
};
struct D3D12_ROOT_SIGNATURE_DESC {
    UINT NumParameters;
    const D3D12_ROOT_PARAMETER* pParameters;
    UINT NumStaticSamplers;
    const void* pStaticSamplers;
    D3D12_ROOT_SIGNATURE_FLAGS Flags;
};
struct D3D12_TEXTURE_COPY_LOCATION { ID3D12Resource* pResource; D3D12_TEXTURE_COPY_TYPE Type;D3D12_PLACED_SUBRESOURCE_FOOTPRINT PlacedFootprint; UINT SubresourceIndex; };

struct ID3D12Object : IUnknown {};
struct ID3D12DeviceChild : ID3D12Object {};
struct ID3D12Pageable : ID3D12DeviceChild {};
struct ID3D12CommandAllocator : ID3D12Pageable { virtual HRESULT Reset() = 0; };
struct ID3D12CommandList : ID3D12DeviceChild { virtual D3D12_COMMAND_LIST_TYPE GetType() = 0; virtual HRESULT Close() = 0; };
struct ID3D12GraphicsCommandList : ID3D12CommandList {};
struct ID3D12GraphicsCommandList7 : ID3D12GraphicsCommandList {
    virtual void SetDescriptorHeaps(UINT, ID3D12DescriptorHeap* const*) = 0;
    virtual void SetGraphicsRootSignature(void*) = 0;
    virtual void SetComputeRootSignature(void*) = 0;
    virtual void SetGraphicsRoot32BitConstants(UINT, UINT, const void*, UINT) = 0;
    virtual void SetComputeRoot32BitConstants(UINT, UINT, const void*, UINT) = 0;
    // The descriptor-table bindings. Present here because the port binds its
    // tables every list -- this is the whole of what replaces SM 6.6 direct heap
    // indexing, so its absence from the stub would have left the table path
    // uncheckable on this host.
    virtual void SetGraphicsRootDescriptorTable(UINT, D3D12_GPU_DESCRIPTOR_HANDLE) = 0;
    virtual void SetComputeRootDescriptorTable(UINT, D3D12_GPU_DESCRIPTOR_HANDLE) = 0;
    virtual void SetPipelineState(void*) = 0;
    virtual void OMSetRenderTargets(UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*) = 0;
    virtual void OMSetStencilRef(UINT) = 0;
    virtual void OMSetBlendFactor(const float*) = 0;
    virtual void ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE, const float*, UINT, const D3D12_RECT*) = 0;
    virtual void ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CLEAR_FLAGS, float, unsigned char, UINT, const D3D12_RECT*) = 0;
    virtual void RSSetViewports(UINT, const D3D12_VIEWPORT*) = 0;
    virtual void RSSetScissorRects(UINT, const D3D12_RECT*) = 0;
    virtual void IASetPrimitiveTopology(UINT) = 0;
    virtual void IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW*) = 0;
    virtual void DrawInstanced(UINT, UINT, UINT, UINT) = 0;
    virtual void DrawIndexedInstanced(UINT, UINT, UINT, INT, UINT) = 0;
    virtual void Dispatch(UINT, UINT, UINT) = 0;
    virtual void ResourceBarrier(UINT, const D3D12_RESOURCE_BARRIER*) = 0;
    virtual void CopyBufferRegion(ID3D12Resource*,UINT64,ID3D12Resource*,UINT64,UINT64)=0;
    virtual void CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION*, UINT, UINT, UINT, const D3D12_TEXTURE_COPY_LOCATION*, const void*) = 0;
    virtual HRESULT Reset(ID3D12CommandAllocator*, ID3D12PipelineState*) = 0;
};
struct ID3D12PipelinePipelineUnused;
struct ID3D12PipelineState : ID3D12Pageable {};
// Serialize only. A library is loaded by passing its blob to
// ID3D12Device::CreatePipelineLibrary; there is no Deserialize on this interface.
// The stub used to declare one, and pipeline.cpp called it -- a mistake no
// amount of care in the backend would have caught against a stub that invented
// the method.
struct ID3D12PipelineLibrary : ID3D12DeviceChild {
    virtual SIZE_T GetSerializedSize() = 0;
    virtual HRESULT Serialize(void*, SIZE_T) = 0;
};
struct ID3D12Fence : ID3D12Pageable {
    virtual unsigned long long GetCompletedValue() = 0;
    virtual HRESULT SetEventOnCompletion(unsigned long long, void*) = 0;
};
struct ID3D12CommandQueue : ID3D12Pageable {
    virtual void ExecuteCommandLists(UINT, ID3D12CommandList* const*) = 0;
    virtual HRESULT Signal(ID3D12Fence*, unsigned long long) = 0;
};
struct ID3D12Device : ID3D12Object {
    virtual HRESULT CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC*,const GUID&,void**)=0;
    virtual void CreateShaderResourceView(ID3D12Resource*,const D3D12_SHADER_RESOURCE_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE)=0;
    virtual void CreateUnorderedAccessView(ID3D12Resource*,ID3D12Resource*,const D3D12_UNORDERED_ACCESS_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE)=0;
    virtual void CreateSampler(const D3D12_SAMPLER_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE)=0;
    virtual void CreateDepthStencilView(ID3D12Resource*,const D3D12_DEPTH_STENCIL_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE)=0;
    virtual void CreateRenderTargetView(ID3D12Resource*,const D3D12_RENDER_TARGET_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE)=0;
    virtual HRESULT CreateCommittedResource(const D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,const GUID&,void**)=0;
    virtual void GetCopyableFootprints(const D3D12_RESOURCE_DESC*,UINT,UINT,UINT64,D3D12_PLACED_SUBRESOURCE_FOOTPRINT*,UINT*,UINT64*,UINT64*)=0;

    virtual HRESULT CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE, const GUID&, void**) = 0;
    virtual HRESULT CreateCommandList(UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator*, ID3D12PipelineState*, const GUID&, void**) = 0;
    virtual HRESULT CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, const GUID&, void**) = 0;
    virtual HRESULT CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC*, const GUID&, void**) = 0;
    virtual HRESULT CreateRootSignature(UINT, const void*, SIZE_T, const GUID&, void**) = 0;
    virtual HRESULT CreatePipelineLibrary(const void*, SIZE_T, const GUID&, void**) = 0;
};
struct ID3D12Device8 : ID3D12Device {};
struct ID3D12Debug : IUnknown { virtual void EnableDebugLayer() = 0; };
struct ID3D12Debug1 : ID3D12Debug {
    virtual void SetEnableGPUBasedValidation(BOOL) = 0;
    virtual void SetEnableSynchronizedCommandQueueValidation(BOOL) = 0;
};
struct ID3DBlob : IUnknown { virtual void* GetBufferPointer() = 0; virtual SIZE_T GetBufferSize() = 0; };

struct ID3D12Resource : ID3D12Pageable {
    virtual UINT64 GetGPUVirtualAddress() = 0;
    virtual HRESULT Map(UINT, const D3D12_RANGE*, void**) = 0;
    virtual void Unmap(UINT, const D3D12_RANGE*) = 0;
    virtual D3D12_RESOURCE_DESC GetDesc() = 0;
};
using D3D12_GPU_VIRTUAL_ADDRESS = UINT64;
struct ID3D12DescriptorHeap : ID3D12Pageable {virtual D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandleForHeapStart()=0;virtual D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandleForHeapStart()=0;};
constexpr UINT D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST=4;
constexpr UINT D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP=5;

struct ID3D12Heap : ID3D12Pageable {};
struct ID3D12RootSignature : ID3D12Object {};
struct D3D12_RESOURCE_DESC_unused2 {};

HRESULT D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
HRESULT D3D12GetDebugInterface(const GUID&, void**);

using D3D_PRIMITIVE_TOPOLOGY=UINT;
constexpr UINT D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST=33;
