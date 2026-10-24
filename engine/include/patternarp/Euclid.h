#pragma once

#include <vector>

namespace patternarp
{

// Spreads `hits` onsets as evenly as possible over `steps` steps (Bjorklund / Euclidean
// rhythm), then rotates the result left by `rotation` steps. The first step of an
// unrotated pattern is always a hit. Out-of-range input is clamped: hits to [0, steps],
// steps to at least 0.
std::vector<bool> euclid (int hits, int steps, int rotation = 0);

} // namespace patternarp
