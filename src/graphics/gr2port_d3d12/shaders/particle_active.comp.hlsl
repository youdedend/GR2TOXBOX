// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/batch2_scene.hlsli"
cbuffer ArenaConstants : register(b0) { ArenaTable g_arena; };
cbuffer ParticleActiveRoot : register(b1) {
    uint2 owners_words;
    uint2 emitters_words;
    uint2 descriptions_words;
    uint2 steps_words;
    uint2 listing_words;
    uint objects;
};
#define owners ArenaMake(owners_words.x, owners_words.y)
#define emitters ArenaMake(emitters_words.x, emitters_words.y)
#define descriptions ArenaMake(descriptions_words.x, descriptions_words.y)
#define steps ArenaMake(steps_words.x, steps_words.y)
#define listing ArenaMake(listing_words.x, listing_words.y)
static const uint Settled = 4u;
static const uint SpawnBit = 0x100u;
static const uint StepMask = 0xffu;

[numthreads(64,1,1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint emitter = dispatch_id.x;
    if (emitter >= objects) {
        return;
    }
    const bool owned = ArenaBuffer(owners,g_arena).Load(owners.offset+4u*(2u * emitter)) != 0u;
    const uint description = ArenaBuffer(emitters,g_arena).Load(emitters.offset+4u*(32u * emitter + 8u));
    const uint spawns = ArenaBuffer(descriptions,g_arena).Load(descriptions.offset+4u*(64u * description + 4u)) == 1u ? SpawnBit : 0u;
    const uint last = ArenaBuffer(steps,g_arena).Load(steps.offset+4u*(emitter));
    uint count = 0u;
    if (!owned && (last & SpawnBit) == spawns) {
        count = min((last & StepMask) + 1u, Settled);
    }
    ArenaBufferRw(steps,g_arena).Store(steps.offset+4u*emitter,count | spawns);
    if (count < Settled) {
        uint slot; ArenaBufferRw(listing,g_arena).InterlockedAdd(listing.offset,1u,slot);
        ArenaBufferRw(listing,g_arena).Store(listing.offset+4u*(4u+slot),emitter);
        ArenaBufferRw(listing,g_arena).InterlockedMax(listing.offset+4u,slot/64u+1u);
    }
}
