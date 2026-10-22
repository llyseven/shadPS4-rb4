// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace Libraries::Pad {
struct OrbisPadData;
}

namespace Input::MidiDrums {

// Merges the drum hits currently held on the MIDI input matching device into a pad data report,
// using the layout of the PS4 Rock Band drum kits: pads and cymbals share the face buttons and are
// told apart by their velocity bytes, while the kick pedal is L1.
void Apply(const std::string& device, Libraries::Pad::OrbisPadData& data);

} // namespace Input::MidiDrums
