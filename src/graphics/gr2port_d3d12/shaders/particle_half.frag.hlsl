// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch2_scene.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer ParticleHalfRoot : register(b1) {
    uint composite;
};
cbuffer Batch2Textures : register(b2) { uint source; };
static float4 pixel_position, color;
static float output_depth;
// A half pixel's depth: the farthest of the scene's 2x2 pixels under it, the least in reverse
// depth, so that a particle in front of any of them draws.
void Downsample() {
    const int2 last = Batch2Size(source, 0) - 1;
    const int2 corner = 2 * int2(pixel_position.xy);
    const float a = Batch2Fetch(source, min(corner, last), 0).r;
    const float b = Batch2Fetch(source, min(corner + int2(1, 0), last), 0).r;
    const float c = Batch2Fetch(source, min(corner + int2(0, 1), last), 0).r;
    const float d = Batch2Fetch(source, min(corner + int2(1, 1), last), 0).r;
    output_depth = min(min(a, b), min(c, d));
}

// The half target's premultiplied colour, with the share of the scene that shows through in
// alpha, filtered by hand from the four texels around the pixel. Where all four still hold the
// clear value, the pixel keeps the scene's colour and alpha.
void Composite() {
    const int2 last = Batch2Size(source, 0) - 1;
    const float2 at = 0.5 * pixel_position.xy - 0.5;
    const int2 base = int2(floor(at));
    const float2 weight = at - float2(base);
    const float4 c00 = Batch2Fetch(source, clamp(base, int2(0, 0), last), 0);
    const float4 c10 = Batch2Fetch(source, clamp(base + int2(1, 0), int2(0, 0), last), 0);
    const float4 c01 = Batch2Fetch(source, clamp(base + int2(0, 1), int2(0, 0), last), 0);
    const float4 c11 = Batch2Fetch(source, clamp(base + int2(1, 1), int2(0, 0), last), 0);
    const float4 empty = float4(0.0, 0.0, 0.0, 1.0);
    if (all(c00 == empty) && all(c10 == empty) && all(c01 == empty) && all(c11 == empty)) {
        discard;
    }
    color = lerp(lerp(c00, c10, weight.x), lerp(c01, c11, weight.x), weight.y);
}

void ParticlePixel() {
    if (composite != 0u) {
        Composite();
    } else {
        Downsample();
    }
}

struct ParticleHalfOutput { float4 color : SV_Target0; float depth : SV_Depth; };
ParticleHalfOutput main(float4 position : SV_Position) {
    pixel_position=position; output_depth=position.z; color=float4(0.0,0.0,0.0,0.0);
    ParticlePixel(); ParticleHalfOutput o; o.color=color; o.depth=output_depth; return o;
}
