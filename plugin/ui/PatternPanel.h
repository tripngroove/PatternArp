#pragma once

#include "LaneGraph.h"
#include "NoteKeyboard.h"
#include "ValueBox.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace patternarp
{

// One pattern: its settings row, the lane tabs and graph, and the Notes Enabled keyboard.
// Clone and Delete appear while the mouse is over the panel.
class PatternPanel final : public juce::Component, private juce::Timer
{
public:
    PatternPanel();
    ~PatternPanel() override;

    void setPattern (const Pattern& pattern, bool canAdd);
    void setInputNotes (const std::bitset<128>& notes) { keyboard.setInputNotes (notes); }

    // A note just played into the plugin: added to the pattern while MIDI Learn is armed.
    void learnNote (int note);

    std::function<void (const Pattern&)> onChange;
    std::function<void()> onClone, onDelete;

    static constexpr int preferredHeight = 376;

    // Where things are, for tests.
    juce::Rectangle<int> getPlayButtonBounds() const { return playButton; }
    juce::Rectangle<int> getTabBounds (Lane lane) const { return tabs[static_cast<size_t> (lane)]; }
    juce::Rectangle<int> getTabValueBounds (Lane lane) const { return tabValues[static_cast<size_t> (lane)]; }

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void changed();
    void updateHoverButtons();
    void updatePausedLook();
    void layoutTabs();
    int tabAt (juce::Point<int> p) const; // -1 if none
    juce::String tabText (Lane lane) const;
    int tabWidth (Lane lane) const;
    void showTabMenu (Lane lane);

    Pattern pattern;

    ValueBox timing { "Step" }, length { "Length" }, direction { "Dir" };
    LaneGraph graph;
    NoteKeyboard keyboard;
    NoteLearner learner; // the current MIDI Learn take

    std::array<juce::Rectangle<int>, numLanes> tabs {}; // full height; closed tabs are drawn lower
    std::array<juce::Rectangle<int>, numLanes> tabValues {}; // the dropdown part of the Chance and CC tabs
    int hoveredTab = -1;
    juce::Rectangle<int> playButton, titleArea;

    juce::TextButton cloneButton { "Clone" }, deleteButton { "Delete" };
    bool confirmingDelete = false;

    // Shows the hover buttons while the mouse is anywhere over the panel, children included.
    // A separate listener, because a component listening to itself gets every click twice.
    struct HoverWatcher final : juce::MouseListener
    {
        explicit HoverWatcher (PatternPanel& p) : panel (p) {}
        void mouseEnter (const juce::MouseEvent&) override { panel.updateHoverButtons(); }
        void mouseExit (const juce::MouseEvent&) override { panel.updateHoverButtons(); }
        PatternPanel& panel;
    };
    HoverWatcher hoverWatcher { *this };
};

} // namespace patternarp
