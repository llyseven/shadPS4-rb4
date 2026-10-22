// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <RtMidi.h>

#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/emulator_settings.h"
#include "core/libraries/pad/pad.h"
#include "input/instrument_report.h"
#include "input/midi_drums.h"

namespace Input::MidiDrums {

using Clock = std::chrono::steady_clock;

// The drum lanes of the PS4 report followed by the kick pedal.
enum class Lane : u8 {
    Red,
    Blue,
    Yellow,
    Green,
    YellowCymbal,
    BlueCymbal,
    GreenCymbal,
    Kick,
    None,
};
constexpr size_t NumLanes = static_cast<size_t>(Lane::None);

// Piece names follow the RPCS3 MIDI drums configuration.
static std::optional<Lane> LaneFromName(std::string_view name) {
    static const std::unordered_map<std::string_view, Lane> lanes = {
        {"Kick", Lane::Kick},          {"HihatPedal", Lane::None},
        {"Snare", Lane::Red},          {"SnareRim", Lane::Red},
        {"HiTom", Lane::Yellow},       {"LowTom", Lane::Blue},
        {"FloorTom", Lane::Green},     {"HihatWithPedalUp", Lane::YellowCymbal},
        {"Hihat", Lane::YellowCymbal}, {"Ride", Lane::BlueCymbal},
        {"Crash", Lane::GreenCymbal},
    };
    const auto it = lanes.find(name);
    if (it == lanes.end()) {
        return std::nullopt;
    }
    return it->second;
}

// Note numbers sent by the Rock Band 3 MIDI Pro Adapter.
static std::array<Lane, 128> DefaultNoteMap() {
    std::array<Lane, 128> map;
    map.fill(Lane::None);
    const auto assign = [&map](Lane lane, std::initializer_list<u8> notes) {
        for (const u8 note : notes) {
            map[note] = lane;
        }
    };
    assign(Lane::Kick, {33, 35, 36});
    assign(Lane::Red, {31, 34, 37, 38, 39, 40});
    assign(Lane::Yellow, {48, 50});
    assign(Lane::Blue, {45, 47});
    assign(Lane::Green, {41, 43});
    assign(Lane::YellowCymbal, {22, 26, 42, 46, 54});
    assign(Lane::BlueCymbal, {51, 53, 56, 59});
    assign(Lane::GreenCymbal, {49, 52, 55, 57});
    return map;
}

// Overrides are a comma separated list of "note=Piece" entries, e.g. "38=Snare,49=Crash".
static void ApplyNoteOverrides(std::array<Lane, 128>& map, const std::string& overrides) {
    for (const std::string& entry : Common::SplitString(overrides, ',')) {
        const auto separator = entry.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const int note = std::atoi(entry.substr(0, separator).c_str());
        const auto lane = LaneFromName(std::string_view{entry}.substr(separator + 1));
        if (note < 0 || note >= static_cast<int>(map.size()) || !lane) {
            LOG_WARNING(Input, "Ignoring invalid MIDI drum note override '{}'", entry);
            continue;
        }
        map[note] = *lane;
    }
}

// The game only registers a hit when it reads the lane pressed after reading it released, so every
// hit is held for at least one read and the pulse width, then released for at least one read.
// Hits arriving in the meantime wait their turn instead of being merged or lost when reads stall.
struct LaneState {
    static constexpr size_t MaxQueuedHits = 8;

    std::deque<u8> queued_hits;
    u8 velocity{};
    Clock::time_point release_time{};
    bool release_reported{true};

    void Queue(u8 hit_velocity) {
        if (queued_hits.size() < MaxQueuedHits) {
            queued_hits.push_back(hit_velocity);
        }
    }

    // Returns the velocity to report for this read, zero when released.
    u8 Read(Clock::time_point now, std::chrono::milliseconds pulse_width) {
        if (velocity != 0 && now >= release_time) {
            velocity = 0;
        }
        if (velocity == 0 && release_reported && !queued_hits.empty()) {
            velocity = queued_hits.front();
            queued_hits.pop_front();
            release_time = now + pulse_width;
            release_reported = false;
        }
        if (velocity == 0) {
            release_reported = true;
        }
        return velocity;
    }
};

struct Kit {
    std::unique_ptr<RtMidiIn> midi_in;
    std::mutex lanes_mutex;
    std::array<LaneState, NumLanes> lanes;
};

static std::once_flag tuning_flag;
static std::array<Lane, 128> note_map;
static std::chrono::milliseconds pulse_width;
static u8 min_velocity;

static std::mutex kits_mutex;
static std::unordered_map<std::string, std::unique_ptr<Kit>> kits;

static void LoadTuning() {
    note_map = DefaultNoteMap();
    ApplyNoteOverrides(note_map, EmulatorSettings.GetMidiDrumsNoteOverrides());
    pulse_width = std::chrono::milliseconds{EmulatorSettings.GetMidiDrumsPulseMs()};
    min_velocity = static_cast<u8>(EmulatorSettings.GetMidiDrumsMinVelocity());
}

static void OnMessage(double, std::vector<unsigned char>* message, void* user_data) {
    if (message->size() < 3 || ((*message)[0] & 0xF0) != 0x90) {
        return;
    }
    const u8 note = (*message)[1] & 0x7F;
    const u8 velocity = (*message)[2];
    const Lane lane = note_map[note];
    if (lane == Lane::None || velocity < min_velocity) {
        return;
    }
    auto* kit = static_cast<Kit*>(user_data);
    std::scoped_lock lock{kit->lanes_mutex};
    kit->lanes[static_cast<size_t>(lane)].Queue(velocity);
}

// Opens the first MIDI input port whose name contains device, ignoring the numbers Windows gives
// devices that are plugged in again. A kit that fails to open is still returned, without an
// input, so that it isn't retried on every read.
static std::unique_ptr<Kit> OpenKit(const std::string& device) {
    auto kit = std::make_unique<Kit>();
    try {
        auto midi_in = std::make_unique<RtMidiIn>();
        const u32 port_count = midi_in->getPortCount();
        const std::string unnumbered_device = Common::RemoveDeviceInstanceNumbers(device);
        std::optional<u32> port;
        for (u32 i = 0; i < port_count; i++) {
            const std::string name = midi_in->getPortName(i);
            LOG_INFO(Input, "MIDI input port {}: {}", i, name);
            if (!port && Common::RemoveDeviceInstanceNumbers(name).find(unnumbered_device) !=
                             std::string::npos) {
                port = i;
            }
        }
        if (!port) {
            LOG_ERROR(Input, "No MIDI input port matches '{}'", device);
            return kit;
        }
        midi_in->setCallback(&OnMessage, kit.get());
        midi_in->openPort(*port);
        LOG_INFO(Input, "MIDI drums using {}", midi_in->getPortName(*port));
        kit->midi_in = std::move(midi_in);
    } catch (const RtMidiError& error) {
        LOG_ERROR(Input, "Failed to open MIDI drums '{}': {}", device, error.getMessage());
    }
    return kit;
}

void Apply(const std::string& device, Libraries::Pad::OrbisPadData& data) {
    std::call_once(tuning_flag, LoadTuning);

    Kit* kit;
    {
        std::scoped_lock lock{kits_mutex};
        auto& entry = kits[device];
        if (!entry) {
            entry = OpenKit(device);
        }
        kit = entry.get();
    }

    const auto now = Clock::now();
    InstrumentReport::DrumState state;
    std::scoped_lock lock{kit->lanes_mutex};
    for (size_t lane = 0; lane < state.velocities.size(); lane++) {
        const u8 velocity = kit->lanes[lane].Read(now, pulse_width);
        state.velocities[lane] = static_cast<u8>((velocity << 1) | (velocity >> 6));
    }
    state.kick1 = kit->lanes[static_cast<size_t>(Lane::Kick)].Read(now, pulse_width) != 0;
    InstrumentReport::EncodeDrums(state, data);
}

} // namespace Input::MidiDrums
