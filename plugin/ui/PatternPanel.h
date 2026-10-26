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
    ~PatternPanel() override;

    void setPattern (const Pattern& pattern, int index, bool canAdd);
    void setInputNotes (const std::bitset<128>& notes) { keyboard.setInputNotes (notes); }

    std::function<void (const Pattern&)> onChange;
    std::function<void()> onNew, onClone, onDelete;

    // Dragging the colour stripe or the title moves the panel: where its top edge would be
    // in the parent, then the drop.
    std::function<void (int)> onReorderDrag;
    std::function<void()> onReorderEnd;

    static constexpr int preferredHeight = 406;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void changed();
    void updateHoverButtons();
    void updatePausedLook();
    void layoutTabs();
    void setDefault (Lane lane, int value);

    Pattern pattern;
    int index = 0;

    ValueBox timing { "Timing" }, length { "Length" }, period { "Period" }, direction { "Direction" };
    LaneGraph graph;
    NoteKeyboard keyboard;

    std::array<juce::Rectangle<int>, numLanes> tabs {};
    juce::Rectangle<int> restButton, skipButton, playButton;
    juce::Label defaultEditor;

    juce::TextButton newButton { "New" }, cloneButton { "Clone" }, deleteButton { "Delete" };
    bool confirmingDelete = false;

    bool isInGrip (juce::Point<int> p) const;
    bool reordering = false;
    int grabY = 0;

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
