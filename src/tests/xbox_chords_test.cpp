// SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Behavioural test for the Xbox controller chord layer, which stands in for the
// DualShock 4 touchpad and motion inputs the game expects. A chord that fires
// twice, fires without its modifier, or does not swallow the button it
// borrowed breaks the game's own menus, so each of those is asserted here.
//
// Dependency free, like the memory budget test. Build:
//   c++ -std=c++23 -Isrc -DGR2_CHORDS_SELFTEST
//       src/tests/xbox_chords_test.cpp src/uwp/chords.cpp -o chords_test

#include <cstdio>
#include <cstdlib>
#include <string>

#include "common/types.h"
#include "uwp/chords.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* expression, int line) {
    ++g_checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++g_failures;
    }
}

#define CHECK(expr) Check((expr), #expr, __LINE__)

using namespace Uwp::Chords;

void DefaultsAreUsable() {
    const Bindings bindings = Current();
    CHECK(bindings.chords[static_cast<u32>(Chord::LunarStyle)].enabled);
    CHECK(bindings.chords[static_cast<u32>(Chord::JupiterStyle)].enabled);
    CHECK(bindings.chords[static_cast<u32>(Chord::Panther)].enabled);
    CHECK(bindings.chords[static_cast<u32>(Chord::NormalStyle)].enabled);
    // The D-Pad belongs to the game (photo mode, map, gestures, first person) so
    // flick chords start disabled.
    CHECK(!bindings.chords[static_cast<u32>(Chord::FlickUp)].enabled);
    CHECK(!bindings.chords[static_cast<u32>(Chord::FlickDown)].enabled);
}

void ModifierAloneDoesNothing() {
    PadState previous{};
    PadState current{};
    current.view = true;
    const ChordOutputs out = Update(current, previous);
    CHECK(!out.lunar_style);
    CHECK(!out.jupiter_style);
    CHECK(!out.normal_style);
    CHECK(!out.motion_active);
}

void StyleChordIsEdgeTriggered() {
    PadState previous{};
    PadState current{};
    current.view = true;
    previous = current;
    current.left_bumper = true;
    ChordOutputs out = Update(current, previous);
    CHECK(out.lunar_style);
    // Holding the chord must not keep re-triggering the transformation.
    previous = current;
    out = Update(current, previous);
    CHECK(!out.lunar_style);
}

void MotionChordDrivesTiltFromTheStick() {
    PadState previous{};
    PadState current{};
    current.view = true;
    current.y = true;
    previous = current;
    ChordOutputs out = Update(current, previous);
    CHECK(out.motion_active);
    CHECK(out.motion_tilt_roll == 0.0f);
    CHECK(out.swallow_face);

    current.right_stick_x = 1.0f;
    previous = current;
    out = Update(current, previous);
    CHECK(out.motion_tilt_roll > 0.0f);
}

void PantherHoldsTheTouchpadClick() {
    PadState previous{};
    PadState current{};
    current.view = true;
    current.left_stick = true;
    previous = current;
    const ChordOutputs out = Update(current, previous);
    CHECK(out.touchpad_center);
    CHECK(out.swallow_sticks);
}

void ChordNeedsItsModifier() {
    PadState previous{};
    PadState current{};
    current.view = true;
    current.left_stick = true;
    previous = current;
    ChordOutputs out = Update(current, previous);
    CHECK(out.touchpad_center);

    current.view = false;
    previous = current;
    out = Update(current, previous);
    CHECK(!out.touchpad_center);
}

void OptionalFlickChordSwallowsTheDpad() {
    Bindings bindings = Current();
    bindings.chords[static_cast<u32>(Chord::FlickUp)].enabled = true;
    Apply(bindings);

    PadState previous{};
    PadState current{};
    previous.view = true;
    current.view = true;
    current.dpad_up = true;
    ChordOutputs out = Update(current, previous);
    CHECK(out.touchpad_swipe_up);
    CHECK(out.swallow_dpad);

    // Holding still must not stream swipes.
    previous = current;
    out = Update(current, previous);
    CHECK(!out.touchpad_swipe_up);

    bindings.chords[static_cast<u32>(Chord::FlickUp)].enabled = false;
    Apply(bindings);
}

void SaveProducesAChordBlock() {
    const std::string json = Save(Current());
    CHECK(json.find("\"chords\"") != std::string::npos);
    Bindings reloaded{};
    CHECK(Load(reloaded, json));
}

} // namespace

int main() {
    DefaultsAreUsable();
    ModifierAloneDoesNothing();
    StyleChordIsEdgeTriggered();
    MotionChordDrivesTiltFromTheStick();
    PantherHoldsTheTouchpadClick();
    ChordNeedsItsModifier();
    OptionalFlickChordSwallowsTheDpad();
    SaveProducesAChordBlock();
    std::printf("chord self-test: %d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
