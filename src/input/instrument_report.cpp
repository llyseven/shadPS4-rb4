// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "core/libraries/pad/pad.h"
#include "input/instrument_report.h"

namespace Input::InstrumentReport {

using Libraries::Pad::OrbisPadButtonDataOffset;
using Libraries::Pad::OrbisPadData;

// Pads and cymbals of the same color share a face button, kick pedals are L1 and R1.
constexpr std::array<OrbisPadButtonDataOffset, NumDrumLanes> LaneButtons = {
    OrbisPadButtonDataOffset::Circle,   OrbisPadButtonDataOffset::Square,
    OrbisPadButtonDataOffset::Triangle, OrbisPadButtonDataOffset::Cross,
    OrbisPadButtonDataOffset::Triangle, OrbisPadButtonDataOffset::Square,
    OrbisPadButtonDataOffset::Cross,
};

// Green to orange frets, solo frets also hold L3.
constexpr std::array<OrbisPadButtonDataOffset, 5> FretButtons = {
    OrbisPadButtonDataOffset::Cross,    OrbisPadButtonDataOffset::Circle,
    OrbisPadButtonDataOffset::Triangle, OrbisPadButtonDataOffset::Square,
    OrbisPadButtonDataOffset::L1,
};

// Device unique data offsets of the guitar report.
enum GuitarData : u8 { Pickup, Whammy, Tilt, Frets, SoloFrets };

static constexpr u8 Lane(DrumLane lane) {
    return 1 << lane;
}

// Follows the PlasticBand reference for deciphering pads and cymbals.
u8 DecodeFourLaneHits(FourLaneFlags flags, bool& has_flags) {
    u8 hits = 0;
    if (flags.pad || flags.cymbal) {
        has_flags = true;
    }

    // A pad and a cymbal of different colors hit together are told apart by the d-pad, which
    // yellow and blue cymbals press up and down.
    if (flags.pad && flags.cymbal) {
        const int colors = flags.red + (flags.yellow || flags.dpad_up) +
                           (flags.blue || flags.dpad_down) +
                           (flags.green || !(flags.dpad_up || flags.dpad_down));
        if (colors > 1) {
            if (flags.dpad_up) {
                hits |= Lane(YellowCymbal);
                flags.yellow = false;
            } else if (flags.dpad_down) {
                hits |= Lane(BlueCymbal);
                flags.blue = false;
            } else {
                hits |= Lane(GreenCymbal);
                flags.green = false;
            }
            flags.cymbal = false;
        }
    }

    if (flags.pad || (!flags.cymbal && !has_flags)) {
        hits |= (flags.red ? Lane(Red) : 0) | (flags.yellow ? Lane(Yellow) : 0) |
                (flags.blue ? Lane(Blue) : 0) | (flags.green ? Lane(Green) : 0);
    }
    if (flags.cymbal) {
        hits |= (flags.yellow ? Lane(YellowCymbal) : 0) | (flags.blue ? Lane(BlueCymbal) : 0) |
                (flags.green ? Lane(GreenCymbal) : 0);
    }
    return hits;
}

DrumState FourLaneDrums(u8 hits, u8 red, u8 yellow, u8 blue, u8 green) {
    DrumState state;
    const auto set = [&](DrumLane lane, u8 velocity) {
        if (hits & Lane(lane)) {
            state.velocities[lane] = velocity;
        }
    };
    set(Red, red);
    set(Yellow, yellow);
    set(Blue, blue);
    set(Green, green);
    set(YellowCymbal, (hits & Lane(Yellow)) ? red : yellow);
    set(BlueCymbal, (hits & Lane(Blue)) ? red : blue);
    set(GreenCymbal, (hits & Lane(Green)) ? red : green);
    return state;
}

u8 PickupNotch(u8 raw, u8& last_notch) {
    if (raw != 0x7F) {
        last_notch = std::min<u8>(raw / (255 / 5), 4);
    }
    return last_notch;
}

void EncodeDrums(const DrumState& state, OrbisPadData& data) {
    for (size_t lane = 0; lane < NumDrumLanes; lane++) {
        if (state.velocities[lane] != 0) {
            data.buttons |= LaneButtons[lane];
            data.deviceUniqueData[lane] = state.velocities[lane];
        }
    }
    if (state.kick1) {
        data.buttons |= OrbisPadButtonDataOffset::L1;
    }
    if (state.kick2) {
        data.buttons |= OrbisPadButtonDataOffset::R1;
    }
    data.deviceUniqueDataLen = sizeof(data.deviceUniqueData);
}

void EncodeGuitar(const GuitarState& state, OrbisPadData& data) {
    for (size_t fret = 0; fret < FretButtons.size(); fret++) {
        if ((state.frets | state.solo_frets) & (1 << fret)) {
            data.buttons |= FretButtons[fret];
        }
    }
    if (state.solo_frets != 0) {
        data.buttons |= OrbisPadButtonDataOffset::L3;
    }
    data.deviceUniqueData[Pickup] = state.pickup;
    data.deviceUniqueData[Whammy] = state.whammy;
    data.deviceUniqueData[Tilt] = state.tilt;
    data.deviceUniqueData[Frets] = state.frets;
    data.deviceUniqueData[SoloFrets] = state.solo_frets;
    data.deviceUniqueDataLen = sizeof(data.deviceUniqueData);
}

void EncodeNavigation(const Navigation& navigation, OrbisPadData& data) {
    static constexpr std::array<OrbisPadButtonDataOffset, 8> hat_directions = {
        OrbisPadButtonDataOffset::Up,
        OrbisPadButtonDataOffset::Up | OrbisPadButtonDataOffset::Right,
        OrbisPadButtonDataOffset::Right,
        OrbisPadButtonDataOffset::Down | OrbisPadButtonDataOffset::Right,
        OrbisPadButtonDataOffset::Down,
        OrbisPadButtonDataOffset::Down | OrbisPadButtonDataOffset::Left,
        OrbisPadButtonDataOffset::Left,
        OrbisPadButtonDataOffset::Up | OrbisPadButtonDataOffset::Left,
    };
    if (navigation.hat < hat_directions.size()) {
        data.buttons |= hat_directions[navigation.hat];
    }
    if (navigation.start) {
        data.buttons |= OrbisPadButtonDataOffset::Options;
    }
}

void ClearInstrumentButtons(OrbisPadData& data) {
    static constexpr auto instrument_buttons =
        OrbisPadButtonDataOffset::Cross | OrbisPadButtonDataOffset::Circle |
        OrbisPadButtonDataOffset::Triangle | OrbisPadButtonDataOffset::Square |
        OrbisPadButtonDataOffset::L1 | OrbisPadButtonDataOffset::R1 | OrbisPadButtonDataOffset::L2 |
        OrbisPadButtonDataOffset::R2 | OrbisPadButtonDataOffset::L3 | OrbisPadButtonDataOffset::R3 |
        OrbisPadButtonDataOffset::Up | OrbisPadButtonDataOffset::Down |
        OrbisPadButtonDataOffset::Left | OrbisPadButtonDataOffset::Right;
    data.buttons = data.buttons & ~instrument_buttons;
}

} // namespace Input::InstrumentReport
