#pragma once

#include "patternarp/Variations.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace patternarp
{

// The eight numbered circles beside the title, one per variation. The one playing is a dark
// circle with a bold white number (like a pattern's play button), filled ones are light
// grey, empty ones white with a grey number that turns into a plus under the mouse. Click to
// switch (an empty one first gets a copy of the current one); right-click to copy or clear.
class VariationStrip final : public juce::Component
{
public:
    struct State
    {
        std::array<bool, maxVariations> filled {};
        int active = 0;
        int waiting = -1; // a click waiting for its bar or beat: it pulses
        int requested = -1; // asked for by automation; ringed when empty
        bool operator== (const State&) const = default;
    };

    static constexpr int diameter = 28;
    static constexpr int gap = 8;
    static constexpr int ring = 2; // room outside the circles for the "asked for" ring
    static constexpr int preferredWidth = maxVariations * diameter + (maxVariations - 1) * gap + 2 * ring;

    VariationStrip();

    void setState (const State& newState);
    const State& getState() const { return state; }

    // The waiting circle alternates between its two looks; the editor's timer drives it.
    void setPulse (bool on);

    std::function<void (int)> onSelect;
    std::function<void (int from, int to)> onCopy;
    std::function<void (int from)> onCopyToEmpty;
    std::function<void (int)> onClear;

    // Where circle `index` is drawn, in this component's coordinates.
    juce::Rectangle<int> circleBounds (int index) const;
    int indexAt (juce::Point<int> position) const; // -1 between circles

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;

    void showMenu (int index);

private:
    void setHovered (int index);

    State state;
    int hovered = -1;
    bool pulse = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VariationStrip)
};

} // namespace patternarp
