// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <utility>

#include <SDL3/SDL_gamepad.h>

#include "common/singleton.h"
#include "core/emulator_settings.h"
#include "core/libraries/pad/pad.h"
#include "core/user_settings.h"
#include "input/controller.h"
#include "input/hid_instrument.h"
#include "input/instrument_report.h"
#include "input/instruments.h"
#include "input/midi_drums.h"

namespace Input::Instruments {

using namespace InstrumentReport;
using Libraries::Pad::OrbisPadData;
using Libraries::Pad::OrbisPadDeviceClass;

constexpr std::string_view MidiPrefix = "midi:";
constexpr std::string_view HidPrefix = "hid:";
// Reads a drum kit controller with the Guitar Hero layout instead of the Rock Band one.
constexpr std::string_view GuitarHeroDrums = "gh";

static std::optional<PlayerInput> GetPlayer(s32 player) {
    const auto players = EmulatorSettings.GetPlayers();
    if (player < 1 || player > static_cast<s32>(players.size())) {
        return std::nullopt;
    }
    return players[player - 1];
}

static bool IsSeparateDevice(std::string_view device) {
    return device.starts_with(MidiPrefix) || device.starts_with(HidPrefix);
}

static SDL_Gamepad* GetGamepad(s32 player) {
    return (*Common::Singleton<GameControllers>::Instance())[player - 1]->m_sdl_gamepad;
}

// Instruments read from a controller, such as the Xbox 360 instruments, use the raw controller
// state rather than the mapped one, as their axes carry instrument data.
class ControllerState {
public:
    explicit ControllerState(SDL_Gamepad* gamepad) : gamepad{gamepad} {}

    bool Button(SDL_GamepadButton button) const {
        return SDL_GetGamepadButton(gamepad, button);
    }

    int Axis(SDL_GamepadAxis axis) const {
        return SDL_GetGamepadAxis(gamepad, axis);
    }

    Navigation GetNavigation() const {
        // Hat directions indexed by the up, right, down and left bits.
        static constexpr std::array<u8, 16> hats = {8, 0, 2, 1, 4, 8, 3, 8, 6, 7, 8, 8, 5, 8, 8, 8};
        const int index =
            Button(SDL_GAMEPAD_BUTTON_DPAD_UP) | Button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) << 1 |
            Button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) << 2 | Button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) << 3;
        return {.hat = hats[index], .start = Button(SDL_GAMEPAD_BUTTON_START)};
    }

private:
    SDL_Gamepad* gamepad;
};

static u8 Scale(int value) {
    return static_cast<u8>(std::clamp(value, 0, 255));
}

// Xbox 360 guitars: frets on the face buttons and left bumper, solo flag on the left stick click,
// whammy on the right stick X axis, tilt on the right stick Y axis and pickup switch on the left
// trigger.
static void ApplyControllerGuitar(const ControllerState& controller, OrbisPadData& data) {
    const u8 frets = static_cast<u8>(controller.Button(SDL_GAMEPAD_BUTTON_SOUTH) |
                                     controller.Button(SDL_GAMEPAD_BUTTON_EAST) << 1 |
                                     controller.Button(SDL_GAMEPAD_BUTTON_NORTH) << 2 |
                                     controller.Button(SDL_GAMEPAD_BUTTON_WEST) << 3 |
                                     controller.Button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) << 4);
    const bool solo = controller.Button(SDL_GAMEPAD_BUTTON_LEFT_STICK);
    const int pickup = controller.Axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >> 7;
    ClearInstrumentButtons(data);
    EncodeGuitar({.frets = solo ? u8{0} : frets,
                  .solo_frets = solo ? frets : u8{0},
                  .pickup = static_cast<u8>(std::min(pickup / (255 / 5), 4)),
                  .whammy = Scale((controller.Axis(SDL_GAMEPAD_AXIS_RIGHTX) + 32768) >> 8),
                  // SDL points the Y axes down, while the guitars report tilting up as positive.
                  .tilt = Scale(-controller.Axis(SDL_GAMEPAD_AXIS_RIGHTY) >> 7)},
                 data);
    EncodeNavigation(controller.GetNavigation(), data);
}

// Xbox 360 Rock Band drums: pads on the face buttons with pad (right stick click) and cymbal
// (right bumper) flags, kicks on the left bumper and left stick click, and velocities on the
// sticks.
static void ApplyControllerRockBandDrums(s32 player, const ControllerState& controller,
                                         OrbisPadData& data) {
    static std::array<bool, 4> has_flags{};
    const u8 hits =
        DecodeFourLaneHits({.red = controller.Button(SDL_GAMEPAD_BUTTON_EAST),
                            .yellow = controller.Button(SDL_GAMEPAD_BUTTON_NORTH),
                            .blue = controller.Button(SDL_GAMEPAD_BUTTON_WEST),
                            .green = controller.Button(SDL_GAMEPAD_BUTTON_SOUTH),
                            .pad = controller.Button(SDL_GAMEPAD_BUTTON_RIGHT_STICK),
                            .cymbal = controller.Button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
                            .dpad_up = controller.Button(SDL_GAMEPAD_BUTTON_DPAD_UP),
                            .dpad_down = controller.Button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)},
                           has_flags[player - 1]);
    // The 15 bit velocities are inverted, the hardest hit is zero. The Y axes are inverted as
    // well, which SDL undoes.
    const auto velocity = [&controller](SDL_GamepadAxis axis) {
        return static_cast<u8>(std::max((0x7FFF - (controller.Axis(axis) & 0x7FFF)) >> 7, 1));
    };
    DrumState state =
        FourLaneDrums(hits, velocity(SDL_GAMEPAD_AXIS_LEFTX), velocity(SDL_GAMEPAD_AXIS_LEFTY),
                      velocity(SDL_GAMEPAD_AXIS_RIGHTX), velocity(SDL_GAMEPAD_AXIS_RIGHTY));
    state.kick1 = controller.Button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    state.kick2 = controller.Button(SDL_GAMEPAD_BUTTON_LEFT_STICK);
    ClearInstrumentButtons(data);
    EncodeDrums(state, data);
    // Cymbal hits also press the d-pad, which only navigates when nothing is hit.
    if (hits == 0) {
        EncodeNavigation(controller.GetNavigation(), data);
    }
}

// Xbox 360 Guitar Hero drums: five lanes on the face buttons and right bumper, and the kick on the
// left bumper. Hits use the full velocity.
static void ApplyControllerGuitarHeroDrums(const ControllerState& controller, OrbisPadData& data) {
    const auto hit = [&controller](SDL_GamepadButton button) {
        return controller.Button(button) ? u8{255} : u8{0};
    };
    DrumState state{.kick1 = controller.Button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)};
    state.velocities[Red] = hit(SDL_GAMEPAD_BUTTON_EAST);
    state.velocities[YellowCymbal] = hit(SDL_GAMEPAD_BUTTON_NORTH);
    state.velocities[Blue] = hit(SDL_GAMEPAD_BUTTON_WEST);
    state.velocities[GreenCymbal] = hit(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    state.velocities[Green] = hit(SDL_GAMEPAD_BUTTON_SOUTH);
    ClearInstrumentButtons(data);
    EncodeDrums(state, data);
    EncodeNavigation(controller.GetNavigation(), data);
}

OrbisPadDeviceClass GetDeviceClass(s32 player) {
    if (EmulatorSettings.IsUsingSpecialPad()) {
        return static_cast<OrbisPadDeviceClass>(EmulatorSettings.GetSpecialPadClass());
    }
    const auto input = GetPlayer(player);
    return input ? static_cast<OrbisPadDeviceClass>(input->instrument)
                 : OrbisPadDeviceClass::Standard;
}

bool HasInstrumentDevice(s32 player) {
    const auto input = GetPlayer(player);
    return input && IsSeparateDevice(input->instrument_device);
}

void Apply(s32 player, OrbisPadData& data) {
    const auto input = GetPlayer(player);
    if (!input) {
        return;
    }
    const std::string_view device = input->instrument_device;
    if (IsSeparateDevice(device) && !data.connected) {
        // The instrument is the player's only device, report it with idle sticks.
        data.leftStick = {128, 128};
        data.rightStick = {128, 128};
        data.connected = true;
        data.connectedCount = 1;
    }
    if (device.starts_with(MidiPrefix)) {
        MidiDrums::Apply(std::string{device.substr(MidiPrefix.size())}, data);
        return;
    }
    if (device.starts_with(HidPrefix)) {
        HidInstrument::Apply(std::string{device.substr(HidPrefix.size())}, data);
        return;
    }
    SDL_Gamepad* gamepad = GetGamepad(player);
    if (!gamepad) {
        return;
    }
    const ControllerState controller{gamepad};
    switch (GetDeviceClass(player)) {
    case OrbisPadDeviceClass::Guitar:
        ApplyControllerGuitar(controller, data);
        break;
    case OrbisPadDeviceClass::Drum:
        if (device == GuitarHeroDrums) {
            ApplyControllerGuitarHeroDrums(controller, data);
        } else {
            ApplyControllerRockBandDrums(player, controller, data);
        }
        break;
    default:
        break;
    }
}

std::string GetMicrophone(s32 player) {
    // Players without an instrument come first, as they are the ones who can sing.
    const auto order = [](s32 index) {
        return std::pair{GetDeviceClass(index) != OrbisPadDeviceClass::Standard, index};
    };
    size_t slot = 0;
    for (s32 other = 1; other <= static_cast<s32>(EmulatorSettings.GetPlayers().size()); other++) {
        const auto user = UserManagement.GetUserByPlayerIndex(other);
        if (user && user->logged_in && order(other) < order(player)) {
            slot++;
        }
    }
    const auto mics = EmulatorSettings.GetMics();
    if (slot >= mics.size() || mics[slot].empty()) {
        return "None";
    }
    return mics[slot];
}

} // namespace Input::Instruments
