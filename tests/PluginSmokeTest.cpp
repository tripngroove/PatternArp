// Drives the real plugin processor like a host would: a held chord, a playing transport,
// then a stopped one, checking the MIDI that comes out.

#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
class FakePlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (120.0);
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        return info;
    }

    bool playing = false;
    double ppq = 0.0;
};

struct Rendered
{
    std::vector<int> noteOns;
    int noteOffs = 0;
    int controllers = 0;
};

Rendered render (juce::AudioProcessor& plugin, FakePlayHead& playHead, juce::MidiBuffer input, int blocks)
{
    constexpr int blockSize = 512;
    constexpr double sampleRate = 48000.0;
    const double beatsPerBlock = blockSize / (sampleRate * 60.0 / 120.0);

    juce::AudioBuffer<float> audio (2, blockSize);
    Rendered result;

    for (int i = 0; i < blocks; ++i)
    {
        juce::MidiBuffer midi;
        if (i == 0)
            midi = input;

        plugin.processBlock (audio, midi);

        for (const auto metadata : midi)
        {
            const auto m = metadata.getMessage();
            if (m.isNoteOn())
                result.noteOns.push_back (m.getNoteNumber());
            else if (m.isNoteOff())
                ++result.noteOffs;
            else if (m.isController())
                ++result.controllers;
        }

        if (playHead.playing)
            playHead.ppq += beatsPerBlock;
    }
    return result;
}
} // namespace

TEST_CASE ("plugin arpeggiates a held chord and passes CC through")
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    FakePlayHead playHead;
    plugin->setPlayHead (&playHead);
    plugin->setPlayConfigDetails (0, 2, 48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);

    juce::MidiBuffer chord;
    chord.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    chord.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
    chord.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
    chord.addEvent (juce::MidiMessage::controllerEvent (1, 74, 64), 0);

    SECTION ("with the transport playing, one note per sixteenth in pitch order")
    {
        playHead.playing = true;
        // 1 beat at 120 bpm / 48 kHz = 24000 samples, so 46 blocks of 512 is just under 1 beat.
        const auto out = render (*plugin, playHead, chord, 46);

        CHECK (out.noteOns == std::vector<int> { 60, 64, 67, 60 });
        CHECK (out.controllers == 1);
    }

    SECTION ("with the transport stopped, the arp free-runs from the first key press")
    {
        const auto out = render (*plugin, playHead, chord, 46);
        CHECK (out.noteOns == std::vector<int> { 60, 64, 67, 60 });
    }

    SECTION ("releasing the keys stops new notes and every note gets an off")
    {
        playHead.playing = true;
        const auto before = render (*plugin, playHead, chord, 26); // a little past the 3rd note

        juce::MidiBuffer release;
        for (int n : { 60, 64, 67 })
            release.addEvent (juce::MidiMessage::noteOff (1, n), 0);

        const auto after = render (*plugin, playHead, release, 40);
        CHECK (after.noteOns.empty());
        CHECK (before.noteOffs + after.noteOffs == static_cast<int> (before.noteOns.size()));
    }

    plugin->releaseResources();
}
