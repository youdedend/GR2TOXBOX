// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// world.vert -> world VS_6_4.
//
// Direct port of src/graphics/gr2port/shaders/world.vert: the game's G2_BG_Main
// vertex programs without their velocity — the view-space frame, the parallax
// direction in the frame's tangent plane, and the colour the model's height
// blends to.
//
// Three changes, all mechanical and marked below:
//   1. The push-constant `scene_draws` / `scene_materials` buffer references are
//      root constants holding ArenaRefs (common/scene.hlsli).
//   2. gl_BaseInstance becomes the `base_instance` root constant; gl_VertexIndex
//      replaces GLSL's implicit vertex index.
//   3. Shared capture stores raw OpenGL matrices: convert Z in the shader.

#include "common/scene.hlsli"

// Root signature constants. b0 holds the arena SRV table, b1 the scene
// constants (draw table ref, material table ref, base instance).
cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

cbuffer WorldRoot : register(b1) {
    uint2 scene_draws_words;
    uint2 scene_materials_words;
    uint2 scene_layers_words;
    uint base_instance; uint root_padding;
};
#define scene_draws ArenaMake(scene_draws_words.x, scene_draws_words.y)
#define scene_materials ArenaMake(scene_materials_words.x, scene_materials_words.y)
#define scene_layers ArenaMake(scene_layers_words.x, scene_layers_words.y)
static SceneConstants g_scene;

// Attributes, at the same locations as the GLSL so the pixel shaders port
// without renumbering.
struct VsOutput {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    // The tangent, and in w the sign of the bitangent.
    float4 tangent : TEXCOORD1;
    // Texture coordinates, and in zw the parallax direction.
    float4 texcoord : TEXCOORD2;
    float4 vertex_color : TEXCOORD3;
    float3 gradation : TEXCOORD4;
    nointerpolation uint draw_index : TEXCOORD5;
};

VsOutput main(uint vertex_index : SV_VertexID) {
    g_scene.draws = scene_draws;
    g_scene.materials = scene_materials;
    g_scene.base_instance = base_instance;
    VsOutput output;

    // gl_BaseInstance: the draw index for this instance.
    const uint draw_index = g_scene.base_instance;
    output.draw_index = draw_index;

    const Draw draw = LoadDrawAt(ArenaAdd(g_scene.draws, draw_index * DRAW_BYTES), g_arena);

    // Vertex pulling over the Unified Bindless Arena: `vertices` is an ArenaRef
    // where the GLSL had a uint64_t device address.
    const float3 unskinned =
        LoadPosition(draw.vertices, draw.stride, draw.position, vertex_index, g_arena);
    const float3 model =
        draw.bones != 0u
            ? SkinPosition(unskinned, draw.skin, draw.palette, draw.bones, vertex_index, g_arena)
            : unskinned;

    const float4x4 world_view = draw.world_view;
    const float3 view_position = mul(world_view, float4(model, 1.0)).xyz;

    float4 clip = mul(draw.world_view_projection, float4(model, 1.0));
    clip.z = 0.5 * (clip.w - clip.z);
    output.position = clip;

    const float4 transform = draw.texcoord_transform;
    const float2 uv =
        LoadTexcoord(draw.vertices, draw.stride, draw.texcoord, vertex_index, g_arena);
    const uint material_index = draw.material;
    output.texcoord = float4(material_index != 0u ? uv : transform.xy + transform.zw * uv, 0.0,
                             0.0);

    const uint normal_element = draw.normal;
    const uint tangent_element = draw.tangent;
    output.normal = float3(0.0, 0.0, 0.0);
    output.tangent = float4(0.0, 0.0, 0.0, 0.0);

    if ((normal_element >> 8) != 0u) {
        float3 n = LoadNormal(draw.vertices, draw.stride, normal_element, vertex_index, g_arena);
        // The HLSL equivalent of GLSL's mat3(world_view) * n: the upper-left 3x3.
        float3x3 view_frame = float3x3(world_view[0].xyz, world_view[1].xyz, world_view[2].xyz);
        n = draw.bones != 0u
                ? SkinDirection(n, draw.skin, draw.palette, draw.bones, vertex_index, g_arena)
                : n;
        output.normal = normalize(mul(view_frame, n));
    }

    if ((normal_element >> 8) != 0u && (tangent_element >> 8) != 0u) {
        const float4 t =
            LoadTangent(draw.vertices, draw.stride, tangent_element, vertex_index, g_arena);
        float3x3 view_frame = float3x3(world_view[0].xyz, world_view[1].xyz, world_view[2].xyz);
        const float3 turned = draw.bones != 0u
                                  ? SkinDirection(t.xyz, draw.skin, draw.palette, draw.bones,
                                                  vertex_index, g_arena)
                                  : t.xyz;
        output.tangent = float4(normalize(mul(view_frame, turned)), t.w);
        const float3 eye = normalize(view_position);
        const float facing = 1.0 / dot(output.normal, eye);
        output.texcoord.zw =
            0.1 * facing *
            float2(dot(output.tangent.xyz, eye),
                   t.w * dot(eye, cross(output.normal, output.tangent.xyz)));
    }

    const uint color_element = draw.vertex_color;
    output.vertex_color =
        (color_element >> 8) != 0u
            ? LoadVertexColor(draw.vertices, draw.stride, color_element, vertex_index, g_arena)
            : float4(1.0, 1.0, 1.0, 1.0);

    output.gradation = float3(1.0, 1.0, 1.0);
    if (material_index != 0u) {
        const uint index = material_index - 1u;
        const Material material = LoadMaterial(g_scene, index, g_arena);
        const float4 low = material.gradation_low;
        const float4 high = material.gradation_high;
        output.gradation =
            lerp(low.rgb, high.rgb, saturate(high.w * (unskinned.y - low.w)));
        // A gem has no gradation: its pixels refract the view ray instead.
        if ((material.flags & MaterialGem) != 0u) {
            output.gradation = view_position;
        }
    }

    return output;
}
