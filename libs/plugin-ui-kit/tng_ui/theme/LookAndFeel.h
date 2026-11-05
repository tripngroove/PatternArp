#pragma once

#include "Theme.h"

namespace tng::theme
{

// Buttons, text fields and scrollbars drawn in the kit's colours and corner radius. A plugin's
// editor owns one and passes it to setLookAndFeel.
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // Anything clickable shows the pointing hand; components that draw their own controls set
    // it themselves, and drag handles use a resize cursor instead.
    juce::MouseCursor getMouseCursorFor (juce::Component& component) override
    {
        if (dynamic_cast<juce::Button*> (&component) != nullptr)
            return juce::MouseCursor::PointingHandCursor;
        return LookAndFeel_V4::getMouseCursorFor (component);
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical, int thumbStart,
                        int thumbSize, bool mouseOver, bool) override
    {
        const auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
        g.setColour (mouseOver ? fill.darker (0.15f) : fill);
        g.fillRoundedRectangle (thumb.reduced (1).toFloat(), cornerRadius);
    }

    void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (0.5f), cornerRadius);
    }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        const auto outline = editor.hasKeyboardFocus (true) ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId;
        g.setColour (editor.findColour (outline));
        g.drawRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (0.5f), cornerRadius, 1.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour, bool highlighted, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const float alpha = button.isEnabled() ? 1.0f : 0.5f;
        g.setColour ((down ? fill : (highlighted ? hover : colour)).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (bounds, cornerRadius);
        g.setColour (ink.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);
    }
};

} // namespace tng::theme
