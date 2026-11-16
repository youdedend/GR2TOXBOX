// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// lambert.frag -> lambert PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/lambert.frag: the game's opaque/Lambert
// pixel program. The colour map times the material's colour scale, lit by a
// half-Lambert term of the key light in the model's space, attenuated by the key
// light's shadow, with a floor of 0.2 so that nothing in shadow goes fully black.
//
// The two shadow lookups are comparison samples. GLSL's sampler2DArrayShadow and
// sampler2DShadow become Texture2DArray<float> and Texture2D<float> read through
// SampleCmpLevelZero against the comparison sampler, which is what the GLSL
// sampler types do implicitly -- the same translation fog_shadow.comp.hlsl uses, so
// the two agree about how the maps are read.

#include "common/scene.hlsli"
#include "common/texel.hlsli"

// As in lambert.vert.hlsl, and must stay in step with it.
#define LAMBERT_CONSTANTS_BYTES 656u

struct LambertConstants {
    float4 light_direction;
    float4 color_scale;
    float4x4 shadow_projection[3];
};

LambertConstants LoadLambertConstantsAt(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    LambertConstants constants;
    constants.light_direction = asfloat(buffer.Load4(ref.offset + 0));
    constants.color_scale = asfloat(buffer.Load4(ref.offset + 16));
    [unroll] for (uint cascade = 0u; cascade < 3u; ++cascade) {
        constants.shadow_projection[cascade] =
            ArenaLoadMatrix(ArenaAdd(ref, 32u + cascade * 64u), table);
    }
    return constants;
}

// b0 is the arena table and b1 the scene constants, matching lambert.vert's bindings
// and the root signature (pipeline.h).
cbuffer ArenaConstants : register(b0) {
    ArenaTable g_arena;
};

cbuffer LambertSceneRoot : register(b1) {
    uint2 g_scene_draws_words;
    uint2 g_scene_characters_words;
    uint g_base_instance;
    // One pad word: the block is two ArenaRefs and this uint, which is exactly
    // RootConstants::DrawBytes (24 bytes, 6 words) in pipeline.h. The root
    // signature binds that many 32-bit values at b1, so a longer block here would
    // bind fewer than it declares and read whatever followed.
    uint g_pad;
};
#define g_scene_draws ArenaMake(g_scene_draws_words.x, g_scene_draws_words.y)
#define g_scene_characters ArenaMake(g_scene_characters_words.x, g_scene_characters_words.y)

cbuffer LambertTextures : register(b2) {
    // The two nearer cascades (Texture2DArray depth) and the last cascade
    // (Texture2D depth). Handles into the bindless heap, as everywhere else.
    uint g_shadow_map;
    uint g_last_shadow_map;
};

// lambert.vert's outputs, at the same TEXCOORD slots.
struct PsInput {
    float4 position : SV_Position;
    float4 shadow_clip0 : TEXCOORD0;
    float4 shadow_clip1 : TEXCOORD1;
    float4 shadow_clip2 : TEXCOORD2;
    float3 model_normal : TEXCOORD3;
    float2 texcoord : TEXCOORD4;
    nointerpolation uint draw_index : TEXCOORD5;
};

// How much of the key light reaches the point: one comparison sample in the first
// of the two nearer cascades whose map holds it, else in the last cascade -- the
// game's own rule, kept as written rather than replaced with a distance test.
// The two examined cascades are written out rather than looped over an array of
// the inputs. Dynamic indexing of a local array is the one construct in the GLSL
// that does not survive translation reliably: dxc has to see the loop trip count
// at compile time to fold the index away, and if it does not, the array is demoted
// to scratch memory and the comparison sampler reads a register it cannot index.
// Two explicit blocks cost nothing and cannot be miscompiled.
float KeyShadow(PsInput input) {
    Texture2DArray<float> cascades = DescriptorDepthArray(g_shadow_map);
    SamplerComparisonState sampler = DescriptorComparisonSampler(SMP_COMPARISON);

    const float3 first = input.shadow_clip0.xyz / input.shadow_clip0.w;
    if (min(first.x, first.y) >= 0.0 && max(first.x, first.y) <= 1.0) {
        return cascades.SampleCmpLevelZero(sampler, float3(first.xy, 0.0), first.z);
    }
    const float3 second = input.shadow_clip1.xyz / input.shadow_clip1.w;
    if (min(second.x, second.y) >= 0.0 && max(second.x, second.y) <= 1.0) {
        return cascades.SampleCmpLevelZero(sampler, float3(second.xy, 1.0), second.z);
    }
    // Else the last cascade, which is a single 2D map rather than a slice of the
    // array -- the game keeps it separate because it covers the whole far field.
    Texture2D<float> last = DescriptorDepth(g_last_shadow_map);
    const float3 third = input.shadow_clip2.xyz / input.shadow_clip2.w;
    return last.SampleCmpLevelZero(sampler, third.xy, third.z);
}

float4 main(PsInput input) : SV_Target0 {
    const Draw draw = LoadDrawAt(ArenaAdd(g_scene_draws, input.draw_index * DRAW_BYTES), g_arena);
    const uint index = draw.material - 1u;
    const LambertConstants constants = LoadLambertConstantsAt(
        ArenaAdd(g_scene_characters, index * LAMBERT_CONSTANTS_BYTES), g_arena);

    const float3 albedo = SampleTex(draw.texture, input.texcoord).rgb;
    // Half-Lambert: 0.5 - 0.5 * dot(n, l), which is what makes the light wrap past
    // the terminator instead of clamping at it.
    const float half_lambert =
        0.5 - 0.5 * dot(normalize(input.model_normal), constants.light_direction.xyz);
    const float4 color =
        float4(albedo * constants.color_scale.rgb * (0.7 * half_lambert * KeyShadow(input) + 0.2),
               1.0);
    return color;
}
