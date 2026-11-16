// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GR2_UI_RASTER_HLSLI
#define GR2_UI_RASTER_HLSLI
// D3D12 viewports cannot have negative height. The CPU normalizes their bounds
// and supplies the equivalent clip-Y sign through the existing 16-byte b2 root.
cbuffer UiRasterTextures : register(b2) {float ui_viewport_y;uint3 ui_raster_padding;};
float4 UiClip(float4 p){p.y*=ui_viewport_y;return p;}
#endif
