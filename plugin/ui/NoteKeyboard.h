#pragma once

#include "patternarp/Pattern.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <bitset>
#include <functional>

namespace patternarp
{

// The Notes Enabled strip: a Select | MIDI In switch, a piano keyboard and a scrollbar.
//
// In Select mode clicking (or dragging across) keys turns them on and off. In MIDI In mode
// the keyboard only shows the notes being played into the plugin. The scrollbar scrolls
// across C1..C8; dragging its ends, or the scroll wheel over the keys, zooms.
class NoteKeyboard final : public juce::Component
{
public:
    NoteKeyboard();

    void setNotes (const std::bitset<128>& enabled, NoteSource source);
    void setInputNotes (const std::bitset<128>& notes);
    void setColour (juce::Colour c) { colour = c; repaint(); }

    std::function<void (const std::bitset<128>&)> onNotesChange;
    std::function<void (NoteSource)> onSourceChange;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    static constexpr int lowestNote = 24;  // C1
    static constexpr int highestNote = 108; // C8

private:
    juce::Rectangle<int> switchArea() const;
    juce::Rectangle<int> keysArea() const;
    juce::Rectangle<int> scrollArea() const;
    juce::Rectangle<float> thumbArea() const;

    // The view is measured in white keys counted from C1.
    float whiteKeyWidth() const;
    juce::Rectangle<float> keyBounds (int note) const;
    int noteAt (juce::Point<float> p) const;
    void setView (float first, float count);

    std::bitset<128> enabled;
    std::bitset<128> input;
    NoteSource source = NoteSource::select;
    juce::Colour colour;

    float viewFirst = 21.0f; // C4
    float viewCount = 15.0f; // two octaves

    enum class Drag
    {
        none,
        keys,
        thumb,
        thumbStart,
        thumbEnd
    };
    Drag drag = Drag::none;
    bool paintValue = true;
    int lastPainted = -1;
    float dragStartFirst = 0.0f, dragStartCount = 0.0f;
};

} // namespace patternarp
