// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// fullscreen.vert -> fullscreen VS_6_6.
//
// A single triangle over the whole target. The D3D12 backend draws it with
// DrawInstanced(3, 1, 0, 0) and no vertex buffer, exactly as upstream draws it
// with gl_VertexIndex.
//
// One difference from the GLSL: the triangle is placed at the *far* plane in
// reverse-Z (`z = 0` after the transform), which is what the GLSL's `0.0` in
// clip space becomes. Upstream's comment says "behind everything in reverse
// depth", and D3D's reverse-Z convention makes that z = 0, not z = 1 as it would
// be in GL.

struct VsOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VsOutput main(uint vertex_index : SV_VertexID) {
    VsOutput output;
    // GLSL: position = vec2((id & 1) * 4 - 1, (id & 2) * 2 - 1), range
    // (-1,-1), (3,-1), (-1,3). The same corners in clip space.
    const float2 corner = float2(float((vertex_index & 1u) * 4u) - 1.0,
                                 float((vertex_index & 2u) * 2u) - 1.0);
    output.position = float4(corner, 0.0, 1.0);
    // The GLSL fragment stages take `position` in (-1, 1) and compute
    // 0.5 * position + 0.5 themselves. Emitting it as a varying keeps those
    // stages a line-for-line port instead of re-deriving uv from SV_Position.
    output.uv = corner;
    return output;
}
