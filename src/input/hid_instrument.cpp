// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

#include <SDL3/SDL_hidapi.h>

#include "common/logging/log.h"
#include "core/libraries/pad/pad.h"
#include "input/hid_instrument.h"
#include "input/instrument_report.h"

namespace Input::HidInstrument {

using namespace InstrumentReport;
using Libraries::Pad::OrbisPadData;

// Report formats of the supported instruments, see the PlasticBand documentation.
enum class Format {
    Ps4Guitar,
    Ps4Drums,
    Ps5Guitar,
    Ps3RockBandGuitar,
    Ps3RockBandDrums,
    Ps3GuitarHeroGuitar,
    Ps3GuitarHeroDrums,
    SantrollerRockBandGuitar,
    SantrollerGuitarHeroGuitar,
    SantrollerRockBandDrums,
    SantrollerGuitarHeroDrums,
};

struct KnownDevice {
    u16 vendor_id;
    u16 product_id;
    // Matches any release when zero.
    u16 release;
    Format format;
};

constexpr std::array<KnownDevice, 27> KnownDevices = {{
    {0x0738, 0x8261, 0, Format::Ps4Guitar}, // Mad Catz Stratocaster
    {0x0E6F, 0x0173, 0, Format::Ps4Guitar}, // PDP Jaguar
    {0x0E6F, 0x024A, 0, Format::Ps4Guitar}, // PDP Riffmaster
    {0x3651, 0x1500, 0, Format::Ps4Guitar}, // CRKD Gibson SG
    {0x3651, 0x5500, 0, Format::Ps4Guitar}, // CRKD Gibson SG dongle
    {0x0738, 0x8262, 0, Format::Ps4Drums},  // Mad Catz drums
    {0x0E6F, 0x0174, 0, Format::Ps4Drums},  // PDP drums
    {0x0E6F, 0x0249, 0, Format::Ps5Guitar}, // PDP Riffmaster
    {0x3651, 0x1600, 0, Format::Ps5Guitar}, // CRKD Gibson SG
    {0x3651, 0x5600, 0, Format::Ps5Guitar}, // CRKD Gibson SG dongle
    {0x12BA, 0x0200, 0, Format::Ps3RockBandGuitar},
    {0x1BAD, 0x0004, 0, Format::Ps3RockBandGuitar}, // Wii
    {0x1BAD, 0x3010, 0, Format::Ps3RockBandGuitar}, // Wii
    {0x12BA, 0x0210, 0, Format::Ps3RockBandDrums},
    {0x1BAD, 0x0005, 0, Format::Ps3RockBandDrums}, // Wii
    {0x1BAD, 0x3110, 0, Format::Ps3RockBandDrums}, // Wii
    {0x12BA, 0x0100, 0, Format::Ps3GuitarHeroGuitar},
    {0x1430, 0x474C, 0, Format::Ps3GuitarHeroGuitar}, // World Tour PC
    {0x12BA, 0x0120, 0, Format::Ps3GuitarHeroDrums},
    {0x1209, 0x2882, 0x0400, Format::SantrollerRockBandGuitar},
    {0x1209, 0x2885, 0x0400, Format::SantrollerRockBandGuitar},
    {0x1209, 0x2882, 0x0300, Format::SantrollerGuitarHeroGuitar},
    {0x1209, 0x2885, 0x0300, Format::SantrollerGuitarHeroGuitar},
    {0x1209, 0x2882, 0x0600, Format::SantrollerRockBandDrums},
    {0x1209, 0x2885, 0x0600, Format::SantrollerRockBandDrums},
    {0x1209, 0x2882, 0x0500, Format::SantrollerGuitarHeroDrums},
    {0x1209, 0x2885, 0x0500, Format::SantrollerGuitarHeroDrums},
}};

constexpr size_t MaxReportSize = 64;
using Report = std::array<u8, MaxReportSize>;

struct Device {
    SDL_hid_device* handle{};
    Format format{};
    std::mutex report_mutex;
    Report report{};
    size_t report_size{};
    u8 last_pickup_notch{};
    bool has_drum_flags{};
    std::jthread reader;

    ~Device() {
        reader.request_stop();
        if (reader.joinable()) {
            reader.join();
        }
        if (handle) {
            SDL_hid_close(handle);
        }
    }
};

static std::mutex devices_mutex;
static std::unordered_map<std::string, std::unique_ptr<Device>> devices;

static std::optional<Format> FindFormat(const SDL_hid_device_info& info) {
    for (const auto& known : KnownDevices) {
        if (known.vendor_id == info.vendor_id && known.product_id == info.product_id &&
            (known.release == 0 || known.release == info.release_number)) {
            return known.format;
        }
    }
    return std::nullopt;
}

static void ReadReports(std::stop_token stop, Device* device) {
    Report buffer;
    while (!stop.stop_requested()) {
        const int size = SDL_hid_read_timeout(device->handle, buffer.data(), buffer.size(), 100);
        if (size < 0) {
            LOG_ERROR(Input, "Lost connection to HID instrument: {}", SDL_GetError());
            return;
        }
        if (size == 0) {
            continue;
        }
        std::scoped_lock lock{device->report_mutex};
        device->report = buffer;
        device->report_size = size;
    }
}

// A device that fails to open is still returned, without a handle, so that it isn't retried on
// every read.
static std::unique_ptr<Device> OpenDevice(const std::string& path) {
    auto device = std::make_unique<Device>();
    if (SDL_hid_init() != 0) {
        LOG_ERROR(Input, "Failed to initialize HID: {}", SDL_GetError());
        return device;
    }
    SDL_hid_device* handle = SDL_hid_open_path(path.c_str());
    if (!handle) {
        LOG_ERROR(Input, "Failed to open HID instrument {}: {}", path, SDL_GetError());
        return device;
    }
    const SDL_hid_device_info* info = SDL_hid_get_device_info(handle);
    const auto format = info ? FindFormat(*info) : std::nullopt;
    if (!format) {
        LOG_ERROR(Input, "HID device {} is not a supported instrument", path);
        SDL_hid_close(handle);
        return device;
    }
    LOG_INFO(Input, "Using HID instrument {:04x}:{:04x} at {}", info->vendor_id, info->product_id,
             path);
    device->handle = handle;
    device->format = *format;
    device->reader = std::jthread(ReadReports, device.get());
    return device;
}

static bool Bit(const Report& report, size_t byte, u8 mask) {
    return (report[byte] & mask) != 0;
}

static u8 Scale(int value) {
    return static_cast<u8>(std::clamp(value, 0, 255));
}

// Velocities are zero when a lane isn't hit, so hits keep at least the softest velocity.
static u8 HitVelocity(int value) {
    return static_cast<u8>(std::clamp(value, 1, 255));
}

static u8 Frets(bool green, bool red, bool yellow, bool blue, bool orange) {
    return static_cast<u8>(green | red << 1 | yellow << 2 | blue << 3 | orange << 4);
}

// PS4 and PS5 instruments report their data the way the game expects it.
static void DecodePs4Guitar(const Report& r, OrbisPadData& data) {
    EncodeGuitar({.frets = static_cast<u8>(r[46] & 0x1F),
                  .solo_frets = static_cast<u8>(r[47] & 0x1F),
                  .pickup = r[43],
                  .whammy = r[44],
                  .tilt = r[45]},
                 data);
    EncodeNavigation({.hat = static_cast<u8>(r[5] & 0x0F), .start = Bit(r, 6, 0x20)}, data);
}

static void DecodePs4Drums(const Report& r, OrbisPadData& data) {
    DrumState state{.kick1 = Bit(r, 6, 0x01), .kick2 = Bit(r, 6, 0x02)};
    std::copy_n(r.begin() + 43, NumDrumLanes, state.velocities.begin());
    EncodeDrums(state, data);
    EncodeNavigation({.hat = static_cast<u8>(r[5] & 0x0F), .start = Bit(r, 6, 0x20)}, data);
}

static void DecodePs5Guitar(const Report& r, OrbisPadData& data) {
    EncodeGuitar({.frets = static_cast<u8>(r[43] & 0x1F),
                  .solo_frets = static_cast<u8>(r[44] & 0x1F),
                  .whammy = r[41],
                  .tilt = r[42]},
                 data);
    EncodeNavigation({.hat = static_cast<u8>(r[8] & 0x0F), .start = Bit(r, 9, 0x20)}, data);
}

// PS3 and Wii instruments use the PS3 controller report, which has no report ID.
enum Ps3Buttons : u8 {
    Ps3Square = 0x01,
    Ps3Cross = 0x02,
    Ps3Circle = 0x04,
    Ps3Triangle = 0x08,
    Ps3L1 = 0x10,
    Ps3R1 = 0x20,
    Ps3L2 = 0x40,
};
enum Ps3ExtraButtons : u8 {
    Ps3Start = 0x02,
    Ps3L3 = 0x04,
    Ps3R3 = 0x08,
};
constexpr size_t Ps3Hat = 2;

static Navigation Ps3Navigation(const Report& r) {
    return {.hat = r[Ps3Hat], .start = Bit(r, 1, Ps3Start)};
}

static void DecodePs3RockBandGuitar(Device& device, const Report& r, OrbisPadData& data) {
    const u8 frets = Frets(Bit(r, 0, Ps3Cross), Bit(r, 0, Ps3Circle), Bit(r, 0, Ps3Triangle),
                           Bit(r, 0, Ps3Square), Bit(r, 0, Ps3L1));
    const bool solo = Bit(r, 0, Ps3L2);
    EncodeGuitar({.frets = solo ? u8{0} : frets,
                  .solo_frets = solo ? frets : u8{0},
                  .pickup = PickupNotch(r[6], device.last_pickup_notch),
                  .whammy = r[5],
                  .tilt = Bit(r, 0, Ps3R1) ? u8{255} : u8{0}},
                 data);
    EncodeNavigation(Ps3Navigation(r), data);
}

static void DecodePs3RockBandDrums(Device& device, const Report& r, OrbisPadData& data) {
    const u8 hat = r[Ps3Hat];
    const u8 hits = DecodeFourLaneHits({.red = Bit(r, 0, Ps3Circle),
                                        .yellow = Bit(r, 0, Ps3Triangle),
                                        .blue = Bit(r, 0, Ps3Square),
                                        .green = Bit(r, 0, Ps3Cross),
                                        .pad = Bit(r, 1, Ps3L3),
                                        .cymbal = Bit(r, 1, Ps3R3),
                                        .dpad_up = hat == 0,
                                        .dpad_down = hat == 4},
                                       device.has_drum_flags);
    // Velocities are inverted, the hardest hit is zero.
    const auto velocity = [&r](size_t offset) { return HitVelocity(255 - r[offset]); };
    DrumState state = FourLaneDrums(hits, velocity(12), velocity(11), velocity(14), velocity(13));
    state.kick1 = Bit(r, 0, Ps3L1);
    state.kick2 = Bit(r, 0, Ps3R1);
    EncodeDrums(state, data);
    // Cymbal hits also press the d-pad, which only navigates when nothing is hit.
    if (hits == 0) {
        EncodeNavigation(Ps3Navigation(r), data);
    }
}

static void DecodePs3GuitarHeroGuitar(const Report& r, OrbisPadData& data) {
    // Tilt comes from the accelerometer, which is below its center when pointed up, or the pedal.
    const int accelerometer = r[19] | r[20] << 8;
    EncodeGuitar({.frets = Frets(Bit(r, 0, Ps3Cross), Bit(r, 0, Ps3Circle), Bit(r, 0, Ps3Square),
                                 Bit(r, 0, Ps3Triangle), Bit(r, 0, Ps3L1)),
                  .whammy = Scale((r[5] - 0x80) * 2),
                  .tilt = Bit(r, 0, Ps3R1) ? u8{255} : Scale((0x200 - accelerometer) * 2)},
                 data);
    EncodeNavigation(Ps3Navigation(r), data);
}

// Guitar Hero drums have five lanes, whose yellow and orange cymbals become the yellow and green
// cymbals. Their velocities are MIDI velocities.
static void DecodePs3GuitarHeroDrums(const Report& r, OrbisPadData& data) {
    const auto velocity = [&r](u8 button, size_t offset) {
        return Bit(r, 0, button) ? HitVelocity(r[offset] * 2) : u8{0};
    };
    DrumState state{.kick1 = Bit(r, 0, Ps3L1)};
    state.velocities[Red] = velocity(Ps3Circle, 12);
    state.velocities[YellowCymbal] = velocity(Ps3Triangle, 11);
    state.velocities[Blue] = velocity(Ps3Square, 14);
    state.velocities[GreenCymbal] = velocity(Ps3R1, 16);
    state.velocities[Green] = velocity(Ps3Cross, 13);
    EncodeDrums(state, data);
    EncodeNavigation(Ps3Navigation(r), data);
}

// Santroller instruments in their own HID mode, told apart by their release number.
static void DecodeSantrollerRockBandGuitar(Device& device, const Report& r, OrbisPadData& data) {
    const int buttons = r[1] | r[2] << 8;
    EncodeGuitar({.frets = static_cast<u8>(buttons & 0x1F),
                  .solo_frets = static_cast<u8>(buttons >> 5 & 0x1F),
                  .pickup = PickupNotch(r[5], device.last_pickup_notch),
                  .whammy = r[4],
                  .tilt = Scale((r[6] - 0x80) * 2)},
                 data);
    EncodeNavigation({.hat = r[3], .start = Bit(r, 2, 0x08)}, data);
}

static void DecodeSantrollerGuitarHeroGuitar(const Report& r, OrbisPadData& data) {
    EncodeGuitar({.frets = static_cast<u8>(r[1] & 0x1F),
                  .whammy = r[4],
                  .tilt = Bit(r, 1, 0x20) ? u8{255} : Scale((r[6] - 0x80) * 2)},
                 data);
    EncodeNavigation({.hat = r[3], .start = Bit(r, 1, 0x80)}, data);
}

static void DecodeSantrollerRockBandDrums(const Report& r, OrbisPadData& data) {
    DrumState state{.kick1 = Bit(r, 1, 0x40), .kick2 = Bit(r, 1, 0x80)};
    state.velocities[Green] = r[4];
    state.velocities[Red] = r[5];
    state.velocities[Yellow] = r[6];
    state.velocities[Blue] = r[7];
    state.velocities[GreenCymbal] = r[8];
    state.velocities[YellowCymbal] = r[9];
    state.velocities[BlueCymbal] = r[10];
    EncodeDrums(state, data);
    // Cymbal hits also press the d-pad, which only navigates when nothing is hit.
    if (std::ranges::all_of(state.velocities, [](u8 velocity) { return velocity == 0; })) {
        EncodeNavigation({.hat = r[3], .start = Bit(r, 2, 0x02)}, data);
    }
}

static void DecodeSantrollerGuitarHeroDrums(const Report& r, OrbisPadData& data) {
    DrumState state{.kick1 = Bit(r, 1, 0x20)};
    state.velocities[Green] = r[4];
    state.velocities[Red] = r[5];
    state.velocities[YellowCymbal] = r[6];
    state.velocities[Blue] = r[7];
    state.velocities[GreenCymbal] = r[8];
    EncodeDrums(state, data);
    EncodeNavigation({.hat = r[3], .start = Bit(r, 1, 0x80)}, data);
}

static void Decode(Device& device, const Report& r, OrbisPadData& data) {
    switch (device.format) {
    case Format::Ps4Guitar:
        return DecodePs4Guitar(r, data);
    case Format::Ps4Drums:
        return DecodePs4Drums(r, data);
    case Format::Ps5Guitar:
        return DecodePs5Guitar(r, data);
    case Format::Ps3RockBandGuitar:
        return DecodePs3RockBandGuitar(device, r, data);
    case Format::Ps3RockBandDrums:
        return DecodePs3RockBandDrums(device, r, data);
    case Format::Ps3GuitarHeroGuitar:
        return DecodePs3GuitarHeroGuitar(r, data);
    case Format::Ps3GuitarHeroDrums:
        return DecodePs3GuitarHeroDrums(r, data);
    case Format::SantrollerRockBandGuitar:
        return DecodeSantrollerRockBandGuitar(device, r, data);
    case Format::SantrollerGuitarHeroGuitar:
        return DecodeSantrollerGuitarHeroGuitar(r, data);
    case Format::SantrollerRockBandDrums:
        return DecodeSantrollerRockBandDrums(r, data);
    case Format::SantrollerGuitarHeroDrums:
        return DecodeSantrollerGuitarHeroDrums(r, data);
    }
}

void Apply(const std::string& path, OrbisPadData& data) {
    Device* device;
    {
        std::scoped_lock lock{devices_mutex};
        auto& entry = devices[path];
        if (!entry) {
            entry = OpenDevice(path);
        }
        device = entry.get();
    }

    std::scoped_lock lock{device->report_mutex};
    if (device->report_size != 0) {
        Decode(*device, device->report, data);
    }
}

} // namespace Input::HidInstrument
