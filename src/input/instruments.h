// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>

#include "common/types.h"

namespace Libraries::Pad {
enum class OrbisPadDeviceClass;
struct OrbisPadData;
} // namespace Libraries::Pad

namespace Input::Instruments {

// The device class reported for a player, from 1 to 4.
Libraries::Pad::OrbisPadDeviceClass GetDeviceClass(s32 player);

// Whether the player's instrument is a device of its own rather than their controller.
bool HasInstrumentDevice(s32 player);

// Merges the state of the player's instrument into their pad data report.
void Apply(s32 player, Libraries::Pad::OrbisPadData& data);

// The microphone device of a player. Games such as Rock Band 4 use the microphones of all users
// for their singers, so the logged in players get the configured microphones in player order,
// those without an instrument first, and a silent one when none is left.
std::string GetMicrophone(s32 player);

} // namespace Input::Instruments
