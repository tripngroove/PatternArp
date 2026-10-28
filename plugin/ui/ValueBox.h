#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace patternarp
{

// A caption and its value as bold text in the pattern's colour ("Step: 1/16"): click the
// value to pick from a list, or drag it up/down to step through values. The scroll wheel is
// left to the editor's scrolling.
class ValueBox final : public juce::Component
{
public:
    explicit ValueBox (juce::String caption);

    void setRange (int minimum, int maximum);
    void setValue (int newValue); // does not call onChange
    int getValue() const { return value; }

    std::function<juce::String (int)> toText = [] (int v) { return juce::String (v); };
    std::function<void (int)> onChange;

    void setAccent (juce::Colour colour); // the value's colour

    // Just wide enough for the caption and the value shown.
    int getPreferredWidth() const;

    // The value's hover backing reaches this far past its text.
    static constexpr int valueInset = 4;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

private:
    juce::Rectangle<int> valueArea() const; // the value's text, padded a little
    int captionWidth() const;
    void change (int newValue);
    void showList();

    juce::String caption;
    juce::Colour accent;
    int value = 0, minValue = 0, maxValue = 1;
    int dragStartValue = 0;
    bool draggingValue = false;
};

} // namespace patternarp
