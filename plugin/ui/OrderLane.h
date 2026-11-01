#pragma once

#include "patternarp/Pattern.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace patternarp
{

// The Rotation lane (the Order lane in code): whose turn each step of the arp is.
//
// Collapsed, it shows the order as a row of lettered steps in each pattern's colour; click it
// to open the grid, which has a row per pattern and keeps every step where and as it was.
// Sub-patterns take no turns, so they get no row. Click or drag in the grid to give steps to a pattern. Clicking a step a pattern already has
// quiets it (dimmed, silent, keeping its time) or lights it again; a pattern with every step
// quiet is paused, and pausing a pattern dims all its steps. Drag the handle on the grid to
// change how many steps the lane loops over. Each row starts with its pattern's play/pause
// button; clicking it or the name beside it plays or pauses the pattern, as the button on the
// pattern's panel does. Click the header to close it again.
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
    static constexpr int labelWidth = 104; // "Rotation" and, when open, "Pattern A"...; the steps start after it
    static constexpr int nameX = 40; // where an open row's "Pattern A" starts, after its play/pause button
    static constexpr int nameWidth = labelWidth - nameX - 2; // its room, up to just before the steps
    static constexpr float nameMinScale = 0.9f; // fonts differ by platform: a name too wide is narrowed this far at most
    static constexpr int stepPitch = 38; // a step's cell, line to line: the grid's size is fixed
    static constexpr int rowPitch = 29; // a row of the open grid: a line, 2px, a 24px step, 2px

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
        quiet, // quiets the passed-over steps of the pattern under the mouse
        light, // lights them again
        length
    };

    juce::Rectangle<float> boxArea() const { return getLocalBounds().toFloat(); }
    juce::Rectangle<float> headerArea() const;
    juce::Rectangle<float> gridArea() const; // collapsed: the row of steps
    float handleX() const;
    bool isOnHandle (juce::Point<float> p) const; // expanded only
    int stepAt (float x) const;
    int rowAt (float y) const; // may be past the rows
    int patternOfRow (int row) const { return rowPatterns[static_cast<size_t> (row)]; }
    int playToggleAt (juce::Point<float> p) const; // the row whose play/pause button or name is at p, or -1

    void paintCollapsed (juce::Graphics& g);
    void paintExpanded (juce::Graphics& g);
    juce::Rectangle<float> stepBox (int step, float rowTop) const; // a step's box, in the row whose top line is at rowTop
    void paintStep (juce::Graphics& g, juce::Rectangle<float> r, int turn) const;
    void paintHandle (juce::Graphics& g); // the length handle

    void applyAt (juce::Point<float> position);
    void setLengthFromX (float x);
    void edited();

    Arrangement arrangement;
    std::array<int, maxPatterns> rowPatterns {}; // the pattern of each row: the ones that take turns
    int numRows = 0;
    Arrangement dragStart; // the length handle works from the lane as it was when grabbed
    bool expanded = false;
    Drag drag = Drag::none;
    int lastStep = -1; // so fast drags fill every step they pass
};

} // namespace patternarp
