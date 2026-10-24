# PatternArp

An arpeggiator plugin that stacks simple musical shapes (pitch, rhythm, octave, velocity and more), each looping at its own length, so their interaction produces complex, evolving patterns. It runs on an instrument track and sends MIDI notes and CC on to whatever comes next: a software instrument or hardware synths.

**Status:** first playable arp. Hold notes and PatternArp plays them back locked to the song position, with a pitch lane (up, down, up/down, as played, random over 1 to 4 octaves) and a Euclidean rhythm lane deciding which steps play. Other MIDI (CC, pitch bend) passes through. Current focus: CLAP in Bitwig on macOS.

## Controls

| Control | What it does |
| --- | --- |
| Rate | Step length: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 |
| Direction | Order the held notes are walked in: Up, Down, Up/Down, As Played, Random (repeatable at the same song position) |
| Octaves | Repeats the held notes 1 to 4 octaves up |
| Gate | Note length as a percentage of a step |
| Rhythm Hits / Steps / Rotate | Euclidean rhythm: spreads Hits evenly over Steps, shifted by Rotate. Steps that miss are silent, but the pitch lane keeps moving, so a rhythm length that differs from the number of notes makes the pattern shift over time. |

When the transport is stopped, PatternArp runs on its own clock at the project tempo, starting on the first key press.

## Formats

| Plugin | Formats | Where to use it |
| --- | --- | --- |
| PatternArp | CLAP, VST3 | Bitwig (as a note effect), Ableton Live (VST3), Reaper, Cubase, FL Studio, Studio One |
| PatternArp MIDI FX | AU (macOS) | Logic Pro's MIDI FX slot |

## Getting a test build

Every push builds the plugins for macOS, Windows and Linux on GitHub.

1. Open the [Actions tab](../../actions), pick the latest green **Build** run, and download `PatternArp-macOS` (or `-Windows`).
2. Unzip it and copy the plugins into your plugin folders:
   - **macOS:** `PatternArp.clap` to `~/Library/Audio/Plug-Ins/CLAP/`, `PatternArp.vst3` to `~/Library/Audio/Plug-Ins/VST3/`, `PatternArp MIDI FX.component` to `~/Library/Audio/Plug-Ins/Components/`.
   - **Windows:** `PatternArp.clap` to `C:\Program Files\Common Files\CLAP\`, `PatternArp.vst3` to `C:\Program Files\Common Files\VST3\`.
3. **macOS only:** test builds are not signed yet, so clear the download quarantine once per build:
   ```sh
   xattr -cr ~/Library/Audio/Plug-Ins/CLAP/PatternArp.clap ~/Library/Audio/Plug-Ins/VST3/PatternArp.vst3 "$HOME/Library/Audio/Plug-Ins/Components/PatternArp MIDI FX.component"
   ```
4. Rescan plugins in your DAW.

## Building from source

Needs CMake 3.28+, a C++20 compiler and Ninja (optional). Dependencies (JUCE 9, clap-juce-extensions, Catch2) are downloaded automatically.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Plugins land in `build/plugin/PatternArp_artefacts/Release/`.

## Layout

| Folder | What it holds |
| --- | --- |
| `engine/` | The pattern engine: plain C++ with no plugin code, so it can be tested on its own. |
| `plugin/` | The JUCE plugin wrapper (MIDI in/out, host sync, UI). |
| `tests/` | Catch2 tests for the engine. |
| `.github/workflows/` | CI: builds, runs tests, validates with pluginval, clap-validator and auval, and uploads the plugins. |

## Licence note

PatternArp uses [JUCE](https://juce.com), which is free under AGPLv3 or its Starter licence for small projects. Check JUCE's current terms before distributing builds.
