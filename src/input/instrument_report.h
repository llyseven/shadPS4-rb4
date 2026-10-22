// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "common/types.h"

namespace Libraries::Pad {
struct OrbisPadData;
}

// Instrument states and how the PS4 Rock Band instruments report them, see the PlasticBand
// documentation. Every instrument source decodes into these states, which are then written to the
// pad data like the console does.
namespace Input::InstrumentReport {

// Ordered like the velocity bytes of the PS4 drum report.
enum DrumLane : u8 {
    Red,
    Blue,
    Yellow,
    Green,
    YellowCymbal,
    BlueCymbal,
    GreenCymbal,
    NumDrumLanes,
};

struct DrumState {
    std::array<u8, NumDrumLanes> velocities{};
    bool kick1{};
    bool kick2{};
};

// Fret bits, from green to orange.
struct GuitarState {
    u8 frets{};
    u8 solo_frets{};
    u8 pickup{};
    u8 whammy{};
    u8 tilt{};
};

// The menu controls of an instrument.
struct Navigation {
    // Hat switch direction, 0 for up going clockwise to 7, anything else for centered.
    u8 hat{8};
    bool start{};
};

// The hits of a Rock Band drum kit from an Xbox 360, PS3 or Wii, which report the hit colors with
// shared pad and cymbal flags.
struct FourLaneFlags {
    bool red;
    bool yellow;
    bool blue;
    bool green;
    bool pad;
    bool cymbal;
    bool dpad_up;
    bool dpad_down;
};

// Returns a bitmask of the DrumLanes hit. has_flags tracks whether the kit has sent the pad or
// cymbal flags, which Rock Band 1 kits never do.
u8 DecodeFourLaneHits(FourLaneFlags flags, bool& has_flags);

// The drum state of four lane hits from kits that report one velocity per color, where a cymbal
// hit together with the pad of its color reports its velocity in the red one.
DrumState FourLaneDrums(u8 hits, u8 red, u8 yellow, u8 blue, u8 green);

// The notch of a five position pickup switch reported as a byte. PS3 and Wii guitars report 0x7F
// when the switch rests, which keeps the last notch.
u8 PickupNotch(u8 raw, u8& last_notch);

void EncodeDrums(const DrumState& state, Libraries::Pad::OrbisPadData& data);
void EncodeGuitar(const GuitarState& state, Libraries::Pad::OrbisPadData& data);
void EncodeNavigation(const Navigation& navigation, Libraries::Pad::OrbisPadData& data);

// Removes the buttons an instrument reports from pad data mapped from the same device.
void ClearInstrumentButtons(Libraries::Pad::OrbisPadData& data);

} // namespace Input::InstrumentReport
