// Drives the real plugin processor like a host would: held notes, a playing transport,
// then a stopped one, host automation and saved state, checking the MIDI that comes out.

#include "PluginProcessor.h"
#include "ui/LaneGraph.h"
#include "ui/OrderLane.h"
#include "ui/PatternPanel.h"
#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>

#if JUCE_LINUX
    #include <dlfcn.h>
#endif

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
    std::vector<juce::MidiMessage> messages; // everything, in order
};

void setParameter (juce::AudioProcessor& plugin, const juce::String& id, float plainValue)
{
    for (auto* p : plugin.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->getParameterID() == id)
        {
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (plainValue));
            return;
        }
    FAIL ("no parameter " << id);
}

int plainValue (juce::AudioProcessor& plugin, const juce::String& id)
{
    for (auto* p : plugin.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->getParameterID() == id)
            return juce::roundToInt (ranged->convertFrom0to1 (ranged->getValue()));
    FAIL ("no parameter " << id);
    return 0;
}

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
            result.messages.push_back (m);
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

TEST_CASE ("plugin plays its patterns and handles incoming notes")
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

    // 1 beat at 120 bpm / 48 kHz = 24000 samples, so 46 blocks of 512 is just under 1 beat.
    // Pausing B leaves its turns silent, so A alone needs twice as long for as many notes.
    constexpr int justUnderOneBeat = 46;
    constexpr int justUnderTwoBeats = 93;

    SECTION ("with the transport playing, the default patterns take turns and input passes through")
    {
        playHead.playing = true;
        const auto out = render (*plugin, playHead, chord, justUnderOneBeat);

        // Input chord, then A (C3 D3 E3) and B (F4 G4 A4 B4) alternating.
        CHECK (out.noteOns == std::vector<int> { 60, 64, 67, 60, 77, 62, 79 });
        CHECK (out.controllers == 1);
    }

    SECTION ("with the transport stopped, a Select pattern stays quiet")
    {
        const auto out = render (*plugin, playHead, chord, justUnderOneBeat);
        CHECK (out.noteOns == std::vector<int> { 60, 64, 67 });
    }

    SECTION ("a MIDI In pattern plays the held notes and consumes them")
    {
        setParameter (*plugin, "p1_notesource", 1.0f);
        setParameter (*plugin, "p2_play", 0.0f);
        playHead.playing = true;
        const auto out = render (*plugin, playHead, chord, justUnderTwoBeats);

        CHECK (out.noteOns == std::vector<int> { 60, 64, 67, 60 });
        CHECK (out.controllers == 1);
    }

    SECTION ("a MIDI In pattern free-runs from the first key press with the transport stopped")
    {
        setParameter (*plugin, "p1_notesource", 1.0f);
        setParameter (*plugin, "p2_play", 0.0f);
        const auto out = render (*plugin, playHead, chord, justUnderTwoBeats);
        CHECK (out.noteOns == std::vector<int> { 60, 64, 67, 60 });
    }

    SECTION ("releasing the keys stops a MIDI In pattern and every note gets an off")
    {
        setParameter (*plugin, "p1_notesource", 1.0f);
        setParameter (*plugin, "p2_play", 0.0f);
        playHead.playing = true;
        const auto before = render (*plugin, playHead, chord, 52); // a little past the 3rd note

        juce::MidiBuffer release;
        for (int n : { 60, 64, 67 })
            release.addEvent (juce::MidiMessage::noteOff (1, n), 0);

        const auto after = render (*plugin, playHead, release, 40);
        CHECK (after.noteOns.empty());
        CHECK (before.noteOffs + after.noteOffs == static_cast<int> (before.noteOns.size()));
    }

    SECTION ("pausing every pattern silences them; input still passes through")
    {
        setParameter (*plugin, "p1_play", 0.0f);
        setParameter (*plugin, "p2_play", 0.0f);
        playHead.playing = true;
        CHECK (render (*plugin, playHead, chord, justUnderOneBeat).noteOns == std::vector<int> { 60, 64, 67 });
    }

    SECTION ("a pattern's CC lane goes out just before each of its steps' notes")
    {
        playHead.playing = true;
        setParameter (*plugin, "p1_ccnumber", 74.0f);
        const auto out = render (*plugin, playHead, {}, justUnderOneBeat);
        CHECK (out.controllers == 2); // A plays twice in this beat; B's CC is Off

        for (size_t i = 0; i < out.messages.size(); ++i)
            if (const auto& m = out.messages[i]; m.isController())
            {
                CHECK (m.getControllerNumber() == 74);
                CHECK (m.getControllerValue() == 64);
                REQUIRE (i + 1 < out.messages.size());
                const auto& note = out.messages[i + 1];
                CHECK (note.isNoteOn());
                CHECK (note.getChannel() == m.getChannel());
                CHECK (note.getTimeStamp() == Catch::Approx (m.getTimeStamp()));
            }
    }

    SECTION ("automating Length shortens the pattern without losing steps")
    {
        playHead.playing = true;
        setParameter (*plugin, "p2_play", 0.0f);
        setParameter (*plugin, "p1_length", 2.0f);
        CHECK (render (*plugin, playHead, {}, justUnderTwoBeats).noteOns == std::vector<int> { 60, 62, 60, 62 });

        setParameter (*plugin, "p1_length", 3.0f);
        playHead.ppq = 0.0;
        CHECK (render (*plugin, playHead, {}, justUnderTwoBeats).noteOns == std::vector<int> { 60, 62, 64, 60 });
    }

    SECTION ("the drag handles can be automated")
    {
        playHead.playing = true;

        SECTION ("a lane length")
        {
            setParameter (*plugin, "p1_noteslength", 1.0f); // A's Notes lane repeats its first step
            CHECK (render (*plugin, playHead, {}, justUnderOneBeat).noteOns == std::vector<int> { 60, 77, 60, 79 });
        }

        SECTION ("the Rotation length")
        {
            setParameter (*plugin, "rotationlength", 1.0f); // A alone
            CHECK (render (*plugin, playHead, {}, justUnderOneBeat).noteOns == std::vector<int> { 60, 62, 64, 60 });
        }

        SECTION ("the loop length")
        {
            setParameter (*plugin, "looplength", 2.0f); // starts over every two sixteenths
            CHECK (render (*plugin, playHead, {}, justUnderOneBeat).noteOns == std::vector<int> { 60, 77, 60, 77 });
        }
    }

    plugin->releaseResources();
}

TEST_CASE ("parameters are grouped by pattern slot")
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    CHECK (plugin->getParameters().size() == 123); // under Ableton's 128, so it lists them all

    const auto groups = plugin->getParameterTree().getSubgroups (false);
    REQUIRE (groups.size() == 8);
    CHECK (groups[0]->getName() == "Pattern Slot 1");
    CHECK (groups[0]->getParameters (false).size() == 15);
}

TEST_CASE ("every parameter starts at its default value")
{
    // clap-validator's param-default-values test checks the same thing.
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    for (auto* parameter : plugin->getParameters())
    {
        INFO (parameter->getName (64));
        CHECK (parameter->getValue() == Catch::Approx (parameter->getDefaultValue()));
    }
}

TEST_CASE ("notes played in are kept in order for MIDI Learn")
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    auto& arp = dynamic_cast<patternarp::PatternArpProcessor&> (*plugin);
    FakePlayHead playHead;
    plugin->setPlayHead (&playHead);
    plugin->prepareToPlay (48000.0, 512);

    juce::MidiBuffer input;
    input.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0);
    input.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 10);
    input.addEvent (juce::MidiMessage::noteOn (1, 67, static_cast<juce::uint8> (0)), 15); // a note-off in disguise
    input.addEvent (juce::MidiMessage::noteOn (1, 67, 0.8f), 20);
    render (*plugin, playHead, input, 1);

    CHECK (arp.takePlayedNotes() == std::vector<int> { 64, 60, 67 });
    CHECK (arp.takePlayedNotes().empty());
    plugin->releaseResources();
}

TEST_CASE ("plugin state round-trips")
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioProcessor> original (createPluginFilter());
    setParameter (*original, "p1_timing", 5.0f); // 1/8
    setParameter (*original, "p1_notesource", 1.0f);
    {
        auto& arp = dynamic_cast<patternarp::PatternArpProcessor&> (*original);
        auto a = arp.getArrangement();
        a.setOrderLength (4);
        a.setQuiet (2, true);
        a.patterns[0].ccNumber = 74;
        a.patterns[0].steps[1].cc = 20;
        a.patterns[0].steps[2].repeat = 5;
        a.patterns[1].laneLength[static_cast<size_t> (patternarp::Lane::repeat)] = 3;
        a.patterns[1].laneMode[static_cast<size_t> (patternarp::Lane::octave)] = patternarp::LaneMode::note;
        a.patterns[1].laneMode[static_cast<size_t> (patternarp::Lane::cc)] = patternarp::LaneMode::note;
        arp.setArrangement (a);
    }

    juce::MemoryBlock state;
    original->getStateInformation (state);

    std::unique_ptr<juce::AudioProcessor> restored (createPluginFilter());
    restored->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    juce::MemoryBlock again;
    restored->getStateInformation (again);
    CHECK (state == again);

    for (auto* p : restored->getParameters())
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p); choice != nullptr && choice->getParameterID() == "p1_timing")
            CHECK (choice->getIndex() == 5);

    const auto a = dynamic_cast<patternarp::PatternArpProcessor&> (*restored).getArrangement();
    CHECK (a.orderLength == 4);
    CHECK (a.quiet[2]);
    CHECK_FALSE (a.quiet[0]);
    CHECK (a.patterns[0].ccNumber == 74);
    CHECK (a.patterns[0].steps[1].cc == 20);
    CHECK (a.patterns[1].ccNumber == -1);
    CHECK (plainValue (*restored, "p1_ccnumber") == 74);
    CHECK (a.patterns[0].steps[2].repeat == 5);
    CHECK (a.patterns[0].steps[1].repeat == 1);
    CHECK (plainValue (*restored, "p2_repeatlength") == 3);

    // Lanes in Note mode stay so; every other lane (and every lane of a save from before
    // Note mode) moves on per step.
    using patternarp::Lane, patternarp::LaneMode;
    CHECK (a.patterns[1].laneMode[static_cast<size_t> (Lane::octave)] == LaneMode::note);
    CHECK (a.patterns[1].laneMode[static_cast<size_t> (Lane::cc)] == LaneMode::note);
    CHECK (a.patterns[1].laneMode[static_cast<size_t> (Lane::velocity)] == LaneMode::step);
    for (const auto mode : a.patterns[0].laneMode)
        CHECK (mode == LaneMode::step);
}

TEST_CASE ("saves from before the CC lane keep their slot settings")
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioProcessor> original (createPluginFilter());
    setParameter (*original, "p2_timing", 5.0f);
    setParameter (*original, "p2_ratchetlength", 3.0f);
    setParameter (*original, "rotationlength", 1.0f);
    juce::MemoryBlock state;
    original->getStateInformation (state);

    // Those saves had no CC Length or CC Number at the end of each slot.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    auto values = juce::StringArray::fromTokens (xml->getStringAttribute ("slots"), ",", {});
    REQUIRE (values.size() == 8 * 15 + 2);
    for (int slot = 7; slot >= 0; --slot)
        values.removeRange (slot * 15 + 12, 3);
    xml->setAttribute ("slots", values.joinIntoString (","));
    state.reset();
    juce::AudioProcessor::copyXmlToBinary (*xml, state);

    std::unique_ptr<juce::AudioProcessor> restored (createPluginFilter());
    restored->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (plainValue (*restored, "p2_timing") == 5);
    CHECK (plainValue (*restored, "p2_ratchetlength") == 3);
    CHECK (plainValue (*restored, "rotationlength") == 1);
    CHECK (plainValue (*restored, "p1_ccnumber") == -1);
}

TEST_CASE ("saves from before the Repeat lane keep their CC settings and play every step once")
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioProcessor> original (createPluginFilter());
    setParameter (*original, "p2_cclength", 2.0f);
    setParameter (*original, "p2_ccnumber", 74.0f);
    juce::MemoryBlock state;
    original->getStateInformation (state);

    // Those saves had no Repeat Length at the end of each slot, and no repeat in each step.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    auto values = juce::StringArray::fromTokens (xml->getStringAttribute ("slots"), ",", {});
    REQUIRE (values.size() == 8 * 15 + 2);
    for (int slot = 7; slot >= 0; --slot)
        values.remove (slot * 15 + 14);
    xml->setAttribute ("slots", values.joinIntoString (","));

    auto* arrangement = xml->getChildByName ("Arrangement");
    REQUIRE (arrangement != nullptr);
    for (auto* pattern : arrangement->getChildIterator())
    {
        juce::StringArray steps;
        for (const auto& step : juce::StringArray::fromTokens (pattern->getStringAttribute ("steps"), ";", {}))
        {
            auto stepValues = juce::StringArray::fromTokens (step, " ", {});
            REQUIRE (stepValues.size() == 8);
            stepValues.remove (7);
            steps.add (stepValues.joinIntoString (" "));
        }
        pattern->setAttribute ("steps", steps.joinIntoString (";"));
    }
    state.reset();
    juce::AudioProcessor::copyXmlToBinary (*xml, state);

    std::unique_ptr<juce::AudioProcessor> restored (createPluginFilter());
    restored->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (plainValue (*restored, "p2_cclength") == 2);
    CHECK (plainValue (*restored, "p2_ccnumber") == 74);
    CHECK (plainValue (*restored, "p2_repeatlength") == 0);
    const auto loaded = dynamic_cast<patternarp::PatternArpProcessor&> (*restored).getArrangement(); // kept: looping over a temporary's member would dangle
    for (const auto& step : loaded.patterns[1].steps)
        CHECK (step.repeat == 1);
}

TEST_CASE ("saves from before 1/64 keep their timings and loop length")
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());

    // Version 4: timing 4 was 1/8 and loop lengths were in 24ths of a beat (24 = 1 beat).
    juce::String slots;
    for (int slot = 0; slot < 8; ++slot)
        slots << (slot == 0 ? "" : ",") << "4,4,0,0,0,1";
    const auto xml = juce::parseXML ("<PatternArpState version=\"4\" latch=\"0\" slots=\"" + slots + "\">"
                                     "<Arrangement loopLength=\"24\" order=\"0\" orderLength=\"1\">"
                                     "<Pattern timing=\"4\" length=\"4\" notes=\"60\"/></Arrangement></PatternArpState>");
    REQUIRE (xml != nullptr);
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary (*xml, state);
    plugin->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    juce::MemoryBlock saved;
    plugin->getStateInformation (saved);
    const auto reloaded = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (reloaded != nullptr);
    const auto* arrangement = reloaded->getChildByName ("Arrangement");
    REQUIRE (arrangement != nullptr);
    CHECK (arrangement->getIntAttribute ("loopLength") == patternarp::ticksPerBeat);
    REQUIRE (arrangement->getChildByName ("Pattern") != nullptr);
    CHECK (arrangement->getChildByName ("Pattern")->getIntAttribute ("timing") == static_cast<int> (patternarp::Timing::eighth));
    CHECK (reloaded->getStringAttribute ("slots").startsWith (juce::String (static_cast<int> (patternarp::Timing::eighth)) + ",4,"));
}

TEST_CASE ("saves from before the full set of triplets and dotted notes keep their timings")
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    using patternarp::Timing;

    // Loads a save of the given version whose two patterns (slots 1 and 2) and empty slots have
    // these timings, as that version numbered them, then checks what they are now.
    const auto check = [&plugin] (int version, int loopLength, const std::array<int, 8>& saved, const std::array<Timing, 8>& now) {
        juce::String slots;
        for (int slot = 0; slot < 8; ++slot)
            slots << (slot == 0 ? "" : ",") << saved[static_cast<size_t> (slot)] << ",4,0,0,0,1";
        const auto xml = juce::parseXML ("<PatternArpState version=\"" + juce::String (version) + "\" latch=\"0\" slots=\"" + slots + "\">"
                                         "<Arrangement loopLength=\"" + juce::String (loopLength) + "\" order=\"0,1\" orderLength=\"2\">"
                                         "<Pattern timing=\"" + juce::String (saved[0]) + "\" length=\"4\" notes=\"60\"/>"
                                         "<Pattern timing=\"" + juce::String (saved[1]) + "\" length=\"2\" notes=\"64\"/>"
                                         "</Arrangement></PatternArpState>");
        REQUIRE (xml != nullptr);
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary (*xml, state);
        plugin->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

        for (int slot = 0; slot < 8; ++slot)
        {
            juce::AudioParameterChoice* timing = nullptr;
            for (auto* p : plugin->getParameters())
                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p);
                    choice != nullptr && choice->getParameterID() == "p" + juce::String (slot + 1) + "_timing")
                    timing = choice;
            REQUIRE (timing != nullptr);
            CHECK (timing->getCurrentChoiceName() == patternarp::timingName (now[static_cast<size_t> (slot)]));
        }

        juce::MemoryBlock out;
        plugin->getStateInformation (out);
        const auto reloaded = juce::AudioProcessor::getXmlFromBinary (out.getData(), static_cast<int> (out.getSize()));
        REQUIRE (reloaded != nullptr);
        const auto* arrangement = reloaded->getChildByName ("Arrangement");
        REQUIRE (arrangement != nullptr);
        CHECK (arrangement->getIntAttribute ("loopLength") == 2 * patternarp::ticksPerBeat); // two beats
        const auto* first = arrangement->getChildByName ("Pattern");
        REQUIRE (first != nullptr);
        CHECK (first->getIntAttribute ("timing") == static_cast<int> (now[0]));
        REQUIRE (first->getNextElement() != nullptr);
        CHECK (first->getNextElement()->getIntAttribute ("timing") == static_cast<int> (now[1]));
    };

    SECTION ("version 5: 1/64, 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2, 1/1, at 48 ticks a beat")
    {
        check (5, 96, { 4, 9, 0, 1, 2, 5, 6, 8 },
               { Timing::eighthTriplet, Timing::whole, Timing::sixtyFourth, Timing::thirtySecond, Timing::sixteenthTriplet,
                 Timing::eighth, Timing::quarterTriplet, Timing::half });
    }

    SECTION ("version 6: those with five dotted timings in between")
    {
        check (6, 96, { 3, 11, 6, 9, 13, 14, 4, 7 },
               { Timing::thirtySecondDotted, Timing::quarterDotted, Timing::sixteenthDotted, Timing::eighthDotted,
                 Timing::halfDotted, Timing::whole, Timing::sixteenth, Timing::eighth });
    }
}

TEST_CASE ("plugin editor opens")
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());

    // Set PATTERNARP_STATE to a saved-state .xml file to open the editor on that state.
    if (const auto statePath = juce::SystemStats::getEnvironmentVariable ("PATTERNARP_STATE", {}); statePath.isNotEmpty())
        if (const auto xml = juce::parseXML (juce::File (statePath)))
        {
            juce::MemoryBlock state;
            juce::AudioProcessor::copyXmlToBinary (*xml, state);
            plugin->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        }

    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorAndMakeActive());
    REQUIRE (editor != nullptr);
    CHECK (editor->getWidth() > 0);

    // Set PATTERNARP_OPEN_ROTATION to open the Rotation lane first.
    if (juce::SystemStats::getEnvironmentVariable ("PATTERNARP_OPEN_ROTATION", {}).isNotEmpty())
        for (auto* child : editor->getChildren())
            if (auto* lane = dynamic_cast<patternarp::OrderLane*> (child))
                lane->setExpanded (true);

    // Set PATTERNARP_SNAPSHOT to a .png path to save a picture of the editor.
    if (const auto path = juce::SystemStats::getEnvironmentVariable ("PATTERNARP_SNAPSHOT", {}); path.isNotEmpty())
    {
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        const juce::File file (path);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
    }

    plugin->editorBeingDeleted (editor.get());
}

TEST_CASE ("every name in the open Rotation lane fits between its play button and the steps")
{
    // Each platform has its own font: the Mac's and Linux's fit as they are, while Windows'
    // (Verdana) is wider, so its longest names are narrowed a little.
    juce::ScopedJuceInitialiser_GUI juce;
    for (int letter = 0; letter < 26; ++letter)
    {
        const auto name = juce::String ("Pattern ") + patternarp::patternLetter (letter);
        const int width = patternarp::theme::textWidth (patternarp::theme::font (14.0f, true), name);
        INFO (name << " is " << width << "px wide");
        CHECK (static_cast<float> (width) * patternarp::OrderLane::nameMinScale <= static_cast<float> (patternarp::OrderLane::nameWidth));
    }
}

// Sends real clicks through a window, so it runs on Linux (Xvfb in CI) only: macOS drops
// clicks on a window that is not frontmost on the actual screen.
#if JUCE_LINUX
namespace
{
// Bare Xvfb has no window manager, and JUCE's window setup sets a property that then fails.
// Xlib's default handler exits on that, so ignore X errors for this test.
void ignoreXErrors()
{
    using Handler = int (*) (void*, void*);
    using SetHandler = Handler (*) (Handler);
    if (auto* x11 = dlopen ("libX11.so.6", RTLD_LAZY))
        if (auto setHandler = reinterpret_cast<SetHandler> (dlsym (x11, "XSetErrorHandler")))
            setHandler ([] (void*, void*) { return 0; });
}
} // namespace

// The editor in a real window, driven by mouse events sent to its window.
struct WindowedEditor
{
    explicit WindowedEditor (const char* stateXml = nullptr)
    {
        ignoreXErrors();
        if (stateXml != nullptr)
        {
            juce::MemoryBlock state;
            juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (juce::String (stateXml)), state);
            plugin->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        }

        editor.reset (plugin->createEditorAndMakeActive());
        editor->addToDesktop (0);
        editor->setVisible (true);
        peer = editor->getPeer();
    }

    ~WindowedEditor() { plugin->editorBeingDeleted (editor.get()); }

    juce::Component* find (const juce::String& name) const
    {
        juce::Component* found = nullptr;
        std::function<void (juce::Component&)> search = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (child->getName() == name)
                    found = child;
                search (*child);
            }
        };
        search (*editor);
        return found;
    }

    template <typename Parameter>
    Parameter* parameter (const juce::String& id) const
    {
        for (auto* p : plugin->getParameters())
            if (auto* typed = dynamic_cast<Parameter*> (p); typed != nullptr && typed->getParameterID() == id)
                return typed;
        return nullptr;
    }

    void mouse (juce::Point<float> position, bool down)
    {
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, position,
                                down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(),
                                juce::MouseInputSource::defaultPressure, 0.0f, time += 100);
    }

    void click (juce::Point<float> position)
    {
        mouse (position, true);
        mouse (position, false);
        time += 1000; // so the next click is not a double-click
    }

    void doubleClick (juce::Point<float> position)
    {
        for (int i = 0; i < 2; ++i)
        {
            mouse (position, true);
            mouse (position, false);
        }
        time += 1000;
    }

    std::unique_ptr<juce::XmlElement> state() const
    {
        juce::MemoryBlock block;
        plugin->getStateInformation (block);
        return juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize()));
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<juce::AudioProcessor> plugin { createPluginFilter() };
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::ComponentPeer* peer = nullptr;
    juce::int64 time = juce::Time::currentTimeMillis();
};

TEST_CASE ("clicking a pattern's play button pauses and resumes it")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* panel = dynamic_cast<patternarp::PatternPanel*> (w.find ("Pattern A"));
    auto* play = w.parameter<juce::AudioParameterBool> ("p1_play");
    REQUIRE (panel != nullptr);
    REQUIRE (play != nullptr);

    const auto button = w.editor->getLocalPoint (panel, panel->getPlayButtonBounds().getCentre().toFloat());

    CHECK (play->get());
    w.click (button);
    CHECK_FALSE (play->get());
    w.click (button);
    CHECK (play->get());
}

TEST_CASE ("clicking a value box opens its list")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* timing = w.find ("Step");
    REQUIRE (timing != nullptr);

    // Click the value, right of the caption (see ValueBox.cpp).
    w.click (w.editor->getLocalPoint (timing, juce::Point<float> (static_cast<float> (timing->getWidth()) - 20.0f,
                                                                  static_cast<float> (timing->getHeight()) / 2.0f)));
    CHECK (juce::PopupMenu::dismissAllActiveMenus());
}

TEST_CASE ("the Rotation lane opens on a click and paints steps")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* lane = w.find ("Rotation");
    REQUIRE (lane != nullptr);

    const int closedHeight = lane->getHeight();
    w.click (w.editor->getLocalPoint (lane, lane->getLocalBounds().getCentre().toFloat()));
    CHECK (lane->getHeight() > closedHeight);

    // Give the first step to pattern B: the grid starts right of the row labels, below the
    // header (40) and an 8-pixel margin, with a row per pattern (see OrderLane.cpp).
    constexpr auto gridX = static_cast<float> (patternarp::OrderLane::labelWidth);
    constexpr auto stepWidth = static_cast<float> (patternarp::OrderLane::stepPitch);
    const float rowB = 40.0f + 8.0f + static_cast<float> (patternarp::OrderLane::rowPitch) * 1.5f;
    w.click (w.editor->getLocalPoint (lane, juce::Point<float> (gridX + 10.0f, rowB)));

    const auto xml = w.state();
    REQUIRE (xml != nullptr);
    const auto* arrangement = xml->getChildByName ("Arrangement");
    REQUIRE (arrangement != nullptr);
    CHECK (arrangement->getStringAttribute ("order") == "1,1");

    // Clicking B's lit steps quiets them; once both are quiet, B is paused.
    auto* play = w.parameter<juce::AudioParameterBool> ("p2_play");
    REQUIRE (play != nullptr);
    w.click (w.editor->getLocalPoint (lane, juce::Point<float> (gridX + stepWidth * 0.5f, rowB)));
    CHECK (play->get());
    w.click (w.editor->getLocalPoint (lane, juce::Point<float> (gridX + stepWidth * 1.5f, rowB)));
    CHECK_FALSE (play->get());

    // And clicking one again plays B with just that step lit.
    w.click (w.editor->getLocalPoint (lane, juce::Point<float> (gridX + stepWidth * 1.5f, rowB)));
    CHECK (play->get());
    CHECK (w.state()->getChildByName ("Arrangement")->getStringAttribute ("quiet") == "0");
}

TEST_CASE ("each row of the open Rotation lane has a play button, and its name works it too")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* lane = dynamic_cast<patternarp::OrderLane*> (w.find ("Rotation"));
    auto* play = w.parameter<juce::AudioParameterBool> ("p2_play");
    REQUIRE (lane != nullptr);
    REQUIRE (play != nullptr);
    lane->setExpanded (true);

    // Pattern B's row: below the header (40) and an 8-pixel margin, with a 24-pixel button at
    // the left padding and the name after it (see OrderLane.cpp).
    const float rowB = 40.0f + 8.0f + static_cast<float> (patternarp::OrderLane::rowPitch) * 1.5f;
    const auto button = w.editor->getLocalPoint (lane, juce::Point<float> (8.0f + 12.0f, rowB));
    const auto name = w.editor->getLocalPoint (lane, juce::Point<float> (patternarp::OrderLane::nameX + 10.0f, rowB));

    REQUIRE (play->get());
    w.click (button);
    CHECK_FALSE (play->get());
    CHECK (w.parameter<juce::AudioParameterBool> ("p1_play")->get()); // only B's row
    w.click (name);
    CHECK (play->get());
    w.click (name);
    CHECK_FALSE (play->get());
    w.click (button);
    CHECK (play->get());

    // Past the end of the name, the label column does nothing.
    w.click (w.editor->getLocalPoint (lane, juce::Point<float> (patternarp::OrderLane::labelWidth - 2.0f, rowB)));
    CHECK (play->get());
    CHECK (lane->isExpanded());
}

TEST_CASE ("arming MIDI Learn switches a MIDI Thru pattern to Piano Roll")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* panel = w.find ("Pattern A");
    auto* source = w.parameter<juce::AudioParameterChoice> ("p1_notesource");
    REQUIRE (panel != nullptr);
    REQUIRE (source != nullptr);
    auto* keyboard = dynamic_cast<patternarp::NoteKeyboard*> (panel->findChildWithID ("keyboard"));
    REQUIRE (keyboard != nullptr);

    w.click (w.editor->getLocalPoint (keyboard, keyboard->sourceTab (patternarp::NoteSource::midiIn).getCentre().toFloat()));
    REQUIRE (source->getIndex() == 1);

    w.click (w.editor->getLocalPoint (keyboard, keyboard->learnArea().getCentre().toFloat()));
    CHECK (source->getIndex() == 0);
}

TEST_CASE ("keys clicked into an empty Piano Roll fill the Notes lane in that order")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* panel = w.find ("Pattern A");
    REQUIRE (panel != nullptr);
    auto* keyboard = dynamic_cast<patternarp::NoteKeyboard*> (panel->findChildWithID ("keyboard"));
    REQUIRE (keyboard != nullptr);
    auto& arp = dynamic_cast<patternarp::PatternArpProcessor&> (*w.plugin);

    // Low on a white key, clear of the black ones.
    const auto clickKey = [&] (int note) {
        const auto key = keyboard->keyBounds (note);
        w.click (w.editor->getLocalPoint (keyboard, juce::Point<float> (key.getCentreX(), key.getBottom() - 4.0f)));
    };

    // The pitch each of Pattern A's steps plays.
    const auto stepPitches = [&arp] {
        const auto a = arp.getArrangement();
        const auto& p = a.patterns[0];
        std::vector<int> enabled, pitches;
        for (int n = 0; n < 128; ++n)
            if (p.enabledNotes.test (static_cast<size_t> (n)))
                enabled.push_back (n);
        for (int i = 0; i < p.length && ! enabled.empty(); ++i)
            pitches.push_back (enabled[static_cast<size_t> (juce::roundToInt (p.steps[static_cast<size_t> (i)].note * static_cast<float> (enabled.size() - 1)))]);
        return pitches;
    };

    for (int note : { 60, 62, 64 }) // Pattern A starts on C3 D3 E3: switch them off
        clickKey (note);
    REQUIRE (arp.getArrangement().patterns[0].enabledNotes.none());

    clickKey (67); // G3
    clickKey (60); // C3
    CHECK (stepPitches() == std::vector<int> { 67, 60, 67 });

    clickKey (65); // F3
    clickKey (72); // C4: past the last step, so it only joins the Piano Roll
    CHECK (stepPitches() == std::vector<int> { 67, 60, 65 });
    CHECK (arp.getArrangement().patterns[0].enabledNotes.count() == 4);
}

TEST_CASE ("double-clicking a lane tab resets that lane")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* panel = dynamic_cast<patternarp::PatternPanel*> (w.find ("Pattern A"));
    REQUIRE (panel != nullptr);

    // Pattern A's notes as saved: the first number of each step.
    const auto notes = [&w] {
        juce::StringArray result;
        if (const auto xml = w.state())
            if (const auto* arrangement = xml->getChildByName ("Arrangement"))
                if (const auto* pattern = arrangement->getChildByName ("Pattern"))
                    for (const auto& step : juce::StringArray::fromTokens (pattern->getStringAttribute ("steps"), ";", {}))
                        result.add (juce::String (step.upToFirstOccurrenceOf (" ", false, false).getFloatValue()));
        return result;
    };
    REQUIRE (notes().joinIntoString (",").startsWith ("0,0.5,1"));

    w.doubleClick (w.editor->getLocalPoint (panel, panel->getTabBounds (patternarp::Lane::notes).getCentre().toFloat()));
    const auto after = notes();
    REQUIRE (! after.isEmpty());
    for (const auto& note : after)
        CHECK (note == "0");
}

TEST_CASE ("the dice randomizes the open lane and leaves the others alone")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* panel = dynamic_cast<patternarp::PatternPanel*> (w.find ("Pattern A"));
    REQUIRE (panel != nullptr);
    auto* dice = panel->findChildWithID ("dice");
    REQUIRE (dice != nullptr);

    // Pattern A's steps as saved: "note velocity gate ..." each.
    const auto steps = [&w] {
        if (const auto xml = w.state())
            if (const auto* arrangement = xml->getChildByName ("Arrangement"))
                if (const auto* pattern = arrangement->getChildByName ("Pattern"))
                    return juce::StringArray::fromTokens (pattern->getStringAttribute ("steps"), ";", {});
        return juce::StringArray();
    };
    const auto field = [] (const juce::String& step, int i) { return juce::StringArray::fromTokens (step, " ", {})[i]; };
    const auto before = steps();
    REQUIRE (! before.isEmpty());

    w.click (w.editor->getLocalPoint (panel, panel->getTabBounds (patternarp::Lane::velocity).getCentre().toFloat()));
    w.click (w.editor->getLocalPoint (dice, dice->getLocalBounds().getCentre().toFloat()));

    const auto after = steps();
    REQUIRE (after.size() == before.size());
    bool velocityChanged = false;
    for (int i = 0; i < after.size(); ++i)
    {
        CHECK (field (after[i], 0) == field (before[i], 0)); // notes untouched
        velocityChanged = velocityChanged || field (after[i], 1) != field (before[i], 1);
    }
    CHECK (velocityChanged);
}

TEST_CASE ("the Chance tab opens its list from its value, and the CC tab a field")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* panel = dynamic_cast<patternarp::PatternPanel*> (w.find ("Pattern A"));
    REQUIRE (panel != nullptr);
    auto* graph = dynamic_cast<patternarp::LaneGraph*> (panel->findChildWithID ("graph"));
    REQUIRE (graph != nullptr);

    // The name opens the tab...
    w.click (w.editor->getLocalPoint (panel, panel->getTabBounds (patternarp::Lane::chance).withWidth (12).getCentre().toFloat()));
    CHECK (graph->getLane() == patternarp::Lane::chance);
    CHECK_FALSE (juce::PopupMenu::dismissAllActiveMenus());

    // ...and the value ("Rest") opens it with its list.
    w.click (w.editor->getLocalPoint (panel, panel->getTabValueBounds (patternarp::Lane::chance).getCentre().toFloat()));
    CHECK (juce::PopupMenu::dismissAllActiveMenus());

    // "CC: Off" opens a field to type the number in: Return sets it, and an empty field
    // turns the lane off again. (The field passes Return and Escape on asynchronously, so
    // they are called here directly.)
    auto* field = dynamic_cast<juce::TextEditor*> (panel->findChildWithID ("ccField"));
    REQUIRE (field != nullptr);
    const auto ccNumber = [&w] { return w.state()->getChildByName ("Arrangement")->getChildByName ("Pattern")->getIntAttribute ("ccNumber"); };
    const auto openField = [&] {
        w.click (w.editor->getLocalPoint (panel, panel->getTabValueBounds (patternarp::Lane::cc).getCentre().toFloat()));
        CHECK (graph->getLane() == patternarp::Lane::cc);
        CHECK (field->isVisible());
    };

    openField();
    CHECK (field->getText().isEmpty()); // showing "Enter MIDI CC"
    field->setText ("74");
    field->onReturnKey();
    CHECK_FALSE (field->isVisible());
    CHECK (ccNumber() == 74);
    CHECK (w.parameter<juce::AudioParameterInt> ("p1_ccnumber")->get() == 74);

    openField();
    CHECK (field->getText() == "74");
    field->setText ("12");
    field->onEscapeKey();
    CHECK (ccNumber() == 74);

    openField();
    field->setText ({});
    field->onReturnKey();
    CHECK (ccNumber() == -1);
}

TEST_CASE ("the Velocity, Gate, Octave and CC tabs show how their lane moves on, and open a list for it")
{
    using patternarp::Lane;
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* panel = dynamic_cast<patternarp::PatternPanel*> (w.find ("Pattern A"));
    REQUIRE (panel != nullptr);
    auto* graph = dynamic_cast<patternarp::LaneGraph*> (panel->findChildWithID ("graph"));
    REQUIRE (graph != nullptr);

    for (const auto lane : { Lane::velocity, Lane::gate, Lane::octave })
        CHECK_FALSE (panel->getTabModeBounds (lane).isEmpty());
    for (const auto lane : { Lane::notes, Lane::ratchet, Lane::repeat, Lane::chance })
        CHECK (panel->getTabModeBounds (lane).isEmpty());

    // "Velocity: Step" opens its list from the mode.
    w.click (w.editor->getLocalPoint (panel, panel->getTabModeBounds (Lane::velocity).getCentre().toFloat()));
    CHECK (graph->getLane() == Lane::velocity);
    CHECK (juce::PopupMenu::dismissAllActiveMenus());

    // CC shows its mode only once it sends something, after the number, which still opens
    // its field.
    CHECK (panel->getTabModeBounds (Lane::cc).isEmpty());
    auto* field = dynamic_cast<juce::TextEditor*> (panel->findChildWithID ("ccField"));
    REQUIRE (field != nullptr);
    w.click (w.editor->getLocalPoint (panel, panel->getTabValueBounds (Lane::cc).getCentre().toFloat()));
    REQUIRE (field->isVisible());
    field->setText ("74");
    field->onReturnKey();

    const auto value = panel->getTabValueBounds (Lane::cc), mode = panel->getTabModeBounds (Lane::cc);
    REQUIRE_FALSE (mode.isEmpty());
    CHECK (value.getRight() <= mode.getX());
    CHECK (mode.getRight() == panel->getTabBounds (Lane::cc).getRight());
    w.click (w.editor->getLocalPoint (panel, mode.getCentre().toFloat()));
    CHECK_FALSE (field->isVisible());
    CHECK (juce::PopupMenu::dismissAllActiveMenus());
    w.click (w.editor->getLocalPoint (panel, value.getCentre().toFloat()));
    CHECK (field->isVisible());
    field->onEscapeKey();
}

TEST_CASE ("right-clicking a step opens its menu")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* panel = w.find ("Pattern A");
    REQUIRE (panel != nullptr);
    auto* graph = panel->findChildWithID ("graph");
    REQUIRE (graph != nullptr);

    const auto point = w.editor->getLocalPoint (graph, juce::Point<float> (20.0f, 100.0f)); // the first step
    w.peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point, juce::ModifierKeys::rightButtonModifier,
                              juce::MouseInputSource::defaultPressure, 0.0f, w.time += 100);
    w.peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point, juce::ModifierKeys(),
                              juce::MouseInputSource::defaultPressure, 0.0f, w.time += 100);
    CHECK (juce::PopupMenu::dismissAllActiveMenus());
}

TEST_CASE ("Make polyphonic adds a sub-pattern under its pattern, with no row in the Rotation lane")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* panelA = dynamic_cast<patternarp::PatternPanel*> (w.find ("Pattern A"));
    auto* lane = dynamic_cast<patternarp::OrderLane*> (w.find ("Rotation"));
    REQUIRE (panelA != nullptr);
    REQUIRE (lane != nullptr);
    lane->setExpanded (true);
    const int laneHeight = lane->getPreferredHeight();

    REQUIRE (panelA->getMakePolyphonicButton().onClick != nullptr);
    panelA->getMakePolyphonicButton().onClick();

    auto& arp = dynamic_cast<patternarp::PatternArpProcessor&> (*w.plugin);
    const auto a = arp.getArrangement();
    REQUIRE (a.numPatterns == 3);
    CHECK (a.patterns[1].parent == 0);
    CHECK (lane->getPreferredHeight() == laneHeight);

    auto* sub = dynamic_cast<patternarp::PatternPanel*> (w.find ("Pattern C"));
    auto* panelB = dynamic_cast<patternarp::PatternPanel*> (w.find ("Pattern B"));
    REQUIRE (sub != nullptr);
    REQUIRE (panelB != nullptr);
    CHECK (sub->getY() > panelA->getY());
    CHECK (sub->getY() < panelB->getY()); // between A and B
    CHECK (sub->getX() > panelA->getX()); // set in

    // Cloning the sub-pattern puts the copy under A too.
    sub->getCloneButton().onClick();
    const auto b = arp.getArrangement();
    REQUIRE (b.numPatterns == 4);
    CHECK (b.patterns[2].parent == 0);
    CHECK (b.patterns[3].parent == -1);
}

#endif

TEST_CASE ("sub-patterns are saved and play along with their parent")
{
    juce::ScopedJuceInitialiser_GUI juce;
    std::unique_ptr<juce::AudioProcessor> original (createPluginFilter());
    {
        auto& arp = dynamic_cast<patternarp::PatternArpProcessor&> (*original);
        auto a = arp.getArrangement();
        REQUIRE (a.addSubPattern (0, patternarp::makeSubPattern (a.patterns[0])) == 1);
        arp.setArrangement (a);
    }

    juce::MemoryBlock state;
    original->getStateInformation (state);
    std::unique_ptr<juce::AudioProcessor> restored (createPluginFilter());
    restored->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    const auto a = dynamic_cast<patternarp::PatternArpProcessor&> (*restored).getArrangement();
    REQUIRE (a.numPatterns == 3);
    CHECK (a.patterns[1].parent == 0);
    CHECK (a.patterns[2].parent == -1);
    CHECK (a.orderLength == 2);
    CHECK (a.order[0] == 0);
    CHECK (a.order[1] == 2); // B, which moved down a place

    // The default A (C3 D3 E3) with the new sub-pattern's C4 E4 G4 E4 over it, B between.
    FakePlayHead playHead;
    playHead.playing = true;
    restored->setPlayHead (&playHead);
    restored->prepareToPlay (48000.0, 512);
    const auto out = render (*restored, playHead, {}, 64); // a little over a beat
    REQUIRE (out.noteOns.size() >= 6);
    CHECK (std::vector<int> (out.noteOns.begin(), out.noteOns.begin() + 6) == std::vector<int> { 60, 72, 77, 62, 76, 79 });
    restored->releaseResources();
}
