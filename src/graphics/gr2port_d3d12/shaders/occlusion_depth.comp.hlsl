// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch2_scene.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer OcclusionDepthRoot : register(b1) {
    uint2 output_size;
    int level;
};
cbuffer Batch2Textures : register(b2) { uint scene_depth; uint level_image; uint character_depth; };
// The game's depth mips for its SSAO (CS 0x1c24124): a texel of a level reads the full depth, the
// nearer of the world and the characters, at its place scaled by the level, one pixel along each
// axis by the other axis's parity.
[numthreads(8,8,1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
    const int2 texel = int2(dispatch_id.xy);
    if (any((texel >= int2(output_size)))) {
        return;
    }
    const int2 size = Batch2Size(scene_depth, 0);
    const int2 source = min((texel << level) + (texel.yx & int2(1, 1)), size - int2(1, 1));
    const float depth =
        max(Batch2Fetch(scene_depth, source, 0).r, Batch2Fetch(character_depth, source, 0).r);
    Batch2Store(level_image, output_size, texel, float4(depth,depth,depth,depth));
}
