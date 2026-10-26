#pragma once

#include "LaneGraph.h"
#include "NoteKeyboard.h"
#include "ValueBox.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace patternarp
{

// One pattern: its settings row, the lane tabs and graph, and the Notes Enabled keyboard.
// New / Clone / Delete appear while the mouse is over the panel.
class PatternPanel final : public juce::Component, private juce::Timer
{
public:
    PatternPanel();

    void setPattern (const Pattern& pattern, int index, bool canAdd);
    void setInputNotes (const std::bitset<128>& notes) { keyboard.setInputNotes (notes); }

    std::function<void (const Pattern&)> onChange;
    std::function<void()> onNew, onClone, onDelete;

    static constexpr int preferredHeight = 408;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void changed();
    void updateHoverButtons();
    void layoutTabs();
    void setDefault (Lane lane, int value);

    Pattern pattern;
    int index = 0;

    ValueBox timing { "Timing" }, length { "Length" }, period { "Period" }, direction { "Direction" };
    LaneGraph graph;
    NoteKeyboard keyboard;

    std::array<juce::Rectangle<int>, numLanes> tabs {};
    juce::Rectangle<int> restButton, skipButton;
    juce::Label defaultEditor;

    juce::TextButton newButton { "New" }, cloneButton { "Clone" }, deleteButton { "Delete" };
    bool confirmingDelete = false;
};

} // namespace patternarp
