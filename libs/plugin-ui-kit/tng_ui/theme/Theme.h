#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tng::theme
{

// --- Colours --------------------------------------------------------------------------------

// White panels and black outlines on a pale grey window. Each accent colour (one per pattern,
// track, voice...) has one lighter tint of it (see tint), and draws with nothing else.
inline const juce::Colour background { 0xfff3f4f6 };
inline const juce::Colour panel { 0xffffffff };
inline const juce::Colour ink { 0xff15171a };
inline const juce::Colour inkMuted { 0xff8b9099 };
inline const juce::Colour gridLine { 0xff9aa0a8 };
inline const juce::Colour fill { 0xffa4aab2 }; // neutral fill: the scrollbar, pressed buttons
inline const juce::Colour hover { 0xffe8f5fc };
inline const juce::Colour danger { 0xffd64545 };
inline const juce::Colour separator { 0xffdcdfe3 }; // very light grey dividers

// An accent's lighter tint: its main colour at 45% on white. The main colour is for what is on
// (playing, lit, filled steps) and for editable values; the tint is for what is off (paused,
// quiet, unused) and for hover highlights.
inline juce::Colour tint (juce::Colour main)
{
    return panel.interpolatedWith (main, 0.45f);
}

// The accent palette, in order: each new thing that gets a colour takes the next one.
inline juce::Colour accentColour (int index)
{
    static const juce::Colour colours[] = {
        juce::Colour (0xff2f9fd8), juce::Colour (0xffe8833a), juce::Colour (0xff4caf6e), juce::Colour (0xffb05cc8),
        juce::Colour (0xffd64545), juce::Colour (0xff3a8f8f), juce::Colour (0xffc9a227), juce::Colour (0xff6b6fd6),
    };
    return colours[static_cast<size_t> (index) % std::size (colours)];
}

// --- Text -----------------------------------------------------------------------------------

// Section titles ("Rotation", "Pattern A").
inline constexpr float titleSize = 18.0f;

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

inline int textWidth (const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidthInt (f, text);
}

// Note names follow Bitwig, Ableton and Logic: MIDI note 60 is C3.
inline constexpr int middleCOctave = 3;

// --- Shape and spacing ----------------------------------------------------------------------

// Every rounded corner uses this radius.
inline constexpr float cornerRadius = 3.0f;

// Horizontal spacing is the same everywhere: `padding` inside a box, button or tab, either
// side of its text, and `spacing` between neighbouring controls and at the window's edges.
// Vertical spacing sticks to the same 8-pixel grid.
inline constexpr int padding = 8;
inline constexpr int spacing = 8;

// Inside an outlined box (a preview, a lane), lines and steps stop short of its edge:
// its 1px line, then 2px of white.
inline constexpr float boxPadding = 3.0f;

} // namespace tng::theme
