// SPDX-License-Identifier: GPL-2.0-or-later
// Port of effector.comp/effectors.glsl. Dispatch exactly (1,1,10) groups.
#include "common/arena.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer EffectorConstants : register(b1) {
    uint2 g_jobs_words;
    uint2 g_effectors_words;
    uint2 g_grid_words;
};
#define g_jobs ArenaMake(g_jobs_words.x, g_jobs_words.y)
#define g_effectors ArenaMake(g_effectors_words.x, g_effectors_words.y)
#define g_grid ArenaMake(g_grid_words.x, g_grid_words.y)
[numthreads(6, 6, 1)]
void main(uint3 cell : SV_DispatchThreadID) {
    if (any(cell >= uint3(6, 6, 10))) return;
    ByteAddressBuffer effectors = ArenaBuffer(g_effectors, g_arena);
    const uint offset = g_effectors.offset;
    const float4x4 view = ArenaLoadMatrix(g_effectors, g_arena);
    const float depth = asfloat(effectors.Load(offset + 64));
    const float start = asfloat(effectors.Load(offset + 68));
    const float slice = asfloat(effectors.Load(offset + 72));
    const uint count = effectors.Load(offset + 76);
    const float px = asfloat(effectors.Load(offset + 80));
    const float py = asfloat(effectors.Load(offset + 84));
    const float x = float(cell.x), y = float(cell.y);
    const float2 left = normalize(float2(-3.0 * px, 2.0 - x));
    const float2 right = normalize(float2(3.0 * px, x - 3.0));
    const float2 top = normalize(float2(3.0 * py, 2.0 - y));
    const float2 bottom = normalize(float2(-3.0 * py, y - 3.0));
    const float slice_start = 0.1 * depth * float(cell.z) + start;
    const float slice_end = slice + slice_start;
    uint entry = ((cell.z * 6u + cell.y) * 6u + cell.x) * 128u;
    const uint last = entry + 127u;
    for (uint s = 0; s < count; ++s) {
        const uint4 sphere = effectors.Load4(offset + 96u + s * 32u);
        const float3 center = mul(view, float4(asfloat(sphere.xyz), 1.0)).xyz;
        const float sides = min(min(dot(bottom, center.yz), min(dot(left, center.xz), dot(right, center.xz))), dot(top, center.yz));
        const float inside = min(min(slice_end + center.z, sides), -slice_start - center.z);
        if (inside >= -f16tof32(sphere.w & 0xffffu)) {
            ArenaStore(ArenaAdd(g_grid, entry * 4u), g_arena, s);
            entry = min(entry + 1u, last);
        }
    }
    ArenaStore(ArenaAdd(g_grid, entry * 4u), g_arena, 0xffffffffu);
}
