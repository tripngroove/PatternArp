#pragma once

#include "patternarp/Pattern.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace patternarp
{

const char* laneName (Lane lane);

// The per-step bar graph. Click or drag across it to draw a shape; double-click a step to
// reset it to the lane's default. The handle above the graph sets how many steps this lane
// loops over (dragged fully right, it follows the pattern's length).
class LaneGraph final : public juce::Component
{
public:
    LaneGraph();

    void setPattern (const Pattern& pattern);
    void setLane (Lane lane);
    Lane getLane() const { return lane; }

    // Called while drawing (or moving the lane's length handle) with the edited pattern.
    std::function<void (const Pattern&)> onEdit;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    static constexpr int labelGutter = 44;
    static constexpr int handleStrip = 16; // above the graph, holds the lane length handle

private:
    juce::Rectangle<float> graphArea() const;
    int currentLaneLength() const { return laneLength (pattern, lane); }
    bool isInHandleStrip (juce::Point<float> p) const { return p.y < graphArea().getY(); }
    void setLaneLengthFromX (float x);
    int stepAt (float x) const;
    bool isBipolar() const;

    // Converts between a step's value in the current lane and a height in the graph.
    float valueToY (const Step& step) const;
    void setFromY (Step& step, float y) const;
    juce::String valueText (const Step& step) const;

    void drawNoteGrid (juce::Graphics& g, juce::Rectangle<float> area) const;
    void applyAt (juce::Point<float> position);

    Pattern pattern;
    Lane lane = Lane::notes;
    std::array<int, 128> enabled {};
    int numEnabled = 0;
    bool relativeNotes = false; // MIDI In: the Notes lane spans the held notes, low to high
    int lastStep = -1;
    bool draggingHandle = false;
    float lastY = 0.0f;
};

} // namespace patternarp
