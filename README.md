# PatternArp

An arpeggiator plugin that stacks simple musical shapes (pitch, rhythm, octave, velocity and more), each looping at its own length, so their interaction produces complex, evolving patterns. It runs on an instrument track and sends MIDI notes and CC on to whatever comes next: a software instrument or hardware synths.

**Status:** v1 pattern stacking. Several patterns take turns note by note, each with its own timing, length, direction and drawn lanes, so their different lengths drift against each other. It plays on its own while the transport runs. Other MIDI (CC, pitch bend) passes through. Current focus: CLAP in Bitwig on macOS.

## How it plays

Patterns interleave: with pattern A = C, D, E and pattern B = G, A, the arp plays C G D A E G C A ... and repeats once every pattern lines up again. The header shows how long that takes ("Repeats every 3 beats").

Each pattern has:

| Setting | What it does |
| --- | --- |
| Timing | How long each of its notes lasts, 1/32 to a whole note (triplets included) |
| Length | Number of steps, 1 to 16. Changing it in the editor resamples the drawn lanes; automating it just plays fewer or more steps and keeps the rest. |
| Period | n:m takes n of every m notes. 1:1 is a "fill" pattern that plays whatever the others leave. If two patterns claim the same note, the higher one wins and the other gets the next free note. |
| Direction | Forward, Backward, Ping-pong, Random, Walk (random step up or down) |
| Play / Pause | The button by the pattern name. A paused pattern is taken out of the rotation entirely; the others close up the gap. Automatable as "P1 Play"... |
| Notes (Select / MIDI In) | Select: the notes switched on in its keyboard. MIDI In: the notes you hold, with the Notes lane running from the lowest held note to the highest. A MIDI In pattern rests while nothing is held; Latch keeps the last chord. |

Drawn lanes, one bar per step: Notes, Velocity, Gate, Octave (-3 to +3), Ratchet (1 to 8 repeats in a step) and Chance (percent above the line, every Nth time below it; failed steps either rest or are skipped). "Repeats every" ignores percent chances, so it shows the underlying loop. Double-click a step to reset it. Velocity and Gate have a default value, set in their tab.

Incoming notes pass straight through unless a pattern uses MIDI In. With the transport stopped, MIDI In patterns play from the first key press on their own clock.

The preview strip shows what will play, with a playhead. Drag the handle at its end to make everything start over sooner (it snaps to the shortest pattern timing, or to bars in settings); its label shows how often the arp repeats. Hover it for the loop length, or drag it into your DAW to get a MIDI clip of one full loop. By default the clip holds every note a chance step could play, so you can set chance again in the DAW; click the PatternArp title for settings, including "roll the dice" to export the take the preview shows, and "drag out whole bars", on by default, which repeats the loop until it ends on a bar line because Bitwig rounds dropped MIDI clips up to whole bars.

### Host automation

Each of the 8 pattern slots exposes Play, Timing, Length, Period, Direction, Note Source and Chance Mode as parameters ("P1 Timing"...), plus a global Latch. The drawn lanes and note selections are saved with the project but are not automatable.

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
| `plugin/` | The JUCE plugin wrapper (MIDI in/out, host sync, parameters, UI in `plugin/ui/`). |
| `tests/` | Catch2 tests for the engine, plus a plugin smoke test. |
| `.github/workflows/` | CI: builds, runs tests, validates with pluginval, clap-validator and auval, and uploads the plugins. |

## Licence note

PatternArp uses [JUCE](https://juce.com), which is free under AGPLv3 or its Starter licence for small projects. Check JUCE's current terms before distributing builds.
