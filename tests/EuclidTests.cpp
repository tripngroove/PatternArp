#include "patternarp/Euclid.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <string>

using patternarp::euclid;

namespace
{
std::string toString (const std::vector<bool>& pattern)
{
    std::string s;
    for (bool hit : pattern)
        s += hit ? 'x' : '.';
    return s;
}
} // namespace

TEST_CASE ("euclid produces the classic tresillo for 3 over 8")
{
    CHECK (toString (euclid (3, 8)) == "x..x..x.");
}

TEST_CASE ("euclid handles edge cases")
{
    CHECK (euclid (0, 0).empty());
    CHECK (toString (euclid (0, 4)) == "....");
    CHECK (toString (euclid (4, 4)) == "xxxx");
    CHECK (toString (euclid (9, 4)) == "xxxx");
    CHECK (toString (euclid (-2, 4)) == "....");
    CHECK (euclid (3, -1).empty());
}

TEST_CASE ("euclid rotation shifts the pattern left and wraps")
{
    CHECK (toString (euclid (3, 8, 1)) == "..x..x.x");
    CHECK (toString (euclid (3, 8, 8)) == toString (euclid (3, 8)));
    CHECK (toString (euclid (3, 8, -1)) == ".x..x..x");
}

TEST_CASE ("euclid spaces hits as evenly as possible")
{
    const int steps = GENERATE (range (1, 17));
    const int hits = GENERATE_COPY (range (1, steps + 1));

    const auto pattern = euclid (hits, steps);

    REQUIRE (static_cast<int> (pattern.size()) == steps);
    CHECK (pattern.front());
    CHECK (std::count (pattern.begin(), pattern.end(), true) == hits);

    // Gaps between consecutive hits (wrapping around) differ by at most one step.
    int minGap = steps, maxGap = 0, last = -1, first = -1;
    for (int i = 0; i < steps; ++i)
    {
        if (! pattern[static_cast<size_t> (i)])
            continue;
        if (last >= 0)
        {
            minGap = std::min (minGap, i - last);
            maxGap = std::max (maxGap, i - last);
        }
        else
        {
            first = i;
        }
        last = i;
    }
    const int wrapGap = first + steps - last;
    minGap = std::min (minGap, wrapGap);
    maxGap = std::max (maxGap, wrapGap);
    CHECK (maxGap - minGap <= 1);
}
