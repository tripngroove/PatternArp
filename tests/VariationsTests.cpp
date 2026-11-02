#include "patternarp/Variations.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace patternarp;
using Catch::Approx;

TEST_CASE ("a clicked switch lands on the next bar or beat line")
{
    SECTION ("next bar, in 4/4 from the song start")
    {
        CHECK (switchPosition (0.5, SwitchTiming::nextBar) == Approx (4.0));
        CHECK (switchPosition (3.99, SwitchTiming::nextBar) == Approx (4.0));
        CHECK (switchPosition (5.0, SwitchTiming::nextBar) == Approx (8.0));
    }

    SECTION ("asked for right on a bar line, it switches there")
    {
        CHECK (switchPosition (8.0, SwitchTiming::nextBar) == Approx (8.0));
        CHECK (switchPosition (8.0 - 1.0e-12, SwitchTiming::nextBar) == Approx (8.0));
    }

    SECTION ("bars follow the host's bar lines and time signature")
    {
        // 3/4, with a bar starting at beat 1 (a pickup bar before it).
        CHECK (switchPosition (2.0, SwitchTiming::nextBar, 1.0, 3.0) == Approx (4.0));
        CHECK (switchPosition (4.5, SwitchTiming::nextBar, 4.0, 3.0) == Approx (7.0));
    }

    SECTION ("next beat")
    {
        CHECK (switchPosition (2.25, SwitchTiming::nextBeat) == Approx (3.0));
        CHECK (switchPosition (3.0, SwitchTiming::nextBeat) == Approx (3.0));
        // 6/8: a beat is an eighth note.
        CHECK (switchPosition (2.2, SwitchTiming::nextBeat, 0.0, 3.0, 0.5) == Approx (2.5));
    }

    SECTION ("immediately")
    {
        CHECK (switchPosition (2.25, SwitchTiming::immediately) == Approx (2.25));
    }
}

TEST_CASE ("variations start with only the first one filled")
{
    const Variations v;
    CHECK (v.active == 0);
    CHECK (v.isFilled (0));
    for (int i = 1; i < maxVariations; ++i)
        CHECK_FALSE (v.isFilled (i));
    CHECK_FALSE (v.isFilled (-1));
    CHECK_FALSE (v.isFilled (maxVariations));
}

TEST_CASE ("asking for an empty variation keeps the playing one going")
{
    Variations v;
    v.filled[2] = true;
    CHECK (v.resolve (2, 0) == 2);
    CHECK (v.resolve (3, 0) == 0); // empty: never filled by a request
    CHECK (v.resolve (3, 2) == 2);
    CHECK (v.resolve (-1, 2) == 2);
    CHECK_FALSE (v.isFilled (3));
}

TEST_CASE ("copying and clearing variations")
{
    Variations v;
    v.arrangements[0].loopLength = 96;

    SECTION ("copy fills the target with the source")
    {
        CHECK (v.copy (0, 3));
        CHECK (v.isFilled (3));
        CHECK (v.arrangements[3] == v.arrangements[0]);
    }

    SECTION ("an empty variation can't be copied")
    {
        CHECK_FALSE (v.copy (4, 0));
        CHECK (v.arrangements[0].loopLength == 96);
    }

    SECTION ("copy to all empty fills only the empty ones")
    {
        v.filled[5] = true;
        v.arrangements[5].loopLength = 48;
        CHECK (v.copyToEmpty (0) == 6);
        for (int i = 0; i < maxVariations; ++i)
            CHECK (v.isFilled (i));
        CHECK (v.arrangements[5].loopLength == 48);
        CHECK (v.arrangements[7].loopLength == 96);
    }

    SECTION ("clear empties a variation, but not the active one")
    {
        v.copy (0, 1);
        CHECK (v.clear (1));
        CHECK_FALSE (v.isFilled (1));
        CHECK (v.arrangements[1] == Arrangement {});
        CHECK_FALSE (v.clear (0));
        CHECK (v.isFilled (0));
    }
}
