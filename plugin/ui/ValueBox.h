#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace patternarp
{

// A number box with its caption to the left ("Step: 1/16"): click the value to pick from a
// list, click the small arrows beside it, or drag up/down to step through values. The scroll
// wheel is left to the editor's scrolling.
class ValueBox final : public juce::Component
{
public:
    explicit ValueBox (juce::String caption);

    void setRange (int minimum, int maximum);
    void setValue (int newValue); // does not call onChange
    int getValue() const { return value; }

    std::function<juce::String (int)> toText = [] (int v) { return juce::String (v); };
    std::function<void (int)> onChange;

    // Just wide enough for the caption and the longest value.
    int getPreferredWidth() const;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    juce::Rectangle<int> boxArea() const;
    juce::Rectangle<int> arrowArea() const;
    void change (int newValue);
    void showList();

    juce::String caption;
    int value = 0, minValue = 0, maxValue = 1;
    int dragStartValue = 0;
    bool draggingValue = false;
};

} // namespace patternarp
