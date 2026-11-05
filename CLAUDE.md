# PatternArp notes for Claude

- The owner is not a programmer: explain changes in plain language and keep CI green, since CI builds are how they get plugins to test.
- `engine/` must stay free of JUCE so it remains unit-testable. Put musical logic there with Catch2 tests in `tests/`.
- Two plugin targets share `plugin/` sources: `PatternArp` (VST3 + CLAP, instrument with MIDI out) and `PatternArpMidiFX` (AU MIDI effect, macOS only). Use `JucePlugin_IsMidiEffect` for differences.
- Plugins must output MIDI notes and CC to downstream devices, including hardware. Main DAW is Bitwig; Ableton and Logic must also work.
- Shared look (colours, spacing, fonts, LookAndFeel, generic widgets) lives in the `libs/plugin-ui-kit` submodule (JUCE module `tng_ui`, namespace `tng`), with its `STYLE_GUIDE.md`. `plugin/ui/Theme.h` adds only PatternArp's own bits. Put anything another plugin could reuse in the kit; changes there go to the kit repo first, then bump the submodule here.
- Local Linux build deps are listed in `.github/workflows/build.yml`.
