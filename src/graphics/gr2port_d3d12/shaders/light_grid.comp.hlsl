// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch3.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer LightGridRoot : register(b1) {
    uint2 light_ref_words;
    uint2 lights_words;
    uint2 grid_words;

};
#define light_ref ArenaMake(light_ref_words.x, light_ref_words.y)
#define lights ArenaMake(lights_words.x, lights_words.y)
#define grid ArenaMake(grid_words.x, grid_words.y)
static BatchLight light;
static const uint LightGridColumns=60u,LightGridRows=36u,LightGridSlices=8u,LightsPerCell=48u;
// The far end of the last slice.
static const float GridEnd = 10000.0;

groupshared uint hits;
groupshared uint hit_lights[LightsPerCell - 1u];

[numthreads(64,1,1)]
void main(uint3 group_id:SV_GroupID,uint thread_id:SV_GroupIndex) {
    light=LoadBatchLight(light_ref,g_arena);
    const uint3 cell = group_id;
    const uint thread = thread_id;
    if (thread == 0u) {
        hits = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // The slices double in depth from where they start; the last reaches GridEnd.
    const float front = light.grid_start + exp2(float(cell.z));
    const float back = cell.z == LightGridSlices - 1u
                           ? GridEnd
                           : light.grid_start + exp2(float(cell.z + 1u));
    // Planes through the eye at the cell's edges, facing into it: the column's right and left
    // edges in x and z, the row's lower and upper edges in y and z.
    const float2 half_grid = 0.5 * float2(LightGridColumns, LightGridRows);
    const float2 cell_xy = float2(cell.xy);
    const float2 right =
        normalize(float2(-half_grid.x * light.grid_scale.x, half_grid.x - 1.0 - cell_xy.x));
    const float2 left = normalize(float2(half_grid.x * light.grid_scale.x, cell_xy.x - half_grid.x));
    const float2 lower =
        normalize(float2(half_grid.y * light.grid_scale.y, half_grid.y - 1.0 - cell_xy.y));
    const float2 upper = normalize(float2(-half_grid.y * light.grid_scale.y, cell_xy.y - half_grid.y));
    for (uint i = thread; i < light.point_lights; i += 64u) {
        const float3 p = LoadBatchPointLight(ArenaAdd(lights,i*48u),g_arena).position;
        const float across = min(dot(right, p.xz), dot(left, p.xz));
        const float up = min(dot(lower, p.yz), dot(upper, p.yz));
        const float inside = min(min(across, up), min(back + p.z, -front - p.z));
        if (inside >= -LoadBatchPointLight(ArenaAdd(lights,i*48u),g_arena).range) {
            uint slot; InterlockedAdd(hits,1u,slot);
            if (slot < LightsPerCell - 1u) {
                hit_lights[slot] = i;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    const uint count = min(hits, LightsPerCell - 1u);
    const uint base =
        ((cell.z * LightGridRows + cell.y) * LightGridColumns + cell.x) * LightsPerCell;
    for (uint s = thread; s <= count; s += 64u) {
        B3Store(grid,base+s,s < count ? hit_lights[s] : ~0u,g_arena);
    }
}
