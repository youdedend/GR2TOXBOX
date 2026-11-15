// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// contour.vert -> contour VS_6_6.
//
// Ported from src/graphics/gr2port/shaders/contour.vert: the game's G2_CH_Contour
// vertex programs, which build the outline around a character. The upstream shader
// places the vertex in OpenGL clip space and works out, per vertex, which way the
// silhouette should move on the screen and how strongly.
//
// Two substitutions, and nothing else changes:
//
//   * gl_DrawID becomes a root constant. GLSL's draw ID is per-command metadata;
//     D3D12 has none, so the host writes the contour's index into the root
//     constants for each draw. It is the same number the shader already indexed
//     the table with.
//   * The 64-bit GPU pointers (vertices, skin, palette) become ArenaRefs, through
//     the arena header, exactly as scene.hlsli does for draws.
//
// The arithmetic is transcribed rather than reworked. The outline's look is the
// game's, and every constant in it was tuned against the original.

#include "common/arena.hlsli"
#include "common/vertex.hlsli"

cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

cbuffer ContourRoot : register(b1) {
    // The contour table (scene.glsl's Contours) is an arena ref, and this draw's
    // index in it is what gl_DrawID used to be.
    ArenaRef g_contours;
    uint g_contour_index;
    uint3 g_contour_pad;
};

// One Contour is 288 bytes, and the offsets below are the std430 layout computed
// from scene.glsl's declaration order rather than counted by hand -- the first
// attempt at this file was 16 bytes short because `padding` after `fading` was
// missed, and every field would have read its neighbour.
//
//   0 world_view, 64 world_view_projection, 128 fade_center,
//   144 transparency, 148 fading, 152 padding (vec2), 160 inverse_z,
//   168 size_amplitude, 172 fading_scale, 176 threshold, 180 offset,
//   184 bias (vec2, so 192 next), 192 color,
//   208 size, 212 line_amplitude, 216 line_cycle, 220 curve_amplitude,
//   224 curve_cycle, 228 softness, 232 pixel_padding,
//   240 vertices (8), 248 stride, 252 position, 256 normal, 260 bones,
//   264 skin (8), 272 palette (8), 288 total (a multiple of 16).
#define CONTOUR_BYTES 288u

// The fields this stage reads, in scene.glsl's declaration order.
struct Contour {
    float4x4 world_view;
    float4x4 world_view_projection;
    float4 fade_center;
    float transparency;
    float fading;
    ArenaRef vertices;
    uint stride;
    uint position;
    uint normal;
    uint bones;
    ArenaRef skin;
    ArenaRef palette;
};

// The fields are read at their declared offsets rather than as one struct load:
// ByteAddressBuffer loads scalars and vectors only, which is the same reason
// scene.hlsli's LoadDrawAt is written out.
Contour LoadContour(ArenaRef base, ArenaTable table) {
    Contour c;
    c.world_view = ArenaLoadMatrix(base, table);
    c.world_view_projection = ArenaLoadMatrix(ArenaAdd(base, 64), table);
    c.fade_center = ArenaLoadFloat4(ArenaAdd(base, 128), table);
    c.transparency = ArenaLoadFloat(ArenaAdd(base, 144), table);
    c.fading = ArenaLoadFloat(ArenaAdd(base, 148), table);
    // 152..239: padding, inverse_z, the size and fade tuning, bias, color, the line
    // and curve tuning, pixel_padding -- the geometry and fragment stages' fields,
    // not this one's.
    const uint2 vertices = ArenaLoad2(ArenaAdd(base, 240), table);
    // ArenaMake, not ArenaRef{...}: HLSL 2021 rejects C++ brace-initialisation of a
    // struct, and dxc says so at this line. The helper is the arena header's own.
    c.vertices = ArenaMake(vertices.x, vertices.y);
    c.stride = ArenaLoad(ArenaAdd(base, 248), table);
    c.position = ArenaLoad(ArenaAdd(base, 252), table);
    c.normal = ArenaLoad(ArenaAdd(base, 256), table);
    c.bones = ArenaLoad(ArenaAdd(base, 260), table);
    const uint2 skin = ArenaLoad2(ArenaAdd(base, 264), table);
    c.skin = ArenaMake(skin.x, skin.y);
    const uint2 palette = ArenaLoad2(ArenaAdd(base, 272), table);
    c.palette = ArenaMake(palette.x, palette.y);
    return c;
}

struct VsOutput {
    float4 position : SV_Position;
    float2 direction : TEXCOORD0;
    float facing : TEXCOORD1;
    float phase : TEXCOORD2;
    float strength : TEXCOORD3;
    nointerpolation uint contour_index : TEXCOORD4;
};

VsOutput main(uint vertex_index : SV_VertexID) {
    VsOutput output;
    const Contour contour = LoadContour(ArenaAdd(g_contours, g_contour_index * CONTOUR_BYTES), g_arena);

    // Unskinned position, then through the palette when the mesh is skinned.
    // vertex.hlsli owns both, so the outline's vertices land where the character's
    // own fragment shader expects them on the same mesh.
    const float3 unskinned =
        LoadPosition(contour.vertices, contour.stride, contour.position, vertex_index, g_arena);
    float3 model = unskinned;
    float3 n = LoadNormal(contour.vertices, contour.stride, contour.normal, vertex_index, g_arena);
    if (contour.bones != 0u) {
        model = SkinPosition(unskinned, contour.skin, contour.palette, contour.bones, vertex_index,
                             g_arena);
        n = SkinDirection(n, contour.skin, contour.palette, contour.bones, vertex_index, g_arena);
    }

    output.position = mul(contour.world_view_projection, float4(model, 1.0));
    const float3 view_position = mul(contour.world_view, float4(model, 1.0)).xyz;
    // A direction, not a point: the 3x3 of the world-view drops the translation the
    // way GLSL's mat3(world_view) does.
    const float3 view_normal = normalize(mul((float3x3)contour.world_view, n));

    // The way the silhouette moves: the view normal projected onto the screen and
    // flipped. The 1e-12 floor is upstream's guard for a vertex whose normal faces
    // the camera, where the outline has no direction to grow in and the division
    // would be by zero.
    const float2 planar = view_normal.xy;
    output.direction = -planar * rsqrt(max(dot(planar, planar), 1e-12));

    // How much the vertex faces the camera: 0 on the silhouette, 1 head-on.
    output.facing = abs(view_normal.z);
    // A phase from the vertex's own position, so the stroke's waves travel along the
    // mesh instead of with the screen.
    output.phase = 0.001 * (unskinned.x + unskinned.y + unskinned.z);

    // How much outline to draw here: it fades with the distance from fade_center
    // along the view axis, and a transparency floor keeps a little everywhere.
    const float3 offset = view_position - contour.fade_center.xyz;
    const float offset_length = length(offset);
    // abs(offset.z / offset_length) is the cosine between the offset and the view
    // axis: 1 straight behind the centre, 0 beside it.
    const float turned = max(1.0 - contour.fading * abs(offset.z / offset_length), 0.0);
    const float reach = min(contour.fade_center.w * offset_length, 1.0);
    // A smooth maximum of the two falls, not their sum, so either one alone can
    // fade the outline out -- which is how the game's tuning was done.
    output.strength =
        clamp(reach * (reach + turned - reach * turned) + contour.transparency, 0.0, 1.0);
    output.contour_index = g_contour_index;
    return output;
}
