#pragma once

#include "patternarp/Pattern.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace patternarp::theme
{

// Colours follow the layout sketch: white panels and black outlines. Each pattern has one main
// colour and one lighter tint of it (see tint), and draws with nothing else.
inline const juce::Colour background { 0xfff3f4f6 };
inline const juce::Colour panel { 0xffffffff };
inline const juce::Colour ink { 0xff15171a };
inline const juce::Colour inkMuted { 0xff8b9099 };
inline const juce::Colour gridLine { 0xff9aa0a8 };
inline const juce::Colour fill { 0xffa4aab2 }; // neutral fill: the scrollbar, pressed buttons
inline const juce::Colour hover { 0xffe8f5fc };
inline const juce::Colour danger { 0xffd64545 };
inline const juce::Colour separator { 0xffdcdfe3 }; // very light grey dividers

// A pattern's lighter tint: its main colour at 45% on white. The main colour is for what is on
// (playing, lit, filled steps) and for editable values; the tint is for what is off (paused,
// quiet, steps past a lane's end, ratios below the Chance line) and for hover highlights.
inline juce::Colour tint (juce::Colour main)
{
    return panel.interpolatedWith (main, 0.45f);
}

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

// Inside an outlined box (the preview, the lanes), lines and steps stop short of its edge:
// its 1px line, then 2px of white.
inline constexpr float boxPadding = 3.0f;

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

// A pattern's play/pause button, in its panel's header and, smaller, beside its row in the open
// Rotation lane: the pattern's colour with white pause bars while it plays, its tint with a play
// triangle in its colour while paused. r should sit on whole pixels; the icon is about 3/8 of
// its height, centred on whole pixels too, with each pause bar a third of the icon wide.
inline void drawPlayButton (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool playing)
{
    g.setColour (playing ? colour : tint (colour));
    g.fillRoundedRectangle (r, cornerRadius);

    float size = std::round (r.getHeight() * 0.375f);
    if (juce::roundToInt (r.getHeight() - size) % 2 != 0)
        size += 1.0f;
    const float bar = std::round (size / 3.0f);
    const auto icon = r.withSizeKeepingCentre (size, size);

    g.setColour (playing ? juce::Colours::white : colour);
    if (playing)
    {
        g.fillRect (icon.withWidth (bar));
        g.fillRect (icon.withTrimmedLeft (size - bar));
    }
    else
    {
        juce::Path p;
        p.addTriangle (icon.getX() + 1.0f, icon.getY(), icon.getRight(), icon.getCentreY(), icon.getX() + 1.0f, icon.getBottom());
        g.fillPath (p);
    }
}

// Buttons, text fields and scrollbars drawn in the editor's colours and corner radius.
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

} // namespace patternarp::theme
