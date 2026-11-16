// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch2_scene.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer OutputRoot : register(b1) { uint2 output_size; };
cbuffer Batch2Textures : register(b2) { uint tile_max; uint neighbor_max; };
// The game's 64 by 60 tiles.
// Root dimensions permit the original 64x60 grid and resized targets.

groupshared uint fastest;

// The game's MotionBlur neighbour maxima (CS 1c31508): a group per tile, whose 3 by 3 threads read
// the tile and the tiles around it. The fastest, keyed as the tile maxima are, is the tile's.
[numthreads(3,3,1)]
void main(uint3 local_id : SV_GroupThreadID, uint3 group_id : SV_GroupID) {

    const uint thread = local_id.y * 3u + local_id.x;
    if (thread == 0u) {
        fastest = 0u;
    }
    const int2 tile = int2(group_id.xy);
    const int2 neighbor =
        clamp(tile + int2(local_id.xy) - int2(1, 1), int2(0, 0), int2(output_size) - int2(1,1));
    const float2 pixels = Batch2FetchRaw(tile_max, output_size, neighbor).xy;
    const float length2 = pixels.x * pixels.x + pixels.y * pixels.y;
    const uint key = (uint(clamp(10.0 * length2, 0.0, 4294967040.0)) << 16u) + thread;
    GroupMemoryBarrierWithGroupSync();
    InterlockedMax(fastest, key);
    GroupMemoryBarrierWithGroupSync();
    if (fastest == key) {
        Batch2Store(neighbor_max, output_size, tile, float4(pixels, 0.0, 0.0));
    }
}
