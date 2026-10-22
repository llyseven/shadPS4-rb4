// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace Libraries::Pad {
struct OrbisPadData;
}

namespace Input::HidInstrument {

// Merges the latest input of the USB instrument or instrument dongle at the HID path into a pad
// data report. Supports the Rock Band and Guitar Hero instruments of the PS3, Wii, PS4 and PS5,
// and Santroller instruments.
void Apply(const std::string& path, Libraries::Pad::OrbisPadData& data);

} // namespace Input::HidInstrument
