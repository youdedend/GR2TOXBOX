// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// contour.frag -> contour PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/contour.frag: the game's G2_CH_Contour
// pixel program. A line across the ribbon, its width and centre waving along the
// ribbon like a hand-drawn stroke, with soft edges.
//
// This is the other half of the outline the geometry stage expands. It takes the
// ribbon coordinate and the phase the earlier stages produced and decides how much
// ink is at this pixel: the stroke is a band whose centre wobbles, so the width
// changes along the silhouette the way a brush line does.
//
// The curve is three cosines of the ribbon's position, at frequencies in the tens
// and hundreds of thousands -- not a typo, and not something to "correct": the
// phase was scaled down to ~0.001 in the vertex stage so these multipliers bring it
// back to a visible wavelength. Reusing the shader's numbers verbatim is the only
// way the outline keeps the game's look.

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

// The table entry's size and the offsets of the fields this stage reads. The full
// layout is written out in contour.vert.hlsl; these are the same numbers.
#define CONTOUR_BYTES 288u

#define CONTOUR_COLOR_OFFSET 192u
#define CONTOUR_SIZE_OFFSET 208u
#define CONTOUR_LINE_AMPLITUDE_OFFSET 212u
#define CONTOUR_LINE_CYCLE_OFFSET 216u
#define CONTOUR_CURVE_AMPLITUDE_OFFSET 220u
#define CONTOUR_CURVE_CYCLE_OFFSET 224u
#define CONTOUR_SOFTNESS_OFFSET 228u

struct PsInput {
    float4 position : SV_Position;
    float2 direction : TEXCOORD0;
    float facing : TEXCOORD1;
    float phase : TEXCOORD2;
    float strength : TEXCOORD3;
    nointerpolation uint contour_index : TEXCOORD4;
};

// From contour.geom: x along the edge, y across the ribbon from its outer side, z
// the line's phase, w its strength.
struct GsInput {
    float4 position : SV_Position;
    float4 ribbon : TEXCOORD0;
    nointerpolation uint contour_index : TEXCOORD1;
};

float4 main(GsInput input) : SV_Target {
    const ArenaRef base = ArenaAdd(g_contours, input.contour_index * CONTOUR_BYTES);

    const float4 color = ArenaLoadFloat4(ArenaAdd(base, CONTOUR_COLOR_OFFSET), g_arena);
    const float width = ArenaLoadFloat(ArenaAdd(base, CONTOUR_SIZE_OFFSET), g_arena);
    const float amplitude = ArenaLoadFloat(ArenaAdd(base, CONTOUR_LINE_AMPLITUDE_OFFSET), g_arena);
    const float line_cycle = ArenaLoadFloat(ArenaAdd(base, CONTOUR_LINE_CYCLE_OFFSET), g_arena);
    const float curve_amplitude =
        ArenaLoadFloat(ArenaAdd(base, CONTOUR_CURVE_AMPLITUDE_OFFSET), g_arena);
    const float curve_cycle = ArenaLoadFloat(ArenaAdd(base, CONTOUR_CURVE_CYCLE_OFFSET), g_arena);
    const float softness = ArenaLoadFloat(ArenaAdd(base, CONTOUR_SOFTNESS_OFFSET), g_arena);

    const float along = input.ribbon.z;

    // The stroke's centre, as a sum of two cosines of different wavelengths: one
    // coarse enough to bend the whole silhouette, one fine enough to give it a
    // texture. The first is scaled by how much of the width the amplitude is worth,
    // so a fat line curves proportionally less.
    const float curve = curve_amplitude / (curve_amplitude + 0.5 * width) *
                        (cos(curve_cycle * (30000.0 * along + 9000.0)) +
                         cos(curve_cycle * along * 100000.0));

    // The position across the ribbon, -1..1.
    const float across = 2.0 * input.ribbon.y - 1.0;
    // A modulation of the stroke's own width along the ribbon.
    const float wave = cos(line_cycle * along * 160000.0);

    // The stroke's edge at this point: the ribbon's position, displaced by the wave
    // and the curve, and clamped so a large amplitude cannot fold the line through
    // itself.
    const float stroke = across + 0.5 * amplitude * (across - 0.5 * curve) * (1.0 + wave);

    // The width the curve steals from the line, as a fraction: a curved stroke is a
    // thinner one, which is what makes it read as a stroke rather than a band.
    const float full_width = width / (width + 2.0 * curve_amplitude);
    const float from_center = abs(0.5 * curve - stroke) * 2.0 / full_width;

    // Soft edges: the alpha ramps down over `softness` of the line's half-width.
    // The max with 0 is upstream's, and it matters -- a negative alpha would be
    // clamped by the blend to 0 anyway, but ImGui's and the UI's blending does not
    // clamp, and this shader draws into the same target.
    const float alpha =
        max(color.a * input.ribbon.w * min((1.0 - from_center) / softness, 1.0), 0.0);
    // Below a quarter of a percent the pixel is invisible; dropping it early saves
    // the write and, more usefully, keeps the depth buffer clean for what is
    // behind the silhouette.
    if (alpha < 0.004f) {
        discard;
    }
    return float4(color.rgb, alpha);
}
