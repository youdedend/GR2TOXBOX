// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch2_scene.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer MotionTileRoot : register(b1) {
    uint2 output_size;
    uint2 constants_ref_words;
};
#define constants_ref ArenaMake(constants_ref_words.x, constants_ref_words.y)
cbuffer Batch2Textures : register(b2) { uint velocity; uint tile_max; };
groupshared uint fastest;

// The game's MotionBlur tile maxima (CS 1c31720): a group per tile, whose 16 by 16 threads read
// the velocity spread over the tile. The fastest, in pixels with y down, is the tile's; its key is
// ten times its squared length above the thread's index.
[numthreads(16,16,1)]
void main(uint3 local_id : SV_GroupThreadID, uint3 group_id : SV_GroupID) {
ByteAddressBuffer b=ArenaBuffer(constants_ref,g_arena); uint at=constants_ref.offset;
    float4 target_size=asfloat(b.Load4(at)); float velocity_scale=asfloat(b.Load(at+16u));
    uint tile_width=b.Load(at+20u), tile_height=b.Load(at+24u);

    const uint thread = local_id.y * 16u + local_id.x;
    if (thread == 0u) {
        fastest = 0u;
    }
    const uint2 size = uint2(tile_width, tile_height);
    const uint2 tile = group_id.xy;
    const int2 pixel = int2(size * tile + ((size * local_id.xy) >> 4u));
    const float2 moved = Batch2FetchRaw(velocity, uint2(target_size.xy), pixel).xy;
    const float scale = velocity_scale;
    const float2 pixels = float2((target_size.x * scale) * moved.x * 0.5,
                             -0.5 * ((scale * target_size.y) * moved.y));
    const float length2 = pixels.x * pixels.x + pixels.y * pixels.y;
    const uint key = (uint(clamp(10.0 * length2, 0.0, 4294967040.0)) << 16u) + thread;
    GroupMemoryBarrierWithGroupSync();
    InterlockedMax(fastest, key);
    GroupMemoryBarrierWithGroupSync();
    if (fastest == key) {
        Batch2Store(tile_max, output_size, int2(tile), float4(pixels, 0.0, 0.0));
    }
}
