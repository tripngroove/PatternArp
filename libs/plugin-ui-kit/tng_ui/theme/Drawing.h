#pragma once

#include "Theme.h"

namespace tng::theme
{

// --- Whole pixels ---------------------------------------------------------------------------

// Bars, grid lines and dividers sit on whole pixels so they meet crisply: a bar's top covers
// the line it sits on instead of leaving a faint fringe above it.
inline float pixel (float v)
{
    return std::round (v);
}

// The middle of the pixel row or column `v` falls in, where a 1px line is drawn sharp.
inline float pixelLine (float v)
{
    return pixel (v) + 0.5f;
}

// --- Small marks ----------------------------------------------------------------------------

// A small downward caret beside something that opens a list.
inline constexpr float caretWidth = 8.0f;

inline void drawCaret (juce::Graphics& g, juce::Point<float> centre)
{
    juce::Path p;
    p.addTriangle (centre.x - caretWidth / 2.0f, centre.y - 2.5f, centre.x + caretWidth / 2.0f, centre.y - 2.5f, centre.x, centre.y + 2.5f);
    g.fillPath (p);
}

// A small triangle pointing right (closed) or down (open), beside a section that folds.
inline void drawChevron (juce::Graphics& g, juce::Point<float> centre, bool open)
{
    juce::Path p;
    if (open)
        p.addTriangle (centre.x - 4.5f, centre.y - 2.5f, centre.x + 4.5f, centre.y - 2.5f, centre.x, centre.y + 3.0f);
    else
        p.addTriangle (centre.x - 2.5f, centre.y - 4.5f, centre.x - 2.5f, centre.y + 4.5f, centre.x + 3.0f, centre.y);
    g.fillPath (p);
}

// --- Tabs -----------------------------------------------------------------------------------

// Tabs are one flat strip with square corners, neighbours sharing a dividing line. The open
// tab is white with an ink line and bold text, and opens into what it shows; closed tabs are
// grey with lighter text and show the accent's tint on hover. Closed lines are opaque so
// shared ones do not darken.
inline juce::Colour tabLine (bool open)
{
    return open ? ink : panel.overlaidWith (ink.withAlpha (0.3f));
}

inline juce::Colour tabFill (bool open, bool hovered, juce::Colour accent)
{
    return open ? panel : (hovered ? tint (accent) : background);
}

inline juce::Colour tabText (bool open)
{
    return open ? ink : ink.withAlpha (0.7f);
}

// --- Unused areas ---------------------------------------------------------------------------

// What is kept but never plays (steps past a lane's end, notes past a shortened loop) stays in
// its colours under a veil of the window's grey.
inline void drawUnusedArea (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (background.withAlpha (0.75f));
    g.fillRect (area);
}

// --- Length handles -------------------------------------------------------------------------

// Length handles all look alike: a 1px line through what they shorten, with a grip tab
// centred on it.
inline constexpr float handleWidth = 15.0f; // odd, so a line on a whole pixel has as much tab either side
inline constexpr float handleHeight = 28.0f;

// The tab sits on whole pixels: x is the middle of the line, at a pixel's centre when it is crisp.
inline juce::Rectangle<float> lengthHandleTab (float x, juce::Range<float> line)
{
    return { std::round (x - handleWidth / 2.0f), std::round ((line.getStart() + line.getEnd() - handleHeight) / 2.0f), handleWidth, handleHeight };
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

// --- Play button ----------------------------------------------------------------------------

// A play/pause button with no outline: the accent with white pause bars while playing, its
// tint with a play triangle in the accent while paused. r should sit on whole pixels; the icon
// is about 3/8 of its height, centred on whole pixels too, with each pause bar a third of the
// icon wide.
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

} // namespace tng::theme
