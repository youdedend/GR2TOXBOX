// SPDX-License-Identifier: GPL-2.0-or-later
// Production ring, real packet size, no graphics API or guest memory required.
#include "graphics/gr2port/scene_ring.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace Ring = Gr2Port::SceneRing;
constexpr u64 Base = u64{1} << 32; // Scene arena, byte offset zero.

void Write(u8* data, u64 frame) {
    std::memcpy(data, &frame, sizeof(frame));
    const u64 tail = ~frame;
    std::memcpy(data + Ring::SlotBytes() - sizeof(tail), &tail, sizeof(tail));
}
void Check(const Ring::Packet& packet, u64 frame, const std::vector<u8>& memory) {
    assert(packet.frame == frame && packet.slot < Ring::Slots);
    assert(packet.data == memory.data() + packet.slot * Ring::SlotBytes());
    assert(packet.address == Base + packet.slot * Ring::SlotBytes());
    assert(Ring::Address(packet.data) == packet.address);
    u64 head, tail;
    std::memcpy(&head, packet.data, sizeof(head));
    std::memcpy(&tail, packet.data + Ring::SlotBytes() - sizeof(tail), sizeof(tail));
    assert(head == frame && tail == ~frame);
}
void Publish(u64 frame, bool keep = false) {
    auto* data = Ring::Begin(keep);
    assert(data && Ring::WritingSlot() < Ring::Slots);
    Write(data, frame);
    Ring::Publish(frame, keep);
}
int main() {
    Ring::Packet packet{};
    Ring::Attach(nullptr, 0);
    assert(!Ring::Begin() && !Ring::TryTake(0, packet));
    std::vector<u8> memory(Ring::Slots * Ring::SlotBytes());
    Ring::Attach(memory.data(), Base);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) assert(!Ring::TryTake(0, packet));
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    Publish(1);
    assert(Ring::TryTake(0, packet)); Check(packet, 1, memory);
    assert(!Ring::TryTake(1, packet)); // The held slot is not taken twice.
    Ring::Release(packet.slot);

    Publish(2); Publish(3); Publish(4);
    assert(Ring::TryTake(1, packet)); Check(packet, 4, memory);
    Ring::Release(packet.slot); // Older non-kept frames were freed.
    assert(!Ring::TryTake(4, packet));

    Publish(5); Publish(6, true); Publish(7); Publish(8, true);
    Publish(9, true); // A kept frame may take the last free slot.
    assert(!Ring::Begin() && !Ring::Begin(true));
    assert(Ring::Skipped() == 2);
    assert(Ring::TryTake(4, packet)); Check(packet, 6, memory); Ring::Release(packet.slot);
    assert(Ring::TryTake(6, packet)); Check(packet, 8, memory); Ring::Release(packet.slot);
    assert(Ring::TryTake(8, packet)); Check(packet, 9, memory); Ring::Release(packet.slot);
    assert(!Ring::TryTake(9, packet));
    Publish(10);
    assert(!Ring::TryTake(10, packet)); // Respect the caller's seen watermark.
    assert(Ring::TryTake(9, packet)); Check(packet, 10, memory); Ring::Release(packet.slot);

    // Begin(false) leaves a slot for a kept frame. Held slots cannot be reused.
    Ring::Attach(memory.data(), Base);
    for (u64 frame = 1; frame < Ring::Slots; ++frame) Publish(frame);
    assert(!Ring::Begin());
    Publish(5, true);
    assert(Ring::Take(0, packet)); Check(packet, 5, memory);
    const auto held = packet;
    for (u64 frame = 6; frame < 9; ++frame) Publish(frame);
    Check(held, 5, memory);
    Ring::Release(held.slot);

    // One producer / one consumer stress: all kept packets must arrive in order.
    // Only Release allows the producer to overwrite a consumed slot's payload.
    Ring::Attach(memory.data(), Base);
    constexpr u64 Count = 20000;
    std::thread producer([] {
        for (u64 frame = 1; frame <= Count; ++frame) {
            u8* data;
            while (!(data = Ring::Begin(true))) std::this_thread::yield();
            Write(data, frame);
            Ring::Publish(frame, true);
        }
    });
    u64 seen = 0;
    while (seen < Count) {
        if (!Ring::TryTake(seen, packet)) { std::this_thread::yield(); continue; }
        Check(packet, seen + 1, memory);
        seen = packet.frame;
        Ring::Release(packet.slot);
    }
    producer.join();
    assert(!Ring::TryTake(seen, packet));
    Ring::Attach(nullptr, 0); // Lifecycle boundary: both threads quiescent.
    assert(!Ring::Begin() && !Ring::TryTake(0, packet));
    Ring::Attach(memory.data(), Base);
    assert(Ring::Skipped() == 0); // Reattach resets state for a fresh session.
    Publish(1); assert(Ring::TryTake(0, packet)); Check(packet, 1, memory);
    Ring::Release(packet.slot); Ring::Attach(nullptr, 0);
    std::cout << "SceneRing: roundtrip, newest/kept ordering, seen, release, 20000 concurrent packets passed\n";
}
