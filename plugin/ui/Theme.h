#pragma once

#include "patternarp/Pattern.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace patternarp::theme
{

// Colours follow the layout sketch: white panels, black outlines, light grey fills.
inline const juce::Colour background { 0xfff3f4f6 };
inline const juce::Colour panel { 0xffffffff };
inline const juce::Colour ink { 0xff15171a };
inline const juce::Colour inkMuted { 0xff8b9099 };
inline const juce::Colour gridLine { 0xff9aa0a8 };
inline const juce::Colour fill { 0xffd3d6db }; // filled steps, selected keys, the scrollbar
inline const juce::Colour fillStrong { 0xff2f9fd8 };
inline const juce::Colour hover { 0xffe8f5fc };
inline const juce::Colour danger { 0xffd64545 };
inline const juce::Colour separator { 0xffdcdfe3 }; // very light grey dividers

// Section titles: "Rotation", "Pattern A".
inline constexpr float titleSize = 18.0f;

// One colour per pattern, used for its panel stripe and its notes in the preview.
inline juce::Colour patternColour (int index)
{
    static const juce::Colour colours[] = {
        juce::Colour (0xff2f9fd8), juce::Colour (0xffe8833a), juce::Colour (0xff4caf6e), juce::Colour (0xffb05cc8),
        juce::Colour (0xffd64545), juce::Colour (0xff3a8f8f), juce::Colour (0xffc9a227), juce::Colour (0xff6b6fd6),
    };
    return colours[static_cast<size_t> (index) % std::size (colours)];
}

// A pattern's letter, as shown everywhere in the editor.
inline juce::String patternName (const Pattern& pattern)
{
    return juce::String::charToString (patternLetter (pattern.letter));
}

// Note names follow Bitwig, Ableton and Logic: MIDI note 60 is C3.
inline constexpr int middleCOctave = 3;

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

inline int textWidth (const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidthInt (f, text);
}

// Every rounded corner uses this radius (the Rotation lane's steps set it).
inline constexpr float cornerRadius = 3.0f;

// Horizontal spacing is the same everywhere: `padding` inside a box, button or tab, either
// side of its text, and `spacing` between neighbouring controls and at the window's edges.
inline constexpr int padding = 8;
inline constexpr int spacing = 8;

// A small downward caret beside something that opens a list.
inline constexpr float caretWidth = 8.0f;

inline void drawCaret (juce::Graphics& g, juce::Point<float> centre)
{
    juce::Path p;
    p.addTriangle (centre.x - caretWidth / 2.0f, centre.y - 2.5f, centre.x + caretWidth / 2.0f, centre.y - 2.5f, centre.x, centre.y + 2.5f);
    g.fillPath (p);
}

// The length handles of the preview, the lanes and the Rotation lane all look alike: a 1px
// line through what they shorten, with a grip tab centred on it.
inline constexpr float handleWidth = 14.0f;
inline constexpr float handleHeight = 28.0f;

inline juce::Rectangle<float> lengthHandleTab (float x, juce::Range<float> line)
{
    return juce::Rectangle<float> (handleWidth, handleHeight).withCentre ({ x, (line.getStart() + line.getEnd()) / 2.0f });
}

inline void drawLengthHandle (juce::Graphics& g, float x, juce::Range<float> line)
{
    g.setColour (ink);
    g.fillRect (juce::Rectangle<float> (x - 0.5f, line.getStart(), 1.0f, line.getLength()));
    const auto tab = lengthHandleTab (x, line);
    g.fillRoundedRectangle (tab, cornerRadius);
    g.setColour (panel);
    for (int i = -1; i <= 1; ++i)
        g.drawHorizontalLine (juce::roundToInt (tab.getCentreY()) + i * 4, tab.getX() + 4.0f, tab.getRight() - 4.0f);
}

// A label beside a length handle, near the bottom of its line, on a backing so it reads over
// whatever is drawn there. It goes right of the line if it fits before room's end, else left.
inline void drawLengthHandleLabel (juce::Graphics& g, const juce::String& text, const juce::Font& textFont, float x,
                                   juce::Range<float> line, float roomEnd)
{
    const float w = static_cast<float> (juce::GlyphArrangement::getStringWidthInt (textFont, text)) + 12.0f;
    const float h = textFont.getHeight() + 6.0f;
    const bool right = x + 6.0f + w <= roomEnd;
    const auto r = juce::Rectangle<float> (right ? x + 6.0f : x - 6.0f - w, line.getEnd() - h - 4.0f, w, h);
    g.setColour (panel.withAlpha (0.9f));
    g.fillRoundedRectangle (r, cornerRadius);
    g.setColour (ink);
    g.setFont (textFont);
    g.drawText (text, r, juce::Justification::centred);
}

// Buttons, text fields and scrollbars drawn in the editor's colours and corner radius.
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
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

} // namespace patternarp::theme
