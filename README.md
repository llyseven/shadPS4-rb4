# shadPS4 – Rock Band 4 build (unofficial)

A shadPS4 build made for **Rock Band 4 (CUSA02084, update 2.21)** with **Rock Band 4 Deluxe**. Not affiliated with the shadPS4 project. Please report problems here, not upstream.

This repository is the **emulator** (branch `rb4dx`). The launcher with the instrument and mic settings is in [mothership-q/shadps4-rb4-qtlauncher](https://github.com/mothership-q/shadps4-rb4-qtlauncher) (branch `midi-drums`).

**[Download the latest release](https://github.com/mothership-q/shadps4-rb4-qtlauncher/releases)**. It contains both the launcher and the emulator.

## What's new

- **NVIDIA crash fix**. Shaders and textures appear correctly now on NVIDIA cards (no more "device lost", upstream issue [#3780](https://github.com/shadps4-emu/shadPS4/issues/3780)).
- **Real instruments, per player.** Up to 4 players, each with their own controller and instrument:
  - MIDI drum kits
  - Xbox 360 guitars and drums
  - PS3, Wii, PS4 and PS5 instrument dongles, and Santroller in HID mode
- **Up to 3 mics for harmonies.** Mics that don't belong to a player get their own profile.
- **Performance improvements.** Near double performance thanks to CPU optimizations. For 120 fps, set **Settings → Experimental → Vblank Frequency** to 120.

## Install

1. Download the latest zip from [Releases](https://github.com/mothership-q/shadps4-rb4-qtlauncher/releases), unzip it anywhere and run `shadPS4QtLauncher.exe`. Keep the `emulator` folder next to it: the launcher finds the emulator there and selects it automatically.
2. Add the folder that contains your Rock Band 4 dump, with update 2.21 installed.
3. Download [Rock Band 4 Deluxe](https://rb4dx.milohax.org/index.html) and copy its files into the shadPS4 user folder. This is `%APPDATA%\shadPS4`, or the `user` folder next to the launcher in a portable setup.

| File | Where it goes |
|---|---|
| `RB4DX-Plugin.prx` | `custom_modules\CUSA02084\` |
| `GoldHEN` folder | `data\GoldHEN\` |

### Coming from an older RB4 build (LlysiX)? The folders changed

| | Old build | This build |
|---|---|---|
| Plugin | `<game>\sce_module\RB4DX-Plugin.prx` | `<user>\custom_modules\CUSA02084\RB4DX-Plugin.prx` |
| Mod data | `<user>\data\CUSA02084\GoldHEN\` | `<user>\data\GoldHEN\` |

_The plugin and mod data locations were changed in the main shadPS4 repo._

## Set up controllers

Right click on Rock Band 4 and **Game-specific Settings → Configure Game-specific Settings → Input → Players**: one row per player.

- **Controller**: the pad that player uses, or *Automatic* to assign pads in connection order.
- **Instrument**: Gamepad, Guitar or Drums.
- **Instrument Device**: a MIDI kit, a dongle or a USB instrument. Leave it on *This Controller* for Xbox 360 instruments. When an instrument device is set, the player doesn't need a pad.
- **Mic 1–3**: the inputs used for singing. They're handed out to players in order, starting with players who have no instrument.

**MIDI Drums**: Raise *Minimum Velocity* if you get ghost notes. Use *Note Overrides* if your kit uses unusual notes, for example `38=Snare,48=HiTom`. *Hit Duration* is how long each drum pad hit is held for. Don't mess with this unless you know exactly what you're doing.

## Building

You only need this if you want to build the emulator yourself. Follow the upstream instructions for [Windows](documents/building-windows.md), [Linux](documents/building-linux.md) or [macOS](documents/building-macos.md), but clone this fork's branch:

```
git clone --recursive -b rb4dx https://github.com/mothership-q/shadPS4-rb4.git
```

To use your build with the launcher, put `shadPS4.exe` in the `emulator` folder next to `shadPS4QtLauncher.exe`, or add it in the launcher's **Version Manager** with *Add custom*.

## Source

GPL-2.0, like shadPS4. Based on [shadPS4](https://github.com/shadps4-emu/shadPS4) and the [shadPS4 Qt launcher](https://github.com/shadps4-emu/shadps4-qtlauncher).

- Emulator: [mothership-q/shadPS4-rb4](https://github.com/mothership-q/shadPS4-rb4/tree/rb4dx) branch `rb4dx` (this repository)
- Launcher: [mothership-q/shadps4-rb4-qtlauncher](https://github.com/mothership-q/shadps4-rb4-qtlauncher/tree/midi-drums) branch `midi-drums`
