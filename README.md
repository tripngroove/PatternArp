# PatternArp

An arpeggiator plugin that stacks simple musical shapes (pitch, rhythm, octave, velocity and more), each looping at its own length, so their interaction produces complex, evolving patterns. It runs on an instrument track and sends MIDI notes and CC on to whatever comes next: a software instrument or hardware synths.

**Status:** v1 pattern stacking. Several patterns take turns note by note, each with its own timing, length, direction and drawn lanes, so their different lengths drift against each other. It plays on its own while the transport runs. Other MIDI (CC, pitch bend) passes through. Current focus: CLAP in Bitwig on macOS.

## How it plays

Patterns interleave: with pattern A = C, D, E and pattern B = G, A, the arp plays C G D A E G C A ... and repeats once every pattern lines up again. The preview shows how long that takes ("Repeats every 3 beats"). A new instance starts with two patterns taking turns: A = C3 D3 E3 and B = F4 G4 A4 B4.

The **Rotation** lane, just under the preview, decides whose turn each step is. Closed, it shows the rotation as lettered steps (A B by default). Click it to open a grid with a row per pattern ("Pattern A"...), where each step keeps its place, size and letter: click or drag to give steps to a pattern, click a pattern's own step to quiet it (it dims and stays silent for that pattern's step length, so nothing else moves) or to light it again, and drag the handle on the grid to set how many steps the rotation loops over (1 to 16; lengthening repeats the steps there are). A A A B, for example, plays B on every fourth note. A pattern with every step quiet is paused.

Patterns keep their letter and colour. "+ Add pattern", to the right of the Rotation lane, and a pattern's Clone button add a pattern at the end of the list (the view scrolls down to it); it takes the next step of the rotation, and get the letter after the highest one in use; deleting a pattern removes its steps from the rotation and closes the gaps. There can be up to 8 patterns.

Each pattern has these settings. Click a value to pick from a list, or use its arrows or drag it up and down. The scroll wheel only scrolls the plugin.

| Setting | What it does |
| --- | --- |
| Timing | How long each of its notes lasts, 1/64 to a whole note (triplets included) |
| Length | Number of steps, 1 to 16. Changing it in the editor resamples the drawn lanes; automating it just plays fewer or more steps and keeps the rest. |
| Direction | Forward, Backward, Ping-pong, Random, Walk (random step up or down) |
| Play / Pause | The button by the pattern name. It shows playing while any of the pattern's Rotation steps are lit; pausing quiets them all (they keep their time, so nothing else moves) and playing lights them all again. Automatable as "P1 Play"... |
| Notes (Piano Roll / MIDI Thru) | Piano Roll: the notes switched on in its keyboard. MIDI Learn (under the switch) pulses red while armed. The first note you play starts the pattern afresh (just that note, one step, every lane back to its default); each note after it is switched on and added as the next step of the Notes lane, up to 16 steps. Timing, direction and the other settings stay. Click it again ("Done") to stop. MIDI Thru: the notes you hold, with the Notes lane running from the lowest held note to the highest. A MIDI Thru pattern rests while nothing is held; Latch, in the settings menu (click the PatternArp title), keeps the last chord. Note names follow Bitwig, Ableton and Logic: MIDI note 60 is C3. |

Drawn lanes, one bar per step: Notes, Velocity, Gate, Octave (-3 to +3), Ratchet (1 to 8 repeats in a step) and Chance (percent above the line in 5% steps, 0% to 100%; below it Elektron-style ratios from 1:2 to 8:8, where 3:4 plays the third of every four times the step comes up, counting on across passes of a hand-shortened loop). Failed steps rest, are skipped, or extend: the note before them is held until the failed step's note would have ended, so a 0% extend step is a tie. "Repeats every" ignores percent chances, so it shows the underlying loop. Double-click a step to reset it, or a lane's tab to reset the whole lane (its length stays). Right-click a step to set all the lane's steps to its value. The handle on each lane makes that lane loop over fewer steps than the pattern (say, octave every 3 steps of a 4-step pattern); shortened lanes show their length on the tab. Velocity and Gate have a default value, set in their tab: drag it up or down, or click it to type one. Changing it sets every step of that lane, and new and reset steps use it.

Incoming notes pass straight through unless a pattern uses MIDI Thru. With the transport stopped, MIDI Thru patterns play from the first key press on their own clock.

The preview strip shows what will play, with a playhead. Drag the handle at its end to make everything start over sooner (it snaps to the shortest pattern timing, or to bars in settings); its label shows how often the arp repeats. Random directions and chance roll again on every pass, and the preview redraws each note as the playhead passes it to show the next pass. Hover it for the loop length, or drag it into your DAW to get a MIDI clip of one full loop. By default the clip holds every note a chance step could play, so you can set chance again in the DAW; click the PatternArp title for settings, including "roll the dice" to export the take the preview shows, and "drag out whole bars", on by default, which repeats the loop until it ends on a bar line because Bitwig rounds dropped MIDI clips up to whole bars.

### Host automation

Each of the 8 pattern slots exposes Play, Timing, Length, Direction, Note Source and Chance Mode as parameters ("P1 Timing"...), plus a global Latch. Slots count from the top of the list, so after a delete "P2" may be pattern C. The Rotation lane, drawn lanes and note selections are saved with the project but are not automatable.

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
