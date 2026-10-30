#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace patternarp
{

// A die in a pattern's colour, with white pips: randomizes the open lane's steps. It shows the
// pattern's tint while pressed.
class DiceButton final : public juce::Button
{
public:
    DiceButton() : juce::Button ("Randomize")
    {
        setTooltip ("Randomize this lane's steps");
    }

    void setColour (juce::Colour c)
    {
        colour = c;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool, bool down) override
    {
        const auto face = getLocalBounds().toFloat();
        g.setColour (down ? theme::tint (colour) : colour);
        g.fillRoundedRectangle (face, theme::cornerRadius);

        // Five pips.
        g.setColour (juce::Colours::white);
        const float pip = face.getWidth() * 0.2f;
        const auto corners = face.reduced (face.getWidth() * 0.27f);
        for (const auto p : { corners.getTopLeft(), corners.getTopRight(), corners.getCentre(), corners.getBottomLeft(), corners.getBottomRight() })
            g.fillEllipse (juce::Rectangle<float> (pip, pip).withCentre (p));
    }

private:
    juce::Colour colour;
};

} // namespace patternarp
