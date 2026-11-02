#pragma once

#include "patternarp/Pattern.h"

#include <array>

namespace patternarp
{

// Variations: up to eight whole arrangements to flip between, by clicking or by automating
// one stepped parameter. Each holds everything that shapes the notes (patterns, lanes, the
// Rotation lane, the loop length); empty ones hold nothing until something is copied in.
constexpr int maxVariations = 8;

// When a variation picked by a click takes over while the song plays. Automation always
// switches straight away: it was already placed on the grid.
enum class SwitchTiming
{
    nextBar,
    nextBeat,
    immediately
};

// Where a switch asked for at `ppq` lands: the first bar (or beat) line at or after it, or
// `ppq` itself for `immediately`. Bar lines fall every `beatsPerBar` quarter notes from
// `barStartPpq` (any bar line, such as the host's last one); beats every `beatLength`.
double switchPosition (double ppq, SwitchTiming timing, double barStartPpq = 0.0, double beatsPerBar = 4.0, double beatLength = 1.0);

struct Variations
{
    std::array<Arrangement, maxVariations> arrangements {};
    std::array<bool, maxVariations> filled {};
    int active = 0; // the one being edited and played

    Variations(); // the first one filled with the default arrangement, the rest empty

    bool isFilled (int index) const;

    // What plays when `requested` is asked for while `playing` plays: the request, unless
    // it is empty (or not a variation), which never fills it and keeps `playing` going.
    int resolve (int requested, int playing) const;

    // Copies one filled variation over another (filled or not). False if `from` is empty
    // or either is not a variation.
    bool copy (int from, int to);

    // Copies a filled variation into every empty one; returns how many it filled.
    int copyToEmpty (int from);

    // Empties a variation. The active one can't be cleared; returns false for it.
    bool clear (int index);

    bool operator== (const Variations&) const = default;
};

} // namespace patternarp
