#include "patternarp/Arpeggiator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace patternarp;

namespace
{
std::vector<int> noteOnPitches (const std::vector<NoteEvent>& events)
{
    std::vector<int> pitches;
    for (const auto& e : events)
        if (e.isNoteOn())
            pitches.push_back (e.note);
    return pitches;
}

Arpeggiator makeArp (ArpSettings settings, std::initializer_list<int> notes)
{
    Arpeggiator arp;
    arp.setSettings (settings);
    for (int n : notes)
        arp.noteOn (n, 100);
    return arp;
}
} // namespace

TEST_CASE ("nothing plays when no notes are held")
{
    Arpeggiator arp;
    std::vector<NoteEvent> events;
    arp.process (0.0, 4.0, events);
    CHECK (events.empty());
}

TEST_CASE ("up mode walks the held notes in pitch order, one per sixteenth")
{
    ArpSettings s;
    auto arp = makeArp (s, { 67, 60, 64 }); // pressed out of order

    std::vector<NoteEvent> events;
    arp.process (0.0, 1.5, events);

    CHECK (noteOnPitches (events) == std::vector<int> { 60, 64, 67, 60, 64, 67 });
    CHECK (events.front().ppq == Catch::Approx (0.0));
}

TEST_CASE ("pitch modes")
{
    ArpSettings s;
    std::vector<NoteEvent> events;

    SECTION ("down")
    {
        s.pitchMode = PitchMode::down;
        auto arp = makeArp (s, { 60, 64, 67 });
        arp.process (0.0, 1.0, events);
        CHECK (noteOnPitches (events) == std::vector<int> { 67, 64, 60, 67 });
    }

    SECTION ("up-down does not repeat the turning notes")
    {
        s.pitchMode = PitchMode::upDown;
        auto arp = makeArp (s, { 60, 64, 67 });
        arp.process (0.0, 2.0, events);
        CHECK (noteOnPitches (events) == std::vector<int> { 60, 64, 67, 64, 60, 64, 67, 64 });
    }

    SECTION ("as played keeps press order")
    {
        s.pitchMode = PitchMode::asPlayed;
        auto arp = makeArp (s, { 67, 60, 64 });
        arp.process (0.0, 0.75, events);
        CHECK (noteOnPitches (events) == std::vector<int> { 67, 60, 64 });
    }

    SECTION ("random is repeatable at the same song position")
    {
        s.pitchMode = PitchMode::random;
        auto a = makeArp (s, { 60, 62, 64, 65, 67 });
        auto b = makeArp (s, { 60, 62, 64, 65, 67 });
        std::vector<NoteEvent> other;
        a.process (0.0, 4.0, events);
        b.process (0.0, 4.0, other);
        CHECK (noteOnPitches (events) == noteOnPitches (other));
    }
}

TEST_CASE ("octaves extend the pattern upwards")
{
    ArpSettings s;
    s.octaves = 2;
    auto arp = makeArp (s, { 60, 64 });

    std::vector<NoteEvent> events;
    arp.process (0.0, 1.0, events);
    CHECK (noteOnPitches (events) == std::vector<int> { 60, 64, 72, 76 });
}

TEST_CASE ("rhythm lane masks steps while the pitch lane keeps moving")
{
    ArpSettings s;
    s.rhythmHits = 3;
    s.rhythmSteps = 8; // x..x..x.
    auto arp = makeArp (s, { 60, 62, 64, 65 });

    std::vector<NoteEvent> events;
    arp.process (0.0, 2.0, events);

    // Steps 0, 3 and 6 play; the pitch lane is at index step % 4 on each of them.
    CHECK (noteOnPitches (events) == std::vector<int> { 60, 65, 64 });
}

TEST_CASE ("rate sets the step length")
{
    ArpSettings s;
    s.rate = Rate::eighthTriplet;
    auto arp = makeArp (s, { 60 });

    std::vector<NoteEvent> events;
    arp.process (0.0, 1.0, events);

    std::vector<double> onTimes;
    for (const auto& e : events)
        if (e.isNoteOn())
            onTimes.push_back (e.ppq);

    REQUIRE (onTimes.size() == 3);
    CHECK (onTimes[1] == Catch::Approx (1.0 / 3.0));
    CHECK (onTimes[2] == Catch::Approx (2.0 / 3.0));
}

TEST_CASE ("gate sets note length and offs carry across blocks")
{
    ArpSettings s;
    s.rate = Rate::quarter;
    s.gate = 0.5;
    auto arp = makeArp (s, { 60 });

    std::vector<NoteEvent> events;
    arp.process (0.0, 0.25, events);
    REQUIRE (events.size() == 1);
    CHECK (events[0].isNoteOn());

    arp.process (0.25, 0.75, events);
    REQUIRE (events.size() == 2);
    CHECK_FALSE (events[1].isNoteOn());
    CHECK (events[1].ppq == Catch::Approx (0.5));
}

TEST_CASE ("full gate on a repeated note sends off before the next on")
{
    ArpSettings s;
    s.rate = Rate::quarter;
    s.gate = 1.0;
    auto arp = makeArp (s, { 60 });

    std::vector<NoteEvent> events;
    arp.process (0.0, 1.5, events);

    REQUIRE (events.size() == 3);
    CHECK (events[0].isNoteOn());
    CHECK_FALSE (events[1].isNoteOn());
    CHECK (events[1].ppq == Catch::Approx (1.0));
    CHECK (events[2].isNoteOn());
}

TEST_CASE ("blocks split anywhere give the same result as one big block")
{
    ArpSettings s;
    s.rhythmHits = 5;
    s.rhythmSteps = 7;
    s.pitchMode = PitchMode::upDown;
    s.octaves = 2;

    auto whole = makeArp (s, { 60, 63, 67 });
    auto split = makeArp (s, { 60, 63, 67 });

    std::vector<NoteEvent> a, b;
    whole.process (0.0, 8.0, a);
    for (double t = 0.0; t < 8.0; t += 0.0371)
        split.process (t, std::min (t + 0.0371, 8.0), b);

    REQUIRE (a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK (a[i].note == b[i].note);
        CHECK (a[i].velocity == b[i].velocity);
        CHECK (a[i].ppq == Catch::Approx (b[i].ppq));
    }
}

TEST_CASE ("jumping in the timeline ends sounding notes first")
{
    ArpSettings s;
    s.rate = Rate::quarter;
    s.gate = 0.9;
    auto arp = makeArp (s, { 60 });

    std::vector<NoteEvent> events;
    arp.process (0.0, 0.1, events); // note on at 0, still sounding
    events.clear();

    arp.process (16.0, 16.1, events); // loop back / jump

    REQUIRE (events.size() >= 2);
    CHECK_FALSE (events[0].isNoteOn());
    CHECK (events[0].ppq == Catch::Approx (16.0));
    CHECK (events[1].isNoteOn());
}

TEST_CASE ("stopAll ends every sounding note")
{
    ArpSettings s;
    s.gate = 1.0;
    auto arp = makeArp (s, { 60 });

    std::vector<NoteEvent> events;
    arp.process (0.0, 0.1, events);
    events.clear();

    arp.stopAll (0.1, events);
    REQUIRE (events.size() == 1);
    CHECK_FALSE (events[0].isNoteOn());
}
