#pragma once

#include "patternarp/Pattern.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <bitset>
#include <functional>

namespace patternarp
{

// The Notes Enabled strip: a Piano Roll | MIDI Thru switch, a MIDI Learn button and a piano
// keyboard from C0 to C7 (named the way Bitwig, Ableton and Logic name them: MIDI note 60
// is C3).
//
// In Piano Roll mode clicking (or dragging across) keys turns them on and off. In MIDI Thru
// mode the keyboard only shows the notes being played into the plugin. MIDI Learn arms the
// keyboard, pulsing red, so notes played in are added (the panel does the adding); click it
// again when done.
class NoteKeyboard final : public juce::Component, private juce::Timer
{
public:
    NoteKeyboard();

    void setNotes (const std::bitset<128>& enabled, NoteSource source);
    void setInputNotes (const std::bitset<128>& notes);
    void setColour (juce::Colour c) { colour = c; repaint(); }

    std::function<void (const std::bitset<128>&)> onNotesChange;
    std::function<void (NoteSource)> onSourceChange;
    std::function<void (bool)> onLearnChange; // armed or not

    bool isLearning() const { return learning; }
    void setLearning (bool shouldLearn);

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    static constexpr int lowestNote = 24;  // C0
    static constexpr int highestNote = 108; // C7

private:
    void timerCallback() override { repaint (learnArea()); } // the pulse

    juce::Rectangle<int> switchArea() const; // Piano Roll over MIDI Thru
    juce::Rectangle<int> learnArea() const;
    juce::Rectangle<int> keysArea() const;

    float whiteKeyWidth() const;
    juce::Rectangle<float> keyBounds (int note) const;
    int noteAt (juce::Point<float> p) const;

    std::bitset<128> enabled;
    std::bitset<128> input;
    NoteSource source = NoteSource::select;
    juce::Colour colour;
    bool learning = false;

    bool paintingKeys = false;
    bool paintValue = true;
    int lastPainted = -1;
};

} // namespace patternarp
