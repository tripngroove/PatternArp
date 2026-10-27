#pragma once

#include "patternarp/Pattern.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace patternarp
{

// The Order lane: whose turn each step of the arp is.
//
// Collapsed, it shows the order as a row of lettered steps in each pattern's colour; click it
// to open the grid, which has a row per pattern. Click or drag in the grid to give steps to a
// pattern (clicking a step a pattern already has makes it a rest), and drag the handle above
// the grid to change how many steps the lane loops over. Click the header to close it again.
class OrderLane final : public juce::Component
{
public:
    OrderLane();

    void setArrangement (const Arrangement& arrangement);

    // Called while the order or its length is edited, with the edited arrangement.
    std::function<void (const Arrangement&)> onEdit;

    // Called when opening or closing changes the height the lane wants.
    std::function<void()> onHeightChange;

    int getPreferredHeight() const;
    bool isExpanded() const { return expanded; }
    void setExpanded (bool shouldExpand);

    static constexpr int collapsedHeight = 40;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

private:
    enum class Drag
    {
        none,
        paint, // gives each step passed over to the pattern under the mouse
        clear, // turns the passed-over steps of the pattern under the mouse into rests
        length
    };

    juce::Rectangle<float> boxArea() const { return getLocalBounds().toFloat().reduced (0.5f); }
    juce::Rectangle<float> headerArea() const;
    juce::Rectangle<float> handleArea() const; // expanded only: the strip above the grid
    juce::Rectangle<float> gridArea() const;   // collapsed: the row of steps
    float stepWidth() const;
    int stepAt (float x) const;
    int rowAt (float y) const;

    void paintCollapsed (juce::Graphics& g);
    void paintExpanded (juce::Graphics& g);
    void paintStep (juce::Graphics& g, juce::Rectangle<float> r, int owner, bool lettered) const;
    void paintStrip (juce::Graphics& g); // the length handle

    void applyAt (juce::Point<float> position);
    void setLengthFromX (float x);
    void edited();

    Arrangement arrangement;
    Arrangement dragStart; // the length handle works from the lane as it was when grabbed
    bool expanded = false;
    Drag drag = Drag::none;
    int lastStep = -1; // so fast drags fill every step they pass
};

} // namespace patternarp
