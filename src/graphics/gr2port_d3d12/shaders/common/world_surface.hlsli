// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch2_scene.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer WorldRoot : register(b1) {
    uint2 scene_draws_words;
    uint2 scene_materials_words;
    uint2 scene_layers_words;
    uint base_instance; uint root_padding;
};
#define scene_draws ArenaMake(scene_draws_words.x, scene_draws_words.y)
#define scene_materials ArenaMake(scene_materials_words.x, scene_materials_words.y)
#define scene_layers ArenaMake(scene_layers_words.x, scene_layers_words.y)
struct WorldIO {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float4 tangent : TEXCOORD1;
    float4 texcoord : TEXCOORD2;
    float4 vertex_color : TEXCOORD3;
    float3 gradation : TEXCOORD4;
    nointerpolation uint draw_index : TEXCOORD5;
};
struct WorldTargets { float4 albedo : SV_Target0; float2 normal : SV_Target1; float4 surface : SV_Target2; };
static float4 pixel_position;
static float3 normal, gradation;
static float4 tangent, texcoord, vertex_color;
static uint draw_index;
static bool front_facing;
static float4 g_albedo, g_surface;
static float2 g_normal;
float4 Sample(uint handle, float2 uv) { return Batch2Sample(handle, uv); }
bool WorldScreenDoor(float v) { return ScreenDoorShut(v, pixel_position.xy, pixel_position.z); }
Material WorldMaterial(uint index) {
    SceneConstants constants = (SceneConstants)0;
    constants.materials = scene_materials;
    return LoadMaterial(constants, index, g_arena);
}
// Half a hard light of the gradation over the colour.
float3 Gradate(float3 color) {
    const float3 lighter = 1.0 - 2.0 * (1.0 - gradation) * (1.0 - color);
    const float3 darker = 2.0 * gradation * color;
    return lerp(color, lerp(darker, lighter, step(float3(0.5, 0.5, 0.5), color)), 0.5);
}

// The normal n turned by a tangent-space normal, in the frame of the vertex's tangent.
float3 TurnedNormal(float3 n, float3 tangent_normal) {
    if (all(tangent.xyz == float3(0.0, 0.0, 0.0))) {
        return n;
    }
    const float3 t = normalize(tangent.xyz);
    const float3 b = cross(n, t) * sign(tangent.w);
    const float3 surface_normal = normalize(tangent_normal);
    return normalize(t * surface_normal.x + b * surface_normal.y + n * surface_normal.z);
}

// A layer's scaled normal in tangent space. A layer without a normal map is flat.
float3 LayerNormal(Batch2LayerMaterial table, uint index, int layer, float2 uv) {
    const uint map = table.normal_map[layer];
    const float3 direction =
        map != 0u
            ? 2.0 * Sample(map, Transformed(table.normal_transform[layer], uv)).rgb -
                  1.0
            : float3(0.0, 0.0, 1.0);
    return table.normal_scale[layer] * direction;
}

float4 LayerSurface(Batch2LayerMaterial table, uint index, int layer) {
    return float4(table.roughness[layer], table.metallic[layer],
                table.translucency[layer], table.emissive[layer]);
}

// The game's G2_BG_Layer pixel program without its velocity: four layers mixed from the last to the
// first, each by a channel of the vertex colour and its colour's alpha. A layer with its blend flag
// multiplies the layers below by twice its colour and adds its normal to theirs.
void WriteLayers(uint index, uint hatching, float3 n) {
    Batch2LayerMaterial table = LoadBatch2LayerMaterial(ArenaAdd(scene_layers, index * 352u), g_arena);
#ifdef ALPHA_TEST
    if (WorldScreenDoor(table.punch_through)) {
        discard;
    }
#endif
    float2 uv = texcoord.xy;
    const float parallax =
        lerp(lerp(lerp(table.parallax[3], table.parallax[2],
                    vertex_color.z),
                table.parallax[1], vertex_color.y),
            table.parallax[0], vertex_color.x);
    const uint height_map = table.normal_map[0];
    if (height_map != 0u && any(tangent.xyz != float3(0.0, 0.0, 0.0)) && parallax != 0.0) {
        const float4 transform = table.normal_transform[0];
        uv = (Parallax(height_map, Transformed(transform, uv), texcoord.zw * parallax, n.z) -
              transform.xy) /
             transform.zw;
    }
    float3 albedo =
        Sample(table.color_map[3],
               Transformed(table.color_transform[3], uv))
            .rgb *
        table.color_scale[3].rgb;
    float3 tangent_normal = LayerNormal(table, index, 3, uv);
    float4 surface = LayerSurface(table, index, 3);
    for (int layer = 2; layer >= 0; layer--) {
        const float4 color = Sample(table.color_map[layer],
                                  Transformed(table.color_transform[layer], uv));
        const float3 scaled = color.rgb * table.color_scale[layer].rgb;
        const float3 layer_normal = LayerNormal(table, index, layer, uv);
        const bool blends = table.blend[layer] != 0u;
        const float weight = vertex_color[layer] * color.a;
        albedo = lerp(albedo, blends ? 2.0 * albedo * scaled : scaled, weight);
        tangent_normal =
            lerp(tangent_normal, blends ? tangent_normal + layer_normal : layer_normal, weight);
        surface = lerp(surface, LayerSurface(table, index, layer), weight);
    }
    const float2 repeat =
        float2(table.hatching_repeat_u, table.hatching_repeat_v);
    g_albedo = float4(Gradate(albedo), Sample(hatching, uv * repeat).r);
    g_normal = PackNormal(TurnedNormal(n, tangent_normal));
    g_surface = surface;
}

// The game's G2_BG_Gem pixel program without its velocity: the colour map shows what the view ray
// meets after it refracts through the surface, over the gem's own colour by the map's alpha.
void WriteGem(uint index, float3 n) {
    Batch2GemMaterial table = LoadBatch2GemMaterial(ArenaAdd(scene_layers, index * 352u), g_arena);
#ifdef ALPHA_TEST
    if (WorldScreenDoor(table.punch_through)) {
        discard;
    }
#endif
    const uint normal_map = table.normal_map;
    float3 tangent_normal = float3(0.0, 0.0, 1.0);
    if (normal_map != 0u) {
        const float3 map = Sample(normal_map, texcoord.xy).rgb;
        tangent_normal = float3(table.normal_scale * (2.0 * map.xy - 1.0), map.z);
    }
    const float3 turned = TurnedNormal(n, tangent_normal);
    const float3 ray = refract(normalize(gradation), turned, table.refraction);
    const float2 place = table.projection * ray.xy / -ray.z + table.offset;
    const float2 lookup = float2(0.5 + 0.5 * table.scale.x * place.x,
                             0.5 - 0.5 * table.scale.y * place.y);
    const float4 color = Sample(table.color_map, lookup);
    g_albedo = float4(lerp(table.color.rgb, table.color_scale.rgb * color.rgb,
                        color.a),
                    0.0);
    g_normal = PackNormal(turned);
    g_surface = float4(table.roughness, table.metallic,
                     table.translucency, table.emissive);
}

// The game's G2_BG_Main pixel programs without their velocity, one program for all their variants.
void WorldPixel() {
    const float facing_sign = front_facing ? 1.0 : -1.0;
    const uint material_index = LoadDrawAt(ArenaAdd(scene_draws, draw_index * DRAW_BYTES), g_arena).material;
    const uint color_handle = LoadDrawAt(ArenaAdd(scene_draws, draw_index * DRAW_BYTES), g_arena).texture;
    const float4 color_scale = LoadDrawAt(ArenaAdd(scene_draws, draw_index * DRAW_BYTES), g_arena).color_scale;
    const float length_squared = dot(normal, normal);
    // A mesh without normals faces the eye.
    float3 n = length_squared > 0.0 ? normal * rsqrt(length_squared) * facing_sign
                                  : float3(0.0, 0.0, 1.0);
    if (material_index == 0u) {
#ifdef ALPHA_TEST
        discard;
#endif
        g_albedo = float4(color_handle != 0u ? Sample(color_handle, texcoord.xy).rgb * color_scale.rgb
                                           : float3(0.0, 0.0, 0.0),
                        1.0);
        g_normal = PackNormal(n);
        g_surface = float4(1.0, 0.0, 0.0, 0.0);
        return;
    }
    const uint index = material_index - 1u;
    Material table = WorldMaterial(index);
    if ((table.flags & MaterialLayers) != 0u) {
        WriteLayers(table.layer_material, table.hatching, n);
        return;
    }
    if ((table.flags & MaterialGem) != 0u) {
        WriteGem(table.layer_material, n);
        return;
    }
    const bool layered = (table.flags & MaterialLayered) != 0u;
    const float weight = layered ? vertex_color.a : 1.0;
    const uint normal_map = table.normal_map;
    const float4 normal_transform = table.normal_transform;
    const float2 start = Transformed(normal_transform, texcoord.xy);

    float2 uv = texcoord.xy;
    const float parallax = lerp(table.layered_parallax,
                               table.parallax, weight);
    if (normal_map != 0u && any(tangent.xyz != float3(0.0, 0.0, 0.0)) && parallax != 0.0) {
        uv = (Parallax(normal_map, start, texcoord.zw * parallax, n.z) - normal_transform.xy) /
             normal_transform.zw;
    }

    const float4 color =
        Sample(color_handle, Transformed(table.color_transform, uv));
#ifdef ALPHA_TEST
    if (color.a < color_scale.w || WorldScreenDoor(table.punch_through)) {
        discard;
    }
#endif

    const float4 mask =
        Sample(table.mask, Transformed(table.mask_transform, uv));
    float3 albedo = color.rgb * color_scale.rgb;
    float4 surface =
        mask * float4(table.roughness, table.metallic,
                    table.translucency, table.emissive);
    float3 tangent_normal = float3(0.0, 0.0, 1.0);
    if (normal_map != 0u) {
        tangent_normal = 2.0 * Sample(normal_map, Transformed(normal_transform, uv)).rgb - 1.0;
    }
    if (layered) {
        const float3 layer =
            Sample(table.layered_color,
                   Transformed(table.layered_color_transform, uv))
                .rgb *
            table.layered_color_scale.rgb;
        albedo = lerp(layer, albedo, weight);
        surface = lerp(float4(table.layered_roughness,
                           table.layered_metallic,
                           table.layered_translucency,
                           table.layered_emissive),
                      surface, weight);
        if (table.layered_normal != 0u) {
            const float3 layer_normal =
                2.0 *
                    Sample(table.layered_normal,
                           Transformed(table.layered_normal_transform, uv))
                        .rgb -
                1.0;
            tangent_normal = lerp(layer_normal, tangent_normal, weight);
        }
        albedo *= vertex_color.rgb;
    }
    if ((table.flags & MaterialGradation) != 0u) {
        albedo = Gradate(albedo);
    }
    const float repeat = table.hatching_repeat;
    g_albedo = float4(albedo, Sample(table.hatching, uv * repeat).r);
    g_normal = PackNormal(TurnedNormal(n, tangent_normal));
    g_surface = surface;
}

WorldTargets main(WorldIO input, bool is_front : SV_IsFrontFace) {
    pixel_position=input.position; normal=input.normal; tangent=input.tangent;
    texcoord=input.texcoord; vertex_color=input.vertex_color; gradation=input.gradation;
    draw_index=input.draw_index; front_facing=is_front; WorldPixel();
    WorldTargets o; o.albedo=g_albedo; o.normal=g_normal; o.surface=g_surface; return o;
}
