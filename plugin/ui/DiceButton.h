#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace patternarp
{

// A die in a pattern's colour, with white pips: randomizes the open lane's steps.
class DiceButton final : public juce::Button
{
public:
    DiceButton() : juce::Button ("Randomize")
    {
        setTooltip ("Randomize this lane's steps");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setColour (juce::Colour c)
    {
        colour = c;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto face = getLocalBounds().toFloat();
        g.setColour (down ? colour.darker (0.3f) : (highlighted ? colour.darker (0.15f) : colour));
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
