#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace patternarp::theme
{

// Colours follow the layout sketch: white panels, black outlines, light blue fills.
inline const juce::Colour background { 0xfff3f4f6 };
inline const juce::Colour panel { 0xffffffff };
inline const juce::Colour ink { 0xff15171a };
inline const juce::Colour inkMuted { 0xff8b9099 };
inline const juce::Colour gridLine { 0xff9aa0a8 };
inline const juce::Colour fill { 0xff8fd3f4 };
inline const juce::Colour fillStrong { 0xff2f9fd8 };
inline const juce::Colour hover { 0xffe8f5fc };
inline const juce::Colour danger { 0xffd64545 };

// One colour per pattern, used for its panel stripe and its notes in the preview.
inline juce::Colour patternColour (int index)
{
    static const juce::Colour colours[] = {
        juce::Colour (0xff2f9fd8), juce::Colour (0xffe8833a), juce::Colour (0xff4caf6e), juce::Colour (0xffb05cc8),
        juce::Colour (0xffd64545), juce::Colour (0xff3a8f8f), juce::Colour (0xffc9a227), juce::Colour (0xff6b6fd6),
    };
    return colours[static_cast<size_t> (index) % std::size (colours)];
}

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

} // namespace patternarp::theme
