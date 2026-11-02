#include "patternarp/Variations.h"

#include <algorithm>
#include <cmath>

namespace patternarp
{

namespace
{
// The first line of a grid (lines every `spacing` from `origin`) at or after `ppq`. A line
// a hair behind `ppq` (rounding in the host's position) counts as at it.
double nextLine (double ppq, double origin, double spacing)
{
    if (spacing <= 0.0)
        return ppq;
    constexpr double tolerance = 1.0e-9;
    const double lines = std::ceil ((ppq - origin) / spacing - tolerance);
    return origin + lines * spacing;
}

bool inRange (int index)
{
    return index >= 0 && index < maxVariations;
}
} // namespace

double switchPosition (double ppq, SwitchTiming timing, double barStartPpq, double beatsPerBar, double beatLength)
{
    switch (timing)
    {
        case SwitchTiming::nextBar:
            return std::max (ppq, nextLine (ppq, barStartPpq, beatsPerBar));
        case SwitchTiming::nextBeat:
            return std::max (ppq, nextLine (ppq, barStartPpq, beatLength));
        case SwitchTiming::immediately:
            break;
    }
    return ppq;
}

Variations::Variations()
{
    filled[0] = true;
}

bool Variations::isFilled (int index) const
{
    return inRange (index) && filled[static_cast<size_t> (index)];
}

int Variations::resolve (int requested, int playing) const
{
    return isFilled (requested) ? requested : playing;
}

bool Variations::copy (int from, int to)
{
    if (! isFilled (from) || ! inRange (to))
        return false;
    if (from != to)
        arrangements[static_cast<size_t> (to)] = arrangements[static_cast<size_t> (from)];
    filled[static_cast<size_t> (to)] = true;
    return true;
}

int Variations::copyToEmpty (int from)
{
    if (! isFilled (from))
        return 0;
    int count = 0;
    for (int i = 0; i < maxVariations; ++i)
        if (! filled[static_cast<size_t> (i)])
            count += copy (from, i) ? 1 : 0;
    return count;
}

bool Variations::clear (int index)
{
    if (! inRange (index) || index == active)
        return false;
    filled[static_cast<size_t> (index)] = false;
    arrangements[static_cast<size_t> (index)] = Arrangement {};
    return true;
}

} // namespace patternarp
