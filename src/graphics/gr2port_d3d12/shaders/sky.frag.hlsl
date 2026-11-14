// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// sky.frag -> sky PS_6_6.
//
// Ported from src/graphics/gr2port/shaders/sky.frag: the game's sky programs, the
// cube maps, the sun's disc, then two layers of cloud above the horizon.
//
// The GLSL aliases one binding as both `texture2D[]` and `textureCube[]`: a
// handle's kind decides which declaration samples it. D3D12 has no aliasing of
// that sort -- an SRV is one view -- so the backend gives the sky pass two
// handles for the two cube maps and one for the cloud texture, all indices into
// the same bindless heap. The heap holds a TextureCube SRV for the cube maps
// because the backend creates those views when it uploads the game's cube
// textures; see renderer.cpp.

#include "common/scene.hlsli"
#include "common/texel.hlsli"

// Sky, field for field with the part of scene.glsl's Sky block this stage reads.
//
//   0   float4x4 inverse_view_projection   64
//  64   float4x4 transforms[2]            128
// 192   float4x4 view_to_astro            64 (not used here)
// 256   float4   sun_direction
// 272   float4   sun_color
// 288   float    sun_size
// 292   float    sun_outer
// 296   float    blend
// 300   uint     cubes[2]
// 308   uint     cloud
// 312   float2   cloud_fade
// 320   float4   cloud_layers[2]          32
// 352   float4   cloud_color
// = 368 bytes.
#define SKY_CUBE_TRANSFORMS 64u
#define SKY_SUN_DIRECTION 256u
#define SKY_SUN_COLOR 272u
#define SKY_SUN_SIZE 288u
#define SKY_SUN_OUTER 292u
#define SKY_BLEND 296u
#define SKY_CUBES 300u
#define SKY_CLOUD 308u
#define SKY_CLOUD_FADE 312u
#define SKY_CLOUD_LAYERS 320u
#define SKY_CLOUD_COLOR 352u

struct Sky {
    float4x4 inverse_view_projection;
    float4x4 transforms[2];
    float4 sun_direction;
    float4 sun_color;
    float sun_size;
    float sun_outer;
    float blend;
    uint cubes[2];
    uint cloud;
    float2 cloud_fade;
    float4 cloud_layers[2];
    float4 cloud_color;
};

Sky LoadSky(ArenaRef ref, ArenaTable table) {
    ByteAddressBuffer buffer = ArenaBuffer(ref, table);
    Sky sky;
    sky.inverse_view_projection = ArenaLoadMatrix(ArenaAdd(ref, 0u), table);
    [unroll] for (uint i = 0u; i < 2u; ++i) {
        sky.transforms[i] = ArenaLoadMatrix(ArenaAdd(ref, SKY_CUBE_TRANSFORMS + i * 64u), table);
    }
    sky.sun_direction = asfloat(buffer.Load4(ref.offset + SKY_SUN_DIRECTION));
    sky.sun_color = asfloat(buffer.Load4(ref.offset + SKY_SUN_COLOR));
    sky.sun_size = asfloat(buffer.Load(ref.offset + SKY_SUN_SIZE));
    sky.sun_outer = asfloat(buffer.Load(ref.offset + SKY_SUN_OUTER));
    sky.blend = asfloat(buffer.Load(ref.offset + SKY_BLEND));
    sky.cubes[0] = buffer.Load(ref.offset + SKY_CUBES);
    sky.cubes[1] = buffer.Load(ref.offset + SKY_CUBES + 4u);
    sky.cloud = buffer.Load(ref.offset + SKY_CLOUD);
    sky.cloud_fade = asfloat(buffer.Load2(ref.offset + SKY_CLOUD_FADE));
    [unroll] for (uint i = 0u; i < 2u; ++i) {
        sky.cloud_layers[i] = asfloat(buffer.Load4(ref.offset + SKY_CLOUD_LAYERS + i * 16u));
    }
    sky.cloud_color = asfloat(buffer.Load4(ref.offset + SKY_CLOUD_COLOR));
    return sky;
}

cbuffer SkyConstants : register(b0) {
    ArenaRef g_sky;
    uint g_pad0;
    uint g_pad1;
    uint g_pad2;
};

cbuffer ArenaConstants : register(b1) {
    ArenaTable g_arena;
};

struct PsInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// A cube-map sample at the top mip, through the clamping sampler. GLSL's
// textureLod(samplerCube(cubes[i], clamp_sampler), dir, 0.0).
float3 CubeMap(Sky sky, uint index, float3 ray) {
    TextureCube cube = ResourceDescriptorHeap[NonUniformResourceIndex(sky.cubes[index])];
    SamplerState sampler = SamplerDescriptorHeap[SMP_LINEAR_CLAMP];
    // GLSL's mat3(m) * ray is the upper-left 3x3 of the matrix in column-major
    // order, which is mul(m3, ray) with HLSL's column-vector convention.
    const float4x4 transform = sky.transforms[index];
    const float3x3 frame = float3x3(transform[0].xyz, transform[1].xyz, transform[2].xyz);
    return cube.SampleLevel(sampler, mul(frame, ray), 0.0).rgb;
}

// A layer's cloud density at a point of the plane the clouds lie in, fading with
// distance.
float CloudLayer(Sky sky, float4 layer, float2 plane) {
    const float2 scaled = layer.x * plane;
    const float density = SampleTex(sky.cloud, (layer.yz + scaled) * layer.w).r;
    const float fade = sky.cloud_fade.y * (length(scaled) - sky.cloud_fade.x);
    return density * (1.0 - saturate(fade));
}

float4 main(PsInput input) : SV_Target {
    const Sky sky = LoadSky(g_sky, g_arena);
    const float2 position = 0.5 * input.uv + 0.5;

    // The GLSL's `vec4(position, 1.0, 1.0)` puts a point at the far plane in the
    // direction of the pixel, which is what the direction of the unprojected
    // point is once normalised.
    const float3 ray = normalize(mul(sky.inverse_view_projection, float4(position, 1.0, 1.0)).xyz);

    float3 result = CubeMap(sky, 0u, ray);
    if (sky.blend > 0.0) {
        result = lerp(result, CubeMap(sky, 1u, ray), sky.blend);
    }
    // The sun's disc: a smooth falloff from its radius to its outer radius.
    const float angle = acos(max(dot(sky.sun_direction.xyz, ray), 0.0));
    const float sun = 1.0 - min(max(angle - sky.sun_size, 0.0) * sky.sun_outer, 1.0);
    const float sun_squared = sun * sun;
    result = lerp(result, sky.sun_color.rgb, sun_squared * sun_squared);

    if (ray.y > 0.0 && sky.cloud_color.a > 0.0) {
        const float2 plane = ray.xz / ray.y;
        const float density = max(CloudLayer(sky, sky.cloud_layers[0], plane),
                                  CloudLayer(sky, sky.cloud_layers[1], plane));
        result = lerp(result, sky.cloud_color.rgb, min(density, 1.0) * sky.cloud_color.a);
    }
    return float4(result, 1.0);
}
