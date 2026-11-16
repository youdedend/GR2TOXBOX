# Shader batch 2: translation and binding contract

This batch adds **14 engine stages**, bringing source coverage to **53/184**,
plus the separate **1/1 GCN detile kernel** (54 compiled stages total).
These are shader translations, not a claim of a functioning Xbox frame graph or
GPU equivalence. The renderer must still schedule passes and bind their resources.

## Added stages

- `world.frag.hlsl`, `world_alpha.frag.hlsl`: ordinary, layered, four-layer and
  gem world G-buffers; alpha/screen-door rejection, parallax and material masks.
- `character_depth.frag.hlsl`: character alpha-step/screen-door depth rejection.
- `vision.frag.hlsl`: packed movie YUV, damage and scrolling overlay.
- `particle_half.frag.hlsl`: conservative half-depth reduction / premultiplied composite.
- `motion_blur.vert.hlsl`: full-screen triangle.
- `motion_blur_tile.comp.hlsl`, `motion_blur_neighbor.comp.hlsl`: shared-memory
  atomic fastest-vector reductions, with barriers reached by every group lane.
- `velocity.comp.hlsl`: world camera motion and separate character upscaler motion.
- `occlusion_depth.comp.hlsl`, `occlusion.comp.hlsl`: depth mips and temporal SSAO.
- `particle_active.comp.hlsl`: settling/spawn classification and active-list atomics.
- `ui_text.geom.hlsl`, `ui_text_flat.geom.hlsl`: aspect-correct anchored HUD text.

## ABI / stages

`b0` remains the 20-byte arena table; `b1` is at most 64 bytes; `b2` remains
four texture/buffer handles at most. No descriptor tables were added: the same
12 tables in spaces 100–111 and 37-DWORD root signature are used. All stages use
SM 6.4 and HLSL 2021, including GS. The generated CMake manifest already supports
GS and now includes every added stage.

`shader_batch_layout.h` spells out host root packets with size/offset assertions.
World VS/PS now share a 32-byte root containing draws/materials/layers references
and base instance. The two character auxiliary pixel roots are the first 16 bytes
of the existing 64-byte CharacterRoot, so they can pair with character.vert.
World and character draw indices use `nointerpolation` with matching semantics.
UiText's draw index is TEXCOORD15, attributes are TEXCOORD0/1.

Layer/Gem scene records retain the upstream 352-byte stride, including tightly
packed four-byte scalar arrays and maps at bytes 320/336. UI draws retain their
160-byte stride, flags at 152 and constants reference at 8. The scene uploader must
convert embedded guest addresses into the established eight-byte arena pairs.
Material flags now match upstream: Layered=1, Gradation=2, Layers=4, Gem=8.

## Compute outputs without another root table

There is no typed 2D UAV table in the existing root signature. The new image
compute passes therefore store **RGBA32_FLOAT placed footprints in raw UAV
buffers**, using the existing space108 table. Their roots include `OutputSize`.
`Rgba32Scratch()` rejects zero or >16384 dimensions and calculates row pitch
`align_up(width * 16, 256)` and allocation bytes `row_pitch * height`.
Use a 512-byte-aligned footprint base (zero for a dedicated buffer).

The velocity -> tile -> neighbor chain can read these buffers directly through
space107 raw SRVs (`Batch2FetchRaw`), using the same handle slot for corresponding
SRV/UAV descriptors. Insert UAV barriers and appropriate resource-state
transitions between producers/consumers. Dispatch the reduction shaders as one
whole 16x16 / 3x3 group per output tile; never partially mask lanes before a
barrier. Neighbor limits follow output dimensions rather than hard-coded 64x60.

For texture-consuming passes, copy the scratch footprint into a matching
RGBA32_FLOAT texture with CopyTextureRegion and the required UAV/COPY/SRV state
transitions. Copy each SSAO depth level into its selected texture mip; pass
`level` and matching output dimensions explicitly. SSAO's previous output must
likewise be made available as its history texture. These copies/scheduling are
**host integration requirements, not implemented frame-graph work in this batch**.
The mip-size helper queries the selected mip, not mip zero.

Particle active-list dispatch requires the listing header cleared before use,
capacity for at least `objects` entries plus its four header words, and UAV
barriers before reading steps/listing in subsequent dispatches. A zero-size
scratch footprint must never be dispatched. Particle-half depth mode uses a depth
attachment; composite mode uses the upstream premultiplied blend state with depth
writes disabled.

## Validation

The mandatory local precheck compiles all stages with actual Microsoft DXC
`-T *_6_4 -HV 2021 -WX`, independently checks them with Slang, checks HLSL
structure/register budgets/descriptor tables, and runs host tests. Regression
coverage checks world roots/semantics, material flags, packed layer loader
offsets, scratch pitch/size/invalid-dimension cases and the existing detile tests.
These checks do not replace Xbox hardware rendering or image comparisons.
