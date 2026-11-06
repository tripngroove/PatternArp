#pragma once

#include "patternarp/Pattern.h"

#include <tng_ui/tng_ui.h>

namespace patternarp::theme
{

// PatternArp draws with the shared tripngroove look (libs/plugin-ui-kit): its colours, spacing,
// fonts, drawing helpers and LookAndFeel are all reachable here as theme::... What follows is
// only what is PatternArp's own.
using namespace tng::theme;

// Variations are drawn in black and greys, so they never read as a pattern: the one playing
// in ink, the other filled ones in a light grey.
inline const juce::Colour variationOn { 0xff15171a }; // ink
inline const juce::Colour variationOff { 0xffc4c7cc };

// One accent colour per pattern, used for its panel stripe, its notes in the preview and every
// highlight inside its panel.
inline juce::Colour patternColour (int index)
{
    return accentColour (index);
}

// A pattern's name, as shown everywhere in the editor: its letter, and for a sub-pattern its
// number under its parent ("A.2").
inline juce::String patternName (const Pattern& pattern)
{
    const auto letter = juce::String::charToString (patternLetter (pattern.letter));
    return pattern.isSub() ? letter + "." + juce::String (pattern.subNumber) : letter;
}

} // namespace patternarp::theme
