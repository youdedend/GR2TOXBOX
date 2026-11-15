// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// PS4 GCN micro/macro detiler, derived from host_shaders/tiling.comp.
// One mip/slice extent per dispatch. Pitch/height are padded element dimensions;
// for BC textures an element is a compressed block, never an individual pixel.
#include "common/descriptors.hlsli"
cbuffer DetileConstants : register(b1) {
    uint g_pitch; uint g_height; uint g_depth; uint g_texel_count;
    uint g_bytes; uint g_micro_mode; uint g_thickness; uint g_samples;
    uint g_array_mode; uint g_pipe_config; uint g_bank_width; uint g_bank_height;
    uint g_num_banks; uint g_tile_split; uint g_macro_aspect; uint g_bank_swizzle;
};
cbuffer DetileTextures : register(b2) {
    uint g_input; uint g_output; uint g_input_offset; uint g_output_offset;
};
#define BITS_PER_PIXEL (g_bytes * 8u)
#define NUM_SAMPLES g_samples
#define MICRO_TILE_MODE g_micro_mode
#define MICRO_TILE_THICKNESS g_thickness
#define ARRAY_MODE g_array_mode
#define PIPE_CONFIG g_pipe_config
#define BANK_WIDTH g_bank_width
#define BANK_HEIGHT g_bank_height
#define NUM_BANKS g_num_banks
#define NUM_BANK_BITS uint(firstbithigh(g_num_banks))
#define TILE_SPLIT_BYTES g_tile_split
#define MACRO_TILE_ASPECT g_macro_aspect
#define NUM_PIPES (PIPE_CONFIG == 0u ? 2u : 8u)
#define NUM_PIPE_BITS (PIPE_CONFIG == 0u ? 1u : 3u)
#define _BIT(v, b) (((v) >> (b)) & 1u)
#define MICRO_TILE_WIDTH 8
#define MICRO_TILE_HEIGHT 8
#define MICRO_TILE_PIXELS (MICRO_TILE_WIDTH * MICRO_TILE_HEIGHT)
#define MICRO_TILE_BITS (MICRO_TILE_PIXELS * MICRO_TILE_THICKNESS * BITS_PER_PIXEL * NUM_SAMPLES)
#define MICRO_TILE_BYTES (MICRO_TILE_BITS / 8)

#define NUM_PIPE_INTERLEAVE_BITS 8

#define ADDR_SURF_DISPLAY_MICRO_TILING 0
#define ADDR_SURF_THIN_MICRO_TILING 1
#define ADDR_SURF_DEPTH_MICRO_TILING 2
#define ADDR_SURF_ROTATED_MICRO_TILING 3

#define ARRAY_LINEAR_GENERAL 0
#define ARRAY_LINEAR_ALIGNED 1
#define ARRAY_1D_TILED_THIN1 2
#define ARRAY_1D_TILED_THICK 3
#define ARRAY_2D_TILED_THIN1 4
#define ARRAY_PRT_TILED_THIN1 5
#define ARRAY_PRT_2D_TILED_THIN1 6
#define ARRAY_2D_TILED_THICK 7
#define ARRAY_2D_TILED_XTHICK 8
#define ARRAY_PRT_TILED_THICK 9
#define ARRAY_PRT_2D_TILED_THICK 10
#define ARRAY_PRT_3D_TILED_THIN1 11
#define ARRAY_3D_TILED_THIN1 12
#define ARRAY_3D_TILED_THICK 13
#define ARRAY_3D_TILED_XTHICK 14
#define ARRAY_PRT_3D_TILED_THICK 15

#define ADDR_SURF_P2 0
#define ADDR_SURF_P8_32x32_8x16 10
#define ADDR_SURF_P8_32x32_16x16 12

#define BITS_PER_BYTE 8
#define BITS_TO_BYTES(x) (((x) + (BITS_PER_BYTE - 1)) / BITS_PER_BYTE)


uint ComputePixelIndexWithinMicroTile(uint x, uint y, uint z, uint thickness) {
    uint p0 = 0;
    uint p1 = 0;
    uint p2 = 0;
    uint p3 = 0;
    uint p4 = 0;
    uint p5 = 0;
    uint p6 = 0;
    uint p7 = 0;
    uint p8 = 0;

    uint x0 = _BIT(x, 0);
    uint x1 = _BIT(x, 1);
    uint x2 = _BIT(x, 2);
    uint y0 = _BIT(y, 0);
    uint y1 = _BIT(y, 1);
    uint y2 = _BIT(y, 2);
    uint z0 = _BIT(z, 0);
    uint z1 = _BIT(z, 1);
    uint z2 = _BIT(z, 2);

    if (MICRO_TILE_MODE == ADDR_SURF_DISPLAY_MICRO_TILING) {
        if (BITS_PER_PIXEL == 8) {
        p0 = x0;
        p1 = x1;
        p2 = x2;
        p3 = y1;
        p4 = y0;
        p5 = y2;
        } else if (BITS_PER_PIXEL == 16) {
        p0 = x0;
        p1 = x1;
        p2 = x2;
        p3 = y0;
        p4 = y1;
        p5 = y2;
        } else if (BITS_PER_PIXEL == 32) {
        p0 = x0;
        p1 = x1;
        p2 = y0;
        p3 = x2;
        p4 = y1;
        p5 = y2;
        } else if (BITS_PER_PIXEL == 64) {
        p0 = x0;
        p1 = y0;
        p2 = x1;
        p3 = x2;
        p4 = y1;
        p5 = y2;
        } else if (BITS_PER_PIXEL == 128) {
        p0 = x0;
        p1 = y0;
        p2 = x1;
        p3 = y1;
        p4 = x2;
        p5 = y2;
        }
    } else if (MICRO_TILE_MODE == ADDR_SURF_THIN_MICRO_TILING ||
               MICRO_TILE_MODE == ADDR_SURF_DEPTH_MICRO_TILING) {
        p0 = x0;
        p1 = y0;
        p2 = x1;
        p3 = y1;
        p4 = x2;
        p5 = y2;
    } else {
        if (BITS_PER_PIXEL == 8 || BITS_PER_PIXEL == 16) {
        p0 = x0;
        p1 = y0;
        p2 = x1;
        p3 = y1;
        p4 = z0;
        p5 = z1;
        } else if (BITS_PER_PIXEL == 32) {
        p0 = x0;
        p1 = y0;
        p2 = x1;
        p3 = z0;
        p4 = y1;
        p5 = z1;
        } else if (BITS_PER_PIXEL == 64 || BITS_PER_PIXEL == 128) {
        p0 = x0;
        p1 = y0;
        p2 = z0;
        p3 = x1;
        p4 = y1;
        p5 = z1;
        }
        p6 = x2;
        p7 = y2;

        if (thickness == 8) {
            p8 = z2;
        }
    }

    uint pixel_number = ((p0) | (p1 << 1) | (p2 << 2) | (p3 << 3) | (p4 << 4) | (p5 << 5) |
                             (p6 << 6) | (p7 << 7) | (p8 << 8));

    return pixel_number;
}

uint ComputeSurfaceAddrFromCoordMicroTiled(uint x, uint y, uint slice,
                                               uint pitch, uint height, uint thickness,
                                               uint sample_index) {
    uint micro_tile_bytes = MICRO_TILE_PIXELS * thickness * BITS_PER_PIXEL * NUM_SAMPLES / 8;
    uint slice_bytes = BITS_TO_BYTES(pitch * height * thickness * BITS_PER_PIXEL * NUM_SAMPLES);

    uint micro_tiles_per_row = pitch / MICRO_TILE_WIDTH;
    uint micro_tile_index_x = x / MICRO_TILE_WIDTH;
    uint micro_tile_index_y = y / MICRO_TILE_HEIGHT;
    uint micro_tile_index_z = slice / thickness;

    uint slice_offset = micro_tile_index_z * slice_bytes;
    uint micro_tile_offset =
        (micro_tile_index_y * micro_tiles_per_row + micro_tile_index_x) * micro_tile_bytes;

    uint pixel_index = ComputePixelIndexWithinMicroTile(x, y, slice, thickness);

    uint sample_offset;
    uint pixel_offset;
    if (MICRO_TILE_MODE == ADDR_SURF_DEPTH_MICRO_TILING) {
        sample_offset = sample_index * BITS_PER_PIXEL;
        pixel_offset = pixel_index * BITS_PER_PIXEL * NUM_SAMPLES;
    } else {
        sample_offset = sample_index * (micro_tile_bytes * 8 / NUM_SAMPLES);
        pixel_offset = pixel_index * BITS_PER_PIXEL;
    }

    uint elem_offset = (sample_offset + pixel_offset) / 8;
    return slice_offset + micro_tile_offset + elem_offset;
}

uint ComputePipeFromCoord(uint x, uint y, uint slice) {
    uint p0 = 0;
    uint p1 = 0;
    uint p2 = 0;

    uint tx = x / MICRO_TILE_WIDTH;
    uint ty = y / MICRO_TILE_HEIGHT;
    uint x3 = _BIT(tx, 0);
    uint x4 = _BIT(tx, 1);
    uint x5 = _BIT(tx, 2);
    uint y3 = _BIT(ty, 0);
    uint y4 = _BIT(ty, 1);
    uint y5 = _BIT(ty, 2);

    if (PIPE_CONFIG == ADDR_SURF_P2) {
        p0 = x3 ^ y3;
    } else if (PIPE_CONFIG == ADDR_SURF_P8_32x32_8x16) {
        p0 = x4 ^ y3 ^ x5;
        p1 = x3 ^ y4;
        p2 = x5 ^ y5;
    } else if (PIPE_CONFIG == ADDR_SURF_P8_32x32_16x16) {
        p0 = x3 ^ y3 ^ x4;
        p1 = x4 ^ y4;
        p2 = x5 ^ y5;
    }

    uint pipe = p0 | (p1 << 1) | (p2 << 2);

    uint pipe_swizzle = 0;
    if (ARRAY_MODE == ARRAY_3D_TILED_THIN1 || ARRAY_MODE == ARRAY_3D_TILED_THICK ||
        ARRAY_MODE == ARRAY_3D_TILED_XTHICK) {
        pipe_swizzle += max(1u, NUM_PIPES / 2 - 1) * (slice / MICRO_TILE_THICKNESS);
    }
    pipe_swizzle &= (NUM_PIPES - 1);
    pipe = pipe ^ pipe_swizzle;
    return pipe;
}

uint ComputeBankFromCoord(uint x, uint y, uint slice, uint tile_split_slice) {
    uint b0 = 0;
    uint b1 = 0;
    uint b2 = 0;
    uint b3 = 0;
    uint slice_rotation = 0;
    uint tile_split_rotation = 0;

    uint tx = x / MICRO_TILE_WIDTH / (BANK_WIDTH * NUM_PIPES);
    uint ty = y / MICRO_TILE_HEIGHT / BANK_HEIGHT;

    uint x3 = _BIT(tx, 0);
    uint x4 = _BIT(tx, 1);
    uint x5 = _BIT(tx, 2);
    uint x6 = _BIT(tx, 3);
    uint y3 = _BIT(ty, 0);
    uint y4 = _BIT(ty, 1);
    uint y5 = _BIT(ty, 2);
    uint y6 = _BIT(ty, 3);

    if (NUM_BANKS == 16) {
        b0 = x3 ^ y6;
        b1 = x4 ^ y5 ^ y6;
        b2 = x5 ^ y4;
        b3 = x6 ^ y3;
    } else if (NUM_BANKS == 8) {
        b0 = x3 ^ y5;
        b1 = x4 ^ y4 ^ y5;
        b2 = x5 ^ y3;
    } else if (NUM_BANKS == 4) {
        b0 = x3 ^ y4;
        b1 = x4 ^ y3;
    } else if (NUM_BANKS == 2) {
        b0 = x3 ^ y3;
    }

    uint bank = b0 | (b1 << 1) | (b2 << 2) | (b3 << 3);

    if (ARRAY_MODE == ARRAY_2D_TILED_THIN1 || ARRAY_MODE == ARRAY_2D_TILED_THICK ||
        ARRAY_MODE == ARRAY_2D_TILED_XTHICK) {
        slice_rotation = ((NUM_BANKS / 2) - 1) * (slice / MICRO_TILE_THICKNESS);
    } else if (ARRAY_MODE == ARRAY_3D_TILED_THIN1 || ARRAY_MODE == ARRAY_3D_TILED_THICK ||
               ARRAY_MODE == ARRAY_3D_TILED_XTHICK) {
        slice_rotation = max(1u, (NUM_PIPES / 2) - 1) * (slice / MICRO_TILE_THICKNESS) / NUM_PIPES;
    }

    if (ARRAY_MODE == ARRAY_2D_TILED_THIN1 || ARRAY_MODE == ARRAY_3D_TILED_THIN1 ||
        ARRAY_MODE == ARRAY_PRT_2D_TILED_THIN1 || ARRAY_MODE == ARRAY_PRT_3D_TILED_THIN1) {
        tile_split_rotation = ((NUM_BANKS / 2) + 1) * tile_split_slice;
    }

    bank ^= g_bank_swizzle + slice_rotation;
    bank ^= tile_split_rotation;
    bank &= (NUM_BANKS - 1);

    return bank;
}

uint ComputeSurfaceAddrFromCoordMacroTiled(uint x, uint y, uint slice,
                                               uint pitch, uint height,
                                               uint sample_index) {
    uint pixel_index = ComputePixelIndexWithinMicroTile(x, y, slice, MICRO_TILE_THICKNESS);

    uint sample_offset;
    uint pixel_offset;
    if (MICRO_TILE_MODE == ADDR_SURF_DEPTH_MICRO_TILING) {
        sample_offset = sample_index * BITS_PER_PIXEL;
        pixel_offset = pixel_index * BITS_PER_PIXEL * NUM_SAMPLES;
    } else {
        sample_offset = sample_index * (MICRO_TILE_BITS / NUM_SAMPLES);
        pixel_offset = pixel_index * BITS_PER_PIXEL;
    }

    uint element_offset = (pixel_offset + sample_offset) / 8;

    uint micro_tile_bytes = MICRO_TILE_BYTES;
    uint slices_per_tile = 1;
    uint tile_split_slice = 0;
    if (MICRO_TILE_BYTES > TILE_SPLIT_BYTES && MICRO_TILE_THICKNESS == 1) {
        slices_per_tile = MICRO_TILE_BYTES / TILE_SPLIT_BYTES;
        tile_split_slice = element_offset / TILE_SPLIT_BYTES;
        element_offset %= TILE_SPLIT_BYTES;
        micro_tile_bytes = TILE_SPLIT_BYTES;
    }

    uint macro_tile_pitch = (MICRO_TILE_WIDTH * BANK_WIDTH * NUM_PIPES) * MACRO_TILE_ASPECT;
    uint macro_tile_height = (MICRO_TILE_HEIGHT * BANK_HEIGHT * NUM_BANKS) / MACRO_TILE_ASPECT;

    uint macro_tile_bytes = micro_tile_bytes * (macro_tile_pitch / MICRO_TILE_WIDTH) *
                                (macro_tile_height / MICRO_TILE_HEIGHT) / (NUM_PIPES * NUM_BANKS);

    uint macro_tiles_per_row = pitch / macro_tile_pitch;

    uint macro_tile_index_x = x / macro_tile_pitch;
    uint macro_tile_index_y = y / macro_tile_height;
    uint macro_tile_offset =
        ((macro_tile_index_y * macro_tiles_per_row) + macro_tile_index_x) * macro_tile_bytes;
    uint macro_tiles_per_slice = macro_tiles_per_row * (height / macro_tile_height);

    uint slice_bytes = macro_tiles_per_slice * macro_tile_bytes;
    uint slice_offset =
        slice_bytes * (tile_split_slice + slices_per_tile * (slice / MICRO_TILE_THICKNESS));

    uint tile_row_index = (y / MICRO_TILE_HEIGHT) % BANK_HEIGHT;
    uint tile_column_index = ((x / MICRO_TILE_WIDTH) / NUM_PIPES) % BANK_WIDTH;
    uint tile_index = (tile_row_index * BANK_WIDTH) + tile_column_index;
    uint tile_offset = tile_index * micro_tile_bytes;

    uint total_offset = slice_offset + macro_tile_offset + element_offset + tile_offset;

    if (ARRAY_MODE == ARRAY_PRT_TILED_THIN1 || ARRAY_MODE == ARRAY_PRT_TILED_THICK ||
        ARRAY_MODE == ARRAY_PRT_2D_TILED_THIN1 || ARRAY_MODE == ARRAY_PRT_2D_TILED_THICK ||
        ARRAY_MODE == ARRAY_PRT_3D_TILED_THIN1 || ARRAY_MODE == ARRAY_PRT_3D_TILED_THICK) {
        x %= macro_tile_pitch;
        y %= macro_tile_height;
    }

    uint pipe = ComputePipeFromCoord(x, y, slice);
    uint bank = ComputeBankFromCoord(x, y, slice, tile_split_slice);

    uint pipe_interleave_mask = (1 << NUM_PIPE_INTERLEAVE_BITS) - 1;
    uint pipe_interleave_offset = total_offset & pipe_interleave_mask;
    uint offset = total_offset >> NUM_PIPE_INTERLEAVE_BITS;

    uint addr = pipe_interleave_offset;
    uint pipe_bits = pipe << NUM_PIPE_INTERLEAVE_BITS;
    uint bank_bits = bank << (NUM_PIPE_INTERLEAVE_BITS + NUM_PIPE_BITS);
    uint offset_bits = offset << (NUM_PIPE_INTERLEAVE_BITS + NUM_PIPE_BITS + NUM_BANK_BITS);

    addr |= pipe_bits;
    addr |= bank_bits;
    addr |= offset_bits;

    return addr;
}

uint SourceOffset(uint texel) {
    const uint x = texel % g_pitch;
    const uint y = (texel / g_pitch) % g_height;
    const uint slice = texel / (g_pitch * g_height);
    if (ARRAY_MODE == ARRAY_1D_TILED_THIN1 || ARRAY_MODE == ARRAY_1D_TILED_THICK) {
        return ComputeSurfaceAddrFromCoordMicroTiled(x, y, slice, g_pitch, g_height, MICRO_TILE_THICKNESS, 0u);
    }
    return ComputeSurfaceAddrFromCoordMacroTiled(x, y, slice, g_pitch, g_height, 0u);
}
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint word = id.x + id.y * (65535u * 64u);
    if (word >= (g_texel_count * g_bytes) / 4u) return;
    ByteAddressBuffer input = DescriptorRaw(g_input);
    uint result = 0u;
    if (g_bytes < 4u) {
        // No racing sub-DWORD UAV stores. Every invocation owns four bytes.
        [unroll] for (uint byte_index = 0u; byte_index < 4u; ++byte_index) {
            const uint linear_value = word * 4u + byte_index;
            const uint source = g_input_offset + SourceOffset(linear_value / g_bytes) + linear_value % g_bytes;
            const uint value = (input.Load(source & ~3u) >> ((source & 3u) * 8u)) & 255u;
            result |= value << (byte_index * 8u);
        }
    } else {
        const uint words_per_texel = g_bytes / 4u;
        result = input.Load(g_input_offset + SourceOffset(word / words_per_texel) + (word % words_per_texel) * 4u);
    }
    DescriptorRawRw(g_output).Store(g_output_offset + word * 4u, result);
}
