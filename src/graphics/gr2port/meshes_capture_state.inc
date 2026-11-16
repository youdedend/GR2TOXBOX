// SPDX-License-Identifier: GPL-2.0-or-later
// Shared CPU capture implementation; included inside the backend namespace.
// The .gfx builder FUN_01447990 returns the model; the model unload FUN_01451980 takes a pointer
// to the model in rsi.
constexpr Hooks::Entry Builder{0x1447990, 6, Hooks::FramePrologue};
constexpr Hooks::Entry Unload{0x1451980, 6, Hooks::FramePrologue};

// Model: shape entries of 0x50 bytes at *(model + 0xb0) + *(model + 0xbc), count at +0xb8, the
// shape pointer first in each.
constexpr u64 ModelBlock = 0xb0;
constexpr u64 ModelShapeCount = 0xb8;
constexpr u64 ModelShapeOffset = 0xbc;
constexpr u64 ShapeEntrySize = 0x50;
// Shape: SObj kind at +0xa, vertex buffer object at +0x60, index buffer object at +0x188, skin
// object at +0x1a8.
constexpr u64 ShapeKind = 0xa;
constexpr u16 KindShape = 6;
constexpr u64 ShapeVertexBuffer = 0x60;
constexpr u64 ShapeHandle = 0x184;
constexpr u64 ShapeIndexBuffer = 0x188;
constexpr u64 ShapeSkin = 0x1a8;
constexpr u64 ShapeSoftBody = 0x1b0;
// SoftBody object: particle count +0x30, the buffer objects of the particle records +0x88, the
// constraints +0xa0 and the vertex to particle map +0xb8, each a MemBlock* then a V#.
constexpr u64 SoftBodyParticleCount = 0x30;
constexpr u64 SoftBodyParticles = 0x88;
constexpr u64 SoftBodyConstraints = 0xa0;
constexpr u64 SoftBodyMap = 0xb8;
constexpr u32 ParticleBytes = 8;
// Skin object: the vertex buffer object of the skin stream at +0x68.
constexpr u64 SkinVertexBuffer = 0x68;
constexpr u32 SkinStride = 16;
// Vertex buffer object: MemBlock* at +0, stride +0x18, element count +0x1c, vertex count +0x20,
// element of each semantic at +0x24, elements {format, stride, offset, semantic} at +0x34.
constexpr u64 VertexStride = 0x18;
constexpr u64 VertexElements = 0x1c;
constexpr u64 VertexSemanticElement = 0x24;
constexpr u64 VertexElementInfo = 0x34;
constexpr u32 SemanticPosition = 0;
constexpr u32 SemanticNormal = 2;
constexpr u32 SemanticVertexColor = 3;
constexpr u32 SemanticTangent = 14;
// Index buffer object: MemBlock* at +0, index size flag at +0x18 (0 = 16-bit).
constexpr u64 IndexSizeFlag = 0x18;
// MemBlock: data address at +0, size at +8.
constexpr u64 BlockSize = 8;

// Vertex formats the shaders decode: RGB32 FLOAT, RGBA32 FLOAT, RGBA16 FLOAT.
constexpr bool PositionFormat(u32 format) {
    return format == 3 || format == 4 || format == 7;
}

// Texture coordinate formats the shaders decode: RG32 FLOAT, RG16 FLOAT, RG16 SINT.
constexpr bool TexcoordFormat(u32 format) {
    return format == 2 || format == 6 || format == 12;
}

// The normal is 10_11_11 FLOAT, the tangent RGBA16 FLOAT.
constexpr bool NormalFormat(u32 format) {
    return format == 20;
}

constexpr bool TangentFormat(u32 format) {
    return format == 7;
}

// Vertex colour formats the shaders decode: RGBA8 UNORM, RGBA16 UNORM, RGBA16 FLOAT.
constexpr bool VertexColorFormat(u32 format) {
    return format == 16 || format == 10 || format == 7;
}

constexpr u64 UploadBudget = 64_MB;
constexpr u32 LoggedUploads = 8;
constexpr u32 LoggedSkins = 4;
constexpr u32 LoggedVertexStreams = 12;
constexpr u32 LoggedCloth = 4;
// The shared index buffer's indices: 64M, or 48M when the GPU's budget is small; a session through
// the game measured 42.5M at most.
constexpr u64 IndexCapacity = 64ull << 20;
constexpr u64 SmallIndexCapacity = 48ull << 20;
constexpr u64 SmallBudget = 3_GB;
// While the textures are squeezed, a static mesh no draw has asked for in EvictSeconds gives its
// memory back; the collector looks every ScanFrames frames and evicts at most EvictsPerScan.
constexpr u32 EvictSeconds = 60;
constexpr u64 ScanFrames = 64;
constexpr u32 EvictsPerScan = 256;
// Mesh::eviction.
constexpr u8 Kept = 0;
constexpr u8 Evicting = 1;
constexpr u8 Evicted = 2;
constexpr u8 Rescued = 3;

template <typename T>
T Load(VAddr address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

std::array<Mesh, Capacity> g_meshes;

// Guards the handle lists and the reads of game memory that unloads free.
std::mutex g_mutex;
std::vector<u32> g_free;
u32 g_unused = 0;
std::deque<u32> g_pending;
std::vector<u32> g_unloaded;
