#include "patternarp/Euclid.h"

#include <algorithm>

namespace patternarp
{

std::vector<bool> euclid (int hits, int steps, int rotation)
{
    steps = std::max (steps, 0);
    hits = std::clamp (hits, 0, steps);

    std::vector<bool> pattern (static_cast<size_t> (steps), false);
    if (steps == 0)
        return pattern;

    // Bresenham-style spacing gives the same necklace as Bjorklund's algorithm,
    // with the first hit on step 0.
    for (int i = 0; i < steps; ++i)
        pattern[static_cast<size_t> (i)] = ((i * hits) % steps) < hits;

    const int shift = ((rotation % steps) + steps) % steps;
    std::rotate (pattern.begin(), pattern.begin() + shift, pattern.end());
    return pattern;
}

} // namespace patternarp
