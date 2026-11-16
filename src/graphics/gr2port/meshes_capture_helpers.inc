// SPDX-License-Identifier: GPL-2.0-or-later
// Shared CPU capture implementation; included inside the backend namespace.
template <typename F>
void ForEachShape(VAddr model, F&& f) {
    const VAddr block = Load<VAddr>(model + ModelBlock);
    const u32 count = Load<u32>(model + ModelShapeCount);
    const s32 offset = Load<s32>(model + ModelShapeOffset);
    if (block == 0) {
        return;
    }
    for (u32 i = 0; i < count; i++) {
        const VAddr shape = Load<VAddr>(block + offset + i * ShapeEntrySize);
        if (shape != 0 && Load<u16>(shape + ShapeKind) == KindShape) {
            f(shape);
        }
    }
}

u32 g_missed_unloads = 0;
u32 g_lost_handles = 0;

void PS4_SYSV_ABI OnBuilt(u32, const Hooks::Arguments*, u64 model) {
    if (model == 0) {
        return;
    }
    std::scoped_lock lock{g_mutex};
    ForEachShape(model, [](VAddr shape) {
        // The handle field starts as heap contents, which can be the handle of an earlier
        // shape at this address.
        const u32 earlier = Load<u32>(shape + ShapeHandle);
        if (earlier < Capacity &&
            g_meshes[earlier].shape.load(std::memory_order_relaxed) == shape) {
            g_meshes[earlier].ready.store(false, std::memory_order_relaxed);
            g_meshes[earlier].shape.store(0, std::memory_order_release);
            g_unloaded.push_back(earlier);
            g_missed_unloads++;
        }
        u32 handle = Capacity;
        if (!g_free.empty()) {
            handle = g_free.back();
            g_free.pop_back();
        } else if (g_unused < Capacity) {
            handle = g_unused++;
        }
        std::memcpy(reinterpret_cast<void*>(shape + ShapeHandle), &handle, sizeof(handle));
        if (handle == Capacity) {
            return;
        }
        g_meshes[handle].ready.store(false, std::memory_order_relaxed);
        g_meshes[handle].eviction.store(Kept, std::memory_order_relaxed);
        g_meshes[handle].used.store(g_seconds.load(std::memory_order_relaxed),
                                    std::memory_order_relaxed);
        g_meshes[handle].shape.store(shape, std::memory_order_release);
        g_pending.push_back(handle);
    });
}

void PS4_SYSV_ABI OnUnload(u32, const Hooks::Registers* registers, u64) {
    const VAddr model = Load<VAddr>(registers->rsi);
    if (model == 0) {
        return;
    }
    std::scoped_lock lock{g_mutex};
    ForEachShape(model, [](VAddr shape) {
        const u32 handle = Load<u32>(shape + ShapeHandle);
        if (handle >= Capacity || g_meshes[handle].shape.load(std::memory_order_relaxed) != shape) {
            g_lost_handles += handle != Capacity;
            return;
        }
        g_meshes[handle].ready.store(false, std::memory_order_relaxed);
        g_meshes[handle].shape.store(0, std::memory_order_release);
        g_unloaded.push_back(handle);
    });
}

struct Source {
    VAddr vertices;
    u32 vertex_bytes;
    VAddr indices;
    u32 index_bytes;
    u32 stride;
    u32 position;
    u32 texcoord;
    u32 texcoord1;
    u32 normal;
    u32 tangent;
    u32 vertex_color;
    VAddr skin = 0;
    u32 skin_bytes = 0;
    VAddr particles = 0;
    u32 particle_bytes = 0;
    VAddr constraints = 0;
    u32 constraint_bytes = 0;
    VAddr particle_map = 0;
    u32 map_bytes = 0;
    u32 particle_count = 0;
};

// The shape's cloth data, when it has some.
void ReadSoftBody(VAddr shape, Source& source) {
    const VAddr soft_body = Load<VAddr>(shape + ShapeSoftBody);
    if (soft_body == 0) {
        return;
    }
    const auto block = [soft_body](u64 object, VAddr& data, u32& bytes) {
        const VAddr memory = Load<VAddr>(soft_body + object);
        data = memory != 0 ? Load<VAddr>(memory) : 0;
        bytes = memory != 0 ? Load<u32>(memory + BlockSize) : 0;
    };
    block(SoftBodyParticles, source.particles, source.particle_bytes);
    block(SoftBodyConstraints, source.constraints, source.constraint_bytes);
    block(SoftBodyMap, source.particle_map, source.map_bytes);
    source.particle_count = Load<u32>(soft_body + SoftBodyParticleCount);
}

// Whether the shaders can run the cloth data without reading past it: every anchor names a
// vertex, every constraint range lies in the constraints, and every constraint and map entry names
// a particle. The game's copy is read once, at upload.
bool ValidSoftBody(const Source& source, u32 vertex_count) {
    const u32 count = source.particle_count;
    const u32 constraint_count = source.constraint_bytes / 4;
    if (source.particles == 0 || source.constraints == 0 || source.particle_map == 0 ||
        count == 0 || u64{count} * ParticleBytes > source.particle_bytes ||
        u64{vertex_count} * 2 > source.map_bytes) {
        return false;
    }
    const auto* particles = reinterpret_cast<const u32*>(source.particles);
    for (u32 i = 0; i < count; i++) {
        const u32 anchor = particles[2 * i] & 0xffff;
        const u32 first = particles[2 * i] >> 16;
        const u32 links = particles[2 * i + 1] & 0xffff;
        if (anchor >= vertex_count || first + links > constraint_count) {
            return false;
        }
    }
    const auto* constraints = reinterpret_cast<const u32*>(source.constraints);
    for (u32 i = 0; i < constraint_count; i++) {
        if ((constraints[i] & 0xffff) >= count) {
            return false;
        }
    }
    const auto* map = reinterpret_cast<const u16*>(source.particle_map);
    return std::all_of(map, map + vertex_count, [count](u16 p) { return p < count; });
}

std::string DescribeElements(VAddr vb) {
    std::string elements;
    const u32 count = std::min<u32>(Load<u16>(vb + VertexElements), 16);
    for (u32 i = 0; i < count; i++) {
        const VAddr info = vb + VertexElementInfo + 4 * i;
        elements += fmt::format(" (format {} offset {} semantic {})", Load<u8>(info),
                                Load<u8>(info + 2), Load<u8>(info + 3));
    }
    return elements;
}

// Logs the elements of the first skin streams.
void LogSkin(VAddr shape, VAddr vb) {
    static u32 logged = 0;
    if (logged == LoggedSkins) {
        return;
    }
    logged++;
    LOG_INFO(Render_Vulkan, "GR2port: skin of mesh {:#x}: stride {},{}", shape,
             Load<u16>(vb + VertexStride), DescribeElements(vb));
}

// Logs the elements of the first vertex streams.
void LogVertices(VAddr shape, VAddr vb) {
    static u32 logged = 0;
    if (logged == LoggedVertexStreams) {
        return;
    }
    logged++;
    LOG_INFO(Render_Vulkan, "GR2port: vertices of mesh {:#x}: stride {},{}", shape,
             Load<u16>(vb + VertexStride), DescribeElements(vb));
}

// The element of a semantic, encoded as a position, when its format is one the shaders decode;
// else 0.
template <typename Accepted>
u32 ReadElement(VAddr vb, u32 semantic, Accepted accepted) {
    const u32 element = Load<u8>(vb + VertexSemanticElement + semantic);
    if (element >= Load<u16>(vb + VertexElements)) {
        return 0;
    }
    const u32 format = Load<u8>(vb + VertexElementInfo + 4 * element);
    const u32 offset = Load<u8>(vb + VertexElementInfo + 4 * element + 2);
    return accepted(format) && offset % 4 == 0 ? offset | format << 8 : 0;
}

// The texture coordinates: the two-component element of the lowest semantic after the position.
// Semantics follow each shader's inputs, so texture coordinates have no fixed one. skip passes
// over that many such elements.
u32 ReadTexcoord(VAddr vb, u32 skip = 0) {
    const u32 elements = std::min<u32>(Load<u16>(vb + VertexElements), 16);
    u32 texcoord = 0;
    u32 above = SemanticPosition;
    for (u32 pass = 0; pass <= skip; pass++) {
        texcoord = 0;
        u32 semantic = 16;
        for (u32 i = 0; i < elements; i++) {
            const VAddr info = vb + VertexElementInfo + 4 * i;
            const u32 format = Load<u8>(info);
            const u32 offset = Load<u8>(info + 2);
            const u32 element_semantic = Load<u8>(info + 3);
            if (element_semantic > above && element_semantic < semantic && TexcoordFormat(format) &&
                offset % 4 == 0) {
                texcoord = offset | format << 8;
                semantic = element_semantic;
            }
        }
        above = semantic;
    }
    return texcoord;
}

// The game's vertex format table: RGBA16 FLOAT.
constexpr u32 FormatRgba16Float = 7;

// A half float's value.
float FromHalf(u32 half) {
    const u32 sign = (half & 0x8000) << 16;
    const u32 exponent = half >> 10 & 0x1f;
    const u32 fraction = half & 0x3ff;
    if (exponent == 0) {
        // Zero or a subnormal.
        const float value = static_cast<float>(fraction) * 0x1p-24f;
        return sign != 0 ? -value : value;
    }
    const u32 wide = exponent == 0x1f ? 0xff : exponent + 112;
    return std::bit_cast<float>(sign | wide << 23 | fraction << 13);
}

// Sets mesh's box around the positions LoadPosition of vertex.glsl reads from the shape's vertex
// data, which every index names. A NaN is no bound of a float position and, with a half
// position, leaves the box without that bound.
void SetBox(const Source& source, u32 vertex_count, Mesh& mesh) {
    constexpr float Unbounded = std::numeric_limits<float>::infinity();
    const u32 offset = source.position & 0xff;
    const bool halves = (source.position >> 8) == FormatRgba16Float;
    std::fill_n(mesh.box_low, 3, -Unbounded);
    std::fill_n(mesh.box_high, 3, Unbounded);
    if (vertex_count == 0 || offset + (halves ? 6 : 12) > source.stride) {
        return;
    }
    const u8* vertex = reinterpret_cast<const u8*>(source.vertices) + offset;
    if (halves) {
        // Halves order as their sign and magnitude do.
        std::array<s32, 3> low;
        std::array<s32, 3> high;
        low.fill(std::numeric_limits<s32>::max());
        high.fill(std::numeric_limits<s32>::min());
        for (u32 i = 0; i < vertex_count; i++, vertex += source.stride) {
            std::array<u16, 3> position;
            std::memcpy(position.data(), vertex, sizeof(position));
            for (u32 k = 0; k < 3; k++) {
                const s32 magnitude = position[k] & 0x7fff;
                const s32 key = (position[k] & 0x8000) != 0 ? -magnitude : magnitude;
                low[k] = std::min(low[k], key);
                high[k] = std::max(high[k], key);
            }
        }
        const auto value = [](s32 key) {
            return FromHalf(key < 0 ? 0x8000u | static_cast<u32>(-key) : static_cast<u32>(key));
        };
        for (u32 k = 0; k < 3; k++) {
            mesh.box_low[k] = value(low[k]);
            mesh.box_high[k] = value(high[k]);
        }
        return;
    }
    std::array<float, 3> low;
    std::array<float, 3> high;
    low.fill(Unbounded);
    high.fill(-Unbounded);
    for (u32 i = 0; i < vertex_count; i++, vertex += source.stride) {
        std::array<float, 3> position;
        std::memcpy(position.data(), vertex, sizeof(position));
        for (u32 k = 0; k < 3; k++) {
            low[k] = position[k] < low[k] ? position[k] : low[k];
            high[k] = position[k] > high[k] ? position[k] : high[k];
        }
    }
    std::copy(low.begin(), low.end(), mesh.box_low);
    std::copy(high.begin(), high.end(), mesh.box_high);
}

// Sets the greatest sum of a vertex's four weights over a skinned shape, when no weight is below
// 0, for the box of a caster the shader skins.
void SetWeightSum(const Source& source, Mesh& mesh) {
    mesh.weight_sum = std::numeric_limits<double>::infinity();
    const u32 vertices = source.skin != 0 ? source.skin_bytes / SkinStride : 0;
    if (vertices == 0) {
        return;
    }
    const u8* record = reinterpret_cast<const u8*>(source.skin);
    double greatest = 0.0;
    for (u32 i = 0; i < vertices; i++, record += SkinStride) {
        std::array<u16, 4> weights;
        std::memcpy(weights.data(), record, sizeof(weights));
        // Four halves add up exactly in a double.
        double sum = 0.0;
        for (const u16 weight : weights) {
            const float value = FromHalf(weight);
            // A NaN fails the test too.
            if (!(value >= 0.0f)) {
                return;
            }
            sum += value;
        }
        greatest = std::max(greatest, sum);
    }
    mesh.weight_sum = greatest;
}

// The tree of boxes around the positions that each chunk of a shape's indices names, for
// Mesh::chunk_tree, and the chunks. None for a shape whose mesh has no box, or with one chunk.
std::unique_ptr<ChunkBox[]> BuildChunks(const Source& source, const Mesh& mesh, u32& chunks) {
    chunks = 0;
    const u32 indices = source.index_bytes / 2;
    const auto finite = [](float value) { return std::isfinite(value); };
    if (indices <= ChunkIndices || !std::all_of(mesh.box_low, mesh.box_low + 3, finite) ||
        !std::all_of(mesh.box_high, mesh.box_high + 3, finite)) {
        return nullptr;
    }
    const u32 count = (indices + ChunkIndices - 1) / ChunkIndices;
    const bool halves = (source.position >> 8) == FormatRgba16Float;
    const auto* index = reinterpret_cast<const u16*>(source.indices);
    const u8* positions = reinterpret_cast<const u8*>(source.vertices) + (source.position & 0xff);
    std::vector<ChunkBox> leaves(count);
    for (u32 chunk = 0; chunk < count; chunk++) {
        const u32 end = std::min(indices, (chunk + 1) * ChunkIndices);
        ChunkBox& box = leaves[chunk];
        if (halves) {
            // Halves order as their sign and magnitude do.
            std::array<s32, 3> low;
            std::array<s32, 3> high;
            low.fill(std::numeric_limits<s32>::max());
            high.fill(std::numeric_limits<s32>::min());
            for (u32 i = chunk * ChunkIndices; i < end; i++) {
                std::array<u16, 3> position;
                std::memcpy(position.data(), positions + u64{index[i]} * source.stride,
                            sizeof(position));
                for (u32 k = 0; k < 3; k++) {
                    const s32 magnitude = position[k] & 0x7fff;
                    const s32 key = (position[k] & 0x8000) != 0 ? -magnitude : magnitude;
                    low[k] = std::min(low[k], key);
                    high[k] = std::max(high[k], key);
                }
            }
            const auto value = [](s32 key) {
                return FromHalf(key < 0 ? 0x8000u | static_cast<u32>(-key) : static_cast<u32>(key));
            };
            for (u32 k = 0; k < 3; k++) {
                box.low[k] = value(low[k]);
                box.high[k] = value(high[k]);
            }
            continue;
        }
        std::fill_n(box.low, 3, std::numeric_limits<float>::infinity());
        std::fill_n(box.high, 3, -std::numeric_limits<float>::infinity());
        for (u32 i = chunk * ChunkIndices; i < end; i++) {
            std::array<float, 3> position;
            std::memcpy(position.data(), positions + u64{index[i]} * source.stride,
                        sizeof(position));
            for (u32 k = 0; k < 3; k++) {
                box.low[k] = position[k] < box.low[k] ? position[k] : box.low[k];
                box.high[k] = position[k] > box.high[k] ? position[k] : box.high[k];
            }
        }
    }
    auto tree = std::make_unique<ChunkBox[]>(2 * count - 1);
    const auto build = [&](const auto& self, u32 node, u32 low, u32 high) -> void {
        if (high - low == 1) {
            tree[node] = leaves[low];
            return;
        }
        const u32 middle = (low + high) / 2;
        const u32 second = node + 2 * (middle - low);
        self(self, node + 1, low, middle);
        self(self, second, middle, high);
        for (u32 k = 0; k < 3; k++) {
            tree[node].low[k] = std::min(tree[node + 1].low[k], tree[second].low[k]);
            tree[node].high[k] = std::max(tree[node + 1].high[k], tree[second].high[k]);
        }
    };
    build(build, 0, 0, count);
    chunks = count;
    return tree;
}

// Reads a shape's streams; false when the shaders cannot draw it. A shape without indices, as the
// game's Branch shapes, which it draws as patches of their vertices, gets no indices.
bool ReadShape(VAddr shape, Source& source) {
    const VAddr vb = shape + ShapeVertexBuffer;
    const VAddr vb_block = Load<VAddr>(vb);
    const VAddr ib = shape + ShapeIndexBuffer;
    const VAddr ib_block = Load<VAddr>(ib);
    const bool indexed = ib_block != 0 && Load<VAddr>(ib_block) != 0;
    if (vb_block == 0 || (indexed && Load<u32>(ib + IndexSizeFlag) != 0)) {
        return false;
    }
    const u32 element = Load<u8>(vb + VertexSemanticElement + SemanticPosition);
    if (element >= Load<u16>(vb + VertexElements)) {
        return false;
    }
    const u32 format = Load<u8>(vb + VertexElementInfo + 4 * element);
    const u32 offset = Load<u8>(vb + VertexElementInfo + 4 * element + 2);
    source = {
        .vertices = Load<VAddr>(vb_block),
        .vertex_bytes = Load<u32>(vb_block + BlockSize),
        .indices = indexed ? Load<VAddr>(ib_block) : 0,
        .index_bytes = indexed ? Load<u32>(ib_block + BlockSize) : 0,
        .stride = Load<u16>(vb + VertexStride),
        .position = offset | format << 8,
        .texcoord = ReadTexcoord(vb),
        .texcoord1 = ReadTexcoord(vb, 1),
        .normal = ReadElement(vb, SemanticNormal, NormalFormat),
        .tangent = ReadElement(vb, SemanticTangent, TangentFormat),
        .vertex_color = ReadElement(vb, SemanticVertexColor, VertexColorFormat),
    };
    LogVertices(shape, vb);
    const VAddr skin = Load<VAddr>(shape + ShapeSkin);
    if (skin != 0) {
        const VAddr skin_vb = skin + SkinVertexBuffer;
        const VAddr skin_block = Load<VAddr>(skin_vb);
        LogSkin(shape, skin_vb);
        if (skin_block == 0 || Load<u16>(skin_vb + VertexStride) != SkinStride) {
            return false;
        }
        source.skin = Load<VAddr>(skin_block);
        source.skin_bytes = Load<u32>(skin_block + BlockSize);
        if (source.skin == 0) {
            return false;
        }
    }
    ReadSoftBody(shape, source);
    return source.vertices != 0 && source.vertex_bytes != 0 &&
           (!indexed || source.index_bytes >= 2) && PositionFormat(format) &&
           source.stride % 4 == 0 && offset % 4 == 0;
}
