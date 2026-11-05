#pragma once

#include "../theme/Theme.h"

namespace tng
{

// A die in an accent colour, with white pips, for a "randomize" action. It shows the accent's
// tint while pressed. Give it a tooltip saying what it randomizes.
class DiceButton final : public juce::Button
{
public:
    DiceButton() : juce::Button ("Randomize") {}

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

} // namespace tng
