// Drives the real plugin processor like a host would: held notes, a playing transport,
// then a stopped one, host automation and saved state, checking the MIDI that comes out.

#include "PluginProcessor.h"

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

    plugin->releaseResources();
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
    CHECK (arrangement->getIntAttribute ("loopLength") == 48);
    REQUIRE (arrangement->getChildByName ("Pattern") != nullptr);
    CHECK (arrangement->getChildByName ("Pattern")->getIntAttribute ("timing") == 5);
    CHECK (reloaded->getStringAttribute ("slots").startsWith ("5,4,"));
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
    auto* panel = w.find ("Pattern A");
    auto* play = w.parameter<juce::AudioParameterBool> ("p1_play");
    REQUIRE (panel != nullptr);
    REQUIRE (play != nullptr);

    // The play button sits at the left of the panel's header (see PatternPanel::resized).
    const auto button = w.editor->getLocalPoint (panel, juce::Point<float> (38.0f, 55.0f));

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
    auto* timing = w.find ("Timing");
    REQUIRE (timing != nullptr);

    // Click the value, left of the arrows and below the caption.
    w.click (w.editor->getLocalPoint (timing, juce::Point<float> (20.0f, static_cast<float> (timing->getHeight()) - 12.0f)));
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

    // Give the first step to pattern B: the grid starts right of the row labels (116), below
    // the header (40) and an 8-pixel margin, with a 28-pixel row per pattern (see OrderLane.cpp).
    w.click (w.editor->getLocalPoint (lane, juce::Point<float> (116.0f + 10.0f, 40.0f + 8.0f + 28.0f * 1.5f)));

    const auto xml = w.state();
    REQUIRE (xml != nullptr);
    const auto* arrangement = xml->getChildByName ("Arrangement");
    REQUIRE (arrangement != nullptr);
    CHECK (arrangement->getStringAttribute ("order") == "1,1");

    // Clicking B's lit steps quiets them; once both are quiet, B is paused.
    auto* play = w.parameter<juce::AudioParameterBool> ("p2_play");
    REQUIRE (play != nullptr);
    const float stepWidth = std::min (40.0f, (static_cast<float> (lane->getWidth()) - 116.0f - 12.0f) / 16.0f);
    const float rowB = 40.0f + 8.0f + 28.0f * 1.5f;
    w.click (w.editor->getLocalPoint (lane, juce::Point<float> (116.0f + stepWidth * 0.5f, rowB)));
    CHECK (play->get());
    w.click (w.editor->getLocalPoint (lane, juce::Point<float> (116.0f + stepWidth * 1.5f, rowB)));
    CHECK_FALSE (play->get());

    // And clicking one again plays B with just that step lit.
    w.click (w.editor->getLocalPoint (lane, juce::Point<float> (116.0f + stepWidth * 1.5f, rowB)));
    CHECK (play->get());
    CHECK (w.state()->getChildByName ("Arrangement")->getStringAttribute ("quiet") == "0");
}

TEST_CASE ("arming MIDI Learn switches a MIDI Thru pattern to Piano Roll")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* panel = w.find ("Pattern A");
    auto* source = w.parameter<juce::AudioParameterChoice> ("p1_notesource");
    REQUIRE (panel != nullptr);
    REQUIRE (source != nullptr);

    // The keyboard row starts 304 pixels down the panel, with the Piano Roll / MIDI Thru
    // switch at its left and the Learn button under it (see PatternPanel::resized and
    // NoteKeyboard.cpp).
    w.click (w.editor->getLocalPoint (panel, juce::Point<float> (60.0f, 304.0f + 37.0f)));
    REQUIRE (source->getIndex() == 1);

    w.click (w.editor->getLocalPoint (panel, juce::Point<float> (60.0f, 304.0f + 67.0f)));
    CHECK (source->getIndex() == 0);
}

TEST_CASE ("double-clicking a lane tab resets that lane")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* panel = w.find ("Pattern A");
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

    // The Notes tab is the first one, on top of the graph (see PatternPanel::resized).
    w.doubleClick (w.editor->getLocalPoint (panel, juce::Point<float> (40.0f, 104.0f)));
    const auto after = notes();
    REQUIRE (! after.isEmpty());
    for (const auto& note : after)
        CHECK (note == "0");
}

TEST_CASE ("the Velocity default drags up and down and sets every step")
{
    // Start with pattern A's second step at velocity 40.
    juce::String stateXml;
    {
        WindowedEditor original;
        auto xml = original.state();
        REQUIRE (xml != nullptr);
        auto* pattern = xml->getChildByName ("Arrangement")->getChildByName ("Pattern");
        auto steps = juce::StringArray::fromTokens (pattern->getStringAttribute ("steps"), ";", {});
        auto second = juce::StringArray::fromTokens (steps[1], " ", {});
        second.set (1, "40");
        steps.set (1, second.joinIntoString (" "));
        pattern->setAttribute ("steps", steps.joinIntoString (";"));
        stateXml = xml->toString();
    }

    WindowedEditor w (stateXml.toRawUTF8());
    REQUIRE (w.peer != nullptr);
    auto* panel = w.find ("Pattern A");
    REQUIRE (panel != nullptr);

    // Open the Velocity tab, the second one on top of the graph (see PatternPanel::layoutTabs).
    w.click (w.editor->getLocalPoint (panel, juce::Point<float> (120.0f, 104.0f)));
    auto* value = panel->findChildWithID ("defaultValue");
    REQUIRE (value != nullptr);
    REQUIRE (value->isVisible());

    // Pattern A's default velocity, then each step's velocity.
    const auto velocities = [&w] {
        std::vector<int> result;
        const auto xml = w.state();
        if (const auto* arrangement = xml != nullptr ? xml->getChildByName ("Arrangement") : nullptr)
            if (const auto* pattern = arrangement->getChildByName ("Pattern"))
            {
                result.push_back (pattern->getIntAttribute ("defaultVelocity"));
                for (const auto& step : juce::StringArray::fromTokens (pattern->getStringAttribute ("steps"), ";", {}))
                    result.push_back (juce::StringArray::fromTokens (step, " ", {})[1].getIntValue());
            }
        return result;
    };
    REQUIRE (velocities().size() > 3);
    REQUIRE (velocities()[0] == 100);
    REQUIRE (velocities()[2] == 40);

    // Up 20 pixels is 10 more.
    const auto start = w.editor->getLocalPoint (value, value->getLocalBounds().getCentre().toFloat());
    w.mouse (start, true);
    for (int i = 1; i <= 4; ++i)
        w.mouse (start.translated (0.0f, -5.0f * static_cast<float> (i)), true);
    w.mouse (start.translated (0.0f, -20.0f), false);
    for (const int velocity : velocities())
        CHECK (velocity == 110);
}

TEST_CASE ("right-clicking a step opens its menu")
{
    WindowedEditor w;
    REQUIRE (w.peer != nullptr);
    auto* panel = w.find ("Pattern A");
    REQUIRE (panel != nullptr);

    const auto point = w.editor->getLocalPoint (panel, juce::Point<float> (60.0f, 200.0f)); // the first step
    w.peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point, juce::ModifierKeys::rightButtonModifier,
                              juce::MouseInputSource::defaultPressure, 0.0f, w.time += 100);
    w.peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point, juce::ModifierKeys(),
                              juce::MouseInputSource::defaultPressure, 0.0f, w.time += 100);
    CHECK (juce::PopupMenu::dismissAllActiveMenus());
}
#endif
