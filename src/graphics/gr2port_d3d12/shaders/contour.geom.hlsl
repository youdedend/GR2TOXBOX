// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// contour.geom -> contour GS_6_6.
//
// Ported from src/graphics/gr2port/shaders/contour.geom: the game's G2_CH_Contour
// geometry program. Triangles that face the eye hand out the silhouette edges they
// share with triangles that do not, and each such edge becomes a ribbon around its
// two vertices, pushed outward on the screen and back in depth so the character
// covers the overlapping part.
//
// The stage translates directly: an adjacency triangle in, a four-vertex strip per
// silhouette edge out, GLSL's EmitVertex and EndPrimitive becoming HLSL's Append and
// RestartStrip. Shared capture keeps the game's raw OpenGL clip coordinates;
// depth conversion belongs here, not in the preceding intermediate VS.
//
// The ribbon carries the same four numbers to contour.frag: x along the edge (0 at
// one end, 1 at the other), y across the ribbon (1 at the outer side, 0 at the
// inner), z the stroke's phase and w its strength.

#include "common/arena.hlsli"

cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

cbuffer ContourRoot : register(b1) {
    uint2 g_contours_words;
    uint g_contour_index;
    uint3 g_contour_pad;
};
#define g_contours ArenaMake(g_contours_words.x, g_contours_words.y)

// One Contour is 288 bytes; the layout is computed in contour.vert.hlsl.
#define CONTOUR_BYTES 288u

// The fields this stage reads, at those offsets.
#define CONTOUR_INVERSE_Z_OFFSET 160u
#define CONTOUR_SIZE_AMPLITUDE_OFFSET 168u
#define CONTOUR_THRESHOLD_OFFSET 176u
#define CONTOUR_OFFSET_OFFSET 180u
#define CONTOUR_BIAS_OFFSET 184u
#define CONTOUR_FADING_SCALE_OFFSET 172u

struct VsOutput {
    float4 position : SV_Position;
    float2 direction : TEXCOORD0;
    float facing : TEXCOORD1;
    float phase : TEXCOORD2;
    float strength : TEXCOORD3;
    nointerpolation uint contour_index : TEXCOORD4;
};

struct GsOutput {
    float4 position : SV_Position;
    // x along the edge, y across the ribbon, z the phase, w the strength.
    float4 ribbon : TEXCOORD0;
    nointerpolation uint contour_index : TEXCOORD1;
};

// The ribbon's four vertices, as the geometry stage emits them. Kept as one struct
// and appended in order, because Direct3D has no per-vertex output registers.
GsOutput EmitRibbon(float2 xy, float z, float w, float4 ribbon, uint contour_index) {
    GsOutput output;
    output.position = float4(xy, 0.5 * (w - z), w);
    output.ribbon = ribbon;
    output.contour_index = contour_index;
    return output;
}

float3 Ndc(VsOutput input) {
    return input.position.xyz / input.position.w;
}

// The game's G2_CH_Contour geometry program.
[maxvertexcount(12)]
void main(triangleadj VsOutput edge[6], inout TriangleStream<GsOutput> stream) {
    const float3 p0 = Ndc(edge[0]);
    const float3 p2 = Ndc(edge[2]);
    const float3 p4 = Ndc(edge[4]);

    // Which way the triangle turns on the screen. A back-facing triangle is drawn by
    // its neighbour's pass, so only the front-facing one contributes.
    const float3 first = normalize(p2 - p0);
    const float3 second = normalize(p4 - p2);
    const float turn = first.x * second.y - first.y * second.x;
    if (turn < 0.0) {
        return;
    }

    // The table entry for every edge this triangle contributes: the geometry
    // shader's gl_DrawID equivalent, which is the contour index the vertex stage
    // passed through.
    const uint contour_index = edge[0].contour_index;
    const ArenaRef contours = ArenaAdd(g_contours, contour_index * CONTOUR_BYTES);

    const float2 inverse_z = ArenaLoadFloat2(ArenaAdd(contours, CONTOUR_INVERSE_Z_OFFSET), g_arena);
    const float size = ArenaLoadFloat(ArenaAdd(contours, CONTOUR_SIZE_AMPLITUDE_OFFSET), g_arena);
    const float2 bias = ArenaLoadFloat2(ArenaAdd(contours, CONTOUR_BIAS_OFFSET), g_arena);
    const float threshold = ArenaLoadFloat(ArenaAdd(contours, CONTOUR_THRESHOLD_OFFSET), g_arena);
    const float offset = ArenaLoadFloat(ArenaAdd(contours, CONTOUR_OFFSET_OFFSET), g_arena);
    const float fading = ArenaLoadFloat(ArenaAdd(contours, CONTOUR_FADING_SCALE_OFFSET), g_arena);

    // p1, p3 and p5 are the neighbours' vertices. The three edges are (0,1,2) with
    // 4 opposite, (2,3,4) with 0 opposite and (4,5,0) with 2 opposite.
    [unroll]
    for (int e = 0; e < 3; ++e) {
        const int a = e * 2;
        const int b = (e * 2 + 2) % 6;
        const int adjacent = a + 1;
        const int opposite = (a + 4) % 6;

        const float3 pa = Ndc(edge[a]);
        const float3 pb = Ndc(edge[b]);
        const float3 po = Ndc(edge[opposite]);
        const float3 pn = Ndc(edge[adjacent]);

        // An edge with no neighbour repeats the opposite vertex: there is no
        // silhouette to draw along it.
        if (all(pn == po)) {
            continue;
        }

        const float3 along = normalize(pb - pa);
        const float3 to_neighbour = normalize(pn - pa);
        // Whether the neighbour turns the same way as this triangle. The same way
        // means it is on this side of the silhouette, and the edge is interior.
        const float neighbour_turn = to_neighbour.x * along.y - to_neighbour.y * along.x;
        if (neighbour_turn > 0.0) {
            continue;
        }

        // The surface behind the edge sets the ribbon's width: the intercept and
        // slope of the game's 1/w-to-depth relation, evaluated where the opposite
        // vertex projects.
        const float2 to_opposite = po.xy - pa.xy;
        const float t = dot(to_opposite, along.xy);
        const float2 perpendicular = to_opposite - along.xy * t;
        const float distance_a = 1.0 / (inverse_z.x * pa.z + inverse_z.y);
        const float distance_b = 1.0 / (inverse_z.x * pb.z + inverse_z.y);
        const float distance_o = 1.0 / (inverse_z.x * po.z + inverse_z.y);
        const float slope = (distance_b - distance_a) / length(pb.xy - pa.xy);
        const float foot = distance_a + slope * t;
        const float pushed = foot + (distance_o - foot) * size / length(perpendicular);
        // A floor on how far back the ribbon goes, so an outline never sinks into
        // the surface it borders.
        const float behind = max(offset, pushed - slope * t - distance_a);
        const float pushed_a = distance_a + behind;
        const float pushed_b = distance_b + behind;

        // A sharp turn shows the full line where the surface faces the eye; a gentle
        // one fades out.
        const float sharp = threshold >= abs(turn - neighbour_turn) ? 1.0 : 0.0;
        const float amount_a =
            max(clamp(2.0 * fading * edge[a].facing * sharp, 0.0, 1.0) - bias.x, 0.0);
        const float amount_b =
            max(clamp(2.0 * fading * edge[b].facing * sharp, 0.0, 1.0) - bias.x, 0.0);
        const float strength_a = edge[a].strength * (1.0 - bias.y * amount_a);
        const float strength_b = edge[b].strength * (1.0 - bias.y * amount_b);

        const float4 clip_a = edge[a].position;
        const float4 clip_b = edge[b].position;
        // Outward on the screen, scaled by w: a constant width in pixels rather than
        // one that shrinks with distance.
        const float2 move_a = size * clip_a.w * edge[a].direction;
        const float2 move_b = size * clip_b.w * edge[b].direction;
        // The ribbon's depth, from the surface it was pushed behind.
        const float depth_a = (1.0 - inverse_z.y * pushed_a) / (inverse_z.x * pushed_a);
        const float depth_b = (1.0 - inverse_z.y * pushed_b) / (inverse_z.x * pushed_b);

        // Nearest first: the outer side of a's end, the inner side, then b's two.
        stream.Append(EmitRibbon(clip_a.xy - move_a, clip_a.z, clip_a.w,
                                 float4(0.0, 1.0, edge[a].phase, strength_a), contour_index));
        stream.Append(EmitRibbon(clip_a.xy + move_a, clip_a.w * depth_a, clip_a.w,
                                 float4(0.0, 0.0, edge[a].phase, strength_a), contour_index));
        stream.Append(EmitRibbon(clip_b.xy - move_b, clip_b.z, clip_b.w,
                                 float4(1.0, 1.0, edge[b].phase, strength_b), contour_index));
        stream.Append(EmitRibbon(clip_b.xy + move_b, clip_b.w * depth_b, clip_b.w,
                                 float4(1.0, 0.0, edge[b].phase, strength_b), contour_index));
        // Each edge is its own strip; without this the next edge's vertices would
        // join this ribbon as a fifth and sixth.
        stream.RestartStrip();
    }
}
