// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
struct MotionOutput { float4 position : SV_Position; float4 attr0 : TEXCOORD0; };
MotionOutput main(uint id : SV_VertexID) {
    float2 p=float2(float(int(id & 1u)*4-1),float(int(id & 2u)*2-1));
    MotionOutput o; o.position=float4(p,0.0,1.0); o.attr0=float4(p*0.5+0.5,0.0,1.0); return o;
}
