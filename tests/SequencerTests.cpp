#include "patternarp/PatternEngine.h"
#include "patternarp/Sequencer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <vector>

using namespace patternarp;

namespace
{
// A pattern that plays exactly `notes`, in order, one per step.
Pattern patternOf (std::initializer_list<int> notes)
{
    Pattern p = makeDefaultPattern();
    p.enabledNotes.reset();
    for (int n : notes)
        p.enabledNotes.set (static_cast<size_t> (n));

    // Notes are listed ascending in these tests, so step i picks the i-th enabled note.
    p.length = static_cast<int> (notes.size());
    for (int i = 0; i < p.length; ++i)
    {
        auto& s = p.steps[static_cast<size_t> (i)];
        s = Step {};
        s.note = p.length > 1 ? static_cast<float> (i) / static_cast<float> (p.length - 1) : 0.0f;
    }
    return p;
}

Arrangement arrangementOf (std::initializer_list<Pattern> patterns)
{
    Arrangement a;
    a.numPatterns = 0;
    for (const auto& p : patterns)
        a.patterns[static_cast<size_t> (a.numPatterns++)] = p;
    return a;
}

std::vector<int> firstNotes (const Arrangement& a, int count)
{
    Sequencer sequencer (a);
    std::vector<int> notes;
    Slot slot;
    while (static_cast<int> (notes.size()) < count && sequencer.next (slot))
        for (int i = 0; i < slot.numNotes; ++i)
            notes.push_back (slot.notes[static_cast<size_t> (i)].note);
    return notes;
}

std::vector<int> owners (const Arrangement& a, int count)
{
    Sequencer sequencer (a);
    std::vector<int> result;
    Slot slot;
    for (int i = 0; i < count && sequencer.next (slot); ++i)
        result.push_back (slot.pattern);
    return result;
}

constexpr int A = 57, B = 59, C = 60, E = 64, F = 65;
} // namespace

TEST_CASE ("the design doc example: two 1:1 patterns alternate")
{
    const auto a = arrangementOf ({ patternOf ({ A, B, C }), patternOf ({ E, F }) });
    CHECK (firstNotes (a, 12) == std::vector<int> { A, E, B, F, C, E, A, F, B, E, C, F });

    const auto loop = findLoop (a);
    CHECK (loop.repeats);
    CHECK (loop.lengthSlots == 12);
    CHECK (loop.lengthBeats == Catch::Approx (3.0));
}

TEST_CASE ("Period n:m claims n of every m slots")
{
    auto fill = patternOf ({ C });
    auto insert = patternOf ({ E });

    SECTION ("1:5 inserts every fifth note")
    {
        insert.periodHits = 1;
        insert.periodSlots = 5;
        CHECK (owners (arrangementOf ({ fill, insert }), 10) == std::vector<int> { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0 });
    }

    SECTION ("2:3 takes two of every three")
    {
        insert.periodHits = 2;
        insert.periodSlots = 3;
        CHECK (owners (arrangementOf ({ fill, insert }), 6) == std::vector<int> { 1, 0, 1, 1, 0, 1 });
    }

    SECTION ("a clash goes to the higher pattern; the other gets the next free slot")
    {
        insert.periodHits = 1;
        insert.periodSlots = 2;
        auto second = insert;
        CHECK (owners (arrangementOf ({ insert, second, fill }), 6) == std::vector<int> { 0, 1, 0, 1, 0, 1 });
    }

    SECTION ("with no fill pattern, unclaimed slots rest")
    {
        insert.periodHits = 1;
        insert.periodSlots = 2;
        CHECK (owners (arrangementOf ({ insert }), 4) == std::vector<int> { 0, -1, 0, -1 });
    }
}

TEST_CASE ("directions")
{
    auto p = patternOf ({ 60, 62, 64, 65 });

    SECTION ("backward")
    {
        p.direction = Direction::backward;
        CHECK (firstNotes (arrangementOf ({ p }), 5) == std::vector<int> { 65, 64, 62, 60, 65 });
    }

    SECTION ("ping-pong does not repeat the ends")
    {
        p.direction = Direction::pingPong;
        CHECK (firstNotes (arrangementOf ({ p }), 7) == std::vector<int> { 60, 62, 64, 65, 64, 62, 60 });
    }

    SECTION ("random walk moves one step at a time and is repeatable")
    {
        p.direction = Direction::randomWalk;
        const auto notes = firstNotes (arrangementOf ({ p }), 32);
        CHECK (notes == firstNotes (arrangementOf ({ p }), 32));
        CHECK_FALSE (findLoop (arrangementOf ({ p })).repeats);
    }
}

TEST_CASE ("each note lasts its own pattern's timing")
{
    auto fast = patternOf ({ 60 });
    auto slow = patternOf ({ 72 });
    slow.timing = Timing::eighth;

    Sequencer sequencer (arrangementOf ({ fast, slow }));
    Slot slot;
    std::vector<double> starts;
    for (int i = 0; i < 4; ++i)
    {
        sequencer.next (slot);
        starts.push_back (slot.start);
    }
    CHECK (starts == std::vector<double> { 0.0, 0.25, 0.75, 1.0 });
}

TEST_CASE ("per-step lanes")
{
    auto p = patternOf ({ 60, 64 });

    SECTION ("octave, velocity and gate")
    {
        p.steps[1].octave = -1;
        p.steps[1].velocity = 40;
        p.steps[1].gate = 50;

        Sequencer sequencer (arrangementOf ({ p }));
        Slot slot;
        sequencer.next (slot);
        sequencer.next (slot);
        REQUIRE (slot.numNotes == 1);
        CHECK (slot.notes[0].note == 52);
        CHECK (slot.notes[0].velocity == 40);
        CHECK (slot.notes[0].length == Catch::Approx (0.125));
    }

    SECTION ("ratchet splits the step")
    {
        p.steps[0].ratchet = 4;
        Sequencer sequencer (arrangementOf ({ p }));
        Slot slot;
        sequencer.next (slot);
        REQUIRE (slot.numNotes == 4);
        CHECK (slot.notes[3].start == Catch::Approx (0.1875));
        CHECK (slot.length == Catch::Approx (0.25));
    }

    SECTION ("1:N chance plays every Nth pass, rest keeps time")
    {
        p.steps[1].chance = -2;
        const auto notes = firstNotes (arrangementOf ({ p }), 4);
        CHECK (notes == std::vector<int> { 60, 64, 60, 60 });

        const auto loop = findLoop (arrangementOf ({ p }));
        CHECK (loop.repeats);
        CHECK (loop.lengthBeats == Catch::Approx (1.0)); // 4 slots incl. the rest
    }

    SECTION ("skip takes no time")
    {
        p.steps[1].chance = -2;
        p.chanceMode = ChanceMode::skip;

        Sequencer sequencer (arrangementOf ({ p }));
        Slot slot;
        std::vector<double> starts;
        for (int i = 0; i < 4; ++i)
        {
            sequencer.next (slot);
            starts.push_back (slot.start);
        }
        CHECK (starts == std::vector<double> { 0.0, 0.25, 0.5, 0.75 });

        const auto loop = findLoop (arrangementOf ({ p }));
        CHECK (loop.repeats);
        CHECK (loop.lengthBeats == Catch::Approx (0.75)); // 60, 64, 60, (skip)
    }

    SECTION ("percent chance is repeatable but never loops exactly")
    {
        p.steps[0].chance = 50;
        CHECK (firstNotes (arrangementOf ({ p }), 16) == firstNotes (arrangementOf ({ p }), 16));
        CHECK_FALSE (findLoop (arrangementOf ({ p })).repeats);
    }
}

TEST_CASE ("changing length keeps the shape of every lane")
{
    Pattern p = makeDefaultPattern();
    p.length = 4;
    p.steps[0].note = 0.0f;
    p.steps[1].note = 1.0f / 3.0f;
    p.steps[2].note = 2.0f / 3.0f;
    p.steps[3].note = 1.0f;
    p.steps[3].octave = 2;

    setPatternLength (p, 8);
    CHECK (p.length == 8);
    CHECK (p.steps[0].note == Catch::Approx (0.0f));
    CHECK (p.steps[7].note == Catch::Approx (1.0f));
    for (int i = 1; i < 8; ++i)
        CHECK (p.steps[static_cast<size_t> (i)].note >= p.steps[static_cast<size_t> (i - 1)].note);
    CHECK (p.steps[7].octave == 2);
    CHECK (p.steps[0].octave == 0);

    setPatternLength (p, 2);
    CHECK (p.steps[0].note < 0.5f);
    CHECK (p.steps[1].note > 0.5f);
}

TEST_CASE ("describeLength uses the largest whole unit")
{
    CHECK (describeLength (16.0) == "4 bars");
    CHECK (describeLength (4.0) == "1 bar");
    CHECK (describeLength (6.0) == "6 beats");
    CHECK (describeLength (2.5) == "5 eighths");
    CHECK (describeLength (0.75) == "3 sixteenths");
}

TEST_CASE ("loop length across different periods and lengths")
{
    auto a = patternOf ({ 60, 62, 64 });
    auto b = patternOf ({ 67, 69 });
    b.periodHits = 1;
    b.periodSlots = 4;

    const auto loop = findLoop (arrangementOf ({ a, b }));
    REQUIRE (loop.repeats);

    // Rendering two loops' worth must repeat the first loop exactly.
    const auto notes = render (arrangementOf ({ a, b }), loop.lengthBeats * 2.0);
    REQUIRE (notes.size() % 2 == 0);
    const auto half = notes.size() / 2;
    for (size_t i = 0; i < half; ++i)
    {
        CHECK (notes[i].note == notes[i + half].note);
        CHECK (notes[i].start + loop.lengthBeats == Catch::Approx (notes[i + half].start));
    }
}

// --- Real-time engine --------------------------------------------------------------------

namespace
{
std::vector<int> onPitches (const std::vector<NoteEvent>& events)
{
    std::vector<int> pitches;
    for (const auto& e : events)
        if (e.isNoteOn())
            pitches.push_back (e.note);
    return pitches;
}
} // namespace

TEST_CASE ("engine plays on its own, without any keys")
{
    PatternEngine engine;
    engine.setArrangement (arrangementOf ({ patternOf ({ 60, 64, 67 }) }));

    std::vector<NoteEvent> events;
    engine.process (0.0, 1.0, events);
    CHECK (onPitches (events) == std::vector<int> { 60, 64, 67, 60 });
}

TEST_CASE ("MIDI In patterns pick from the held notes, lowest to highest")
{
    auto p = patternOf ({ 0, 1, 2 }); // three steps: bottom, middle, top
    p.noteSource = NoteSource::midiIn;

    PatternEngine engine;
    engine.setArrangement (arrangementOf ({ p }));

    std::vector<NoteEvent> events;
    engine.process (0.0, 1.0, events);
    CHECK (events.empty()); // nothing held: rests

    engine.noteOn (62);
    engine.noteOn (50);
    engine.noteOn (55);
    engine.process (1.0, 2.0, events);
    CHECK (onPitches (events) == std::vector<int> { 55, 62, 50, 55 }); // steps 4-7 follow the song position
}

TEST_CASE ("latch keeps the last chord for MIDI In patterns")
{
    auto p = patternOf ({ 0 });
    p.noteSource = NoteSource::midiIn;

    PatternEngine engine;
    engine.setArrangement (arrangementOf ({ p }));
    engine.setLatch (true);

    engine.noteOn (48);
    engine.noteOff (48);

    std::vector<NoteEvent> events;
    engine.process (0.0, 0.5, events);
    CHECK (onPitches (events) == std::vector<int> { 48, 48 });

    // A new key after letting go starts a new chord.
    events.clear();
    engine.noteOn (52);
    engine.noteOff (52);
    engine.process (0.5, 1.0, events);
    CHECK (onPitches (events) == std::vector<int> { 52, 52 });

    // Turning latch off with nothing held stops it.
    events.clear();
    engine.setLatch (false);
    engine.process (1.0, 1.5, events);
    CHECK (onPitches (events).empty());
}

TEST_CASE ("engine output is the same whatever the block size")
{
    auto a = arrangementOf ({ patternOf ({ 60, 62, 64 }), patternOf ({ 72, 74 }) });
    a.patterns[1].timing = Timing::eighthTriplet;
    a.patterns[0].steps[1].ratchet = 3;

    PatternEngine whole, split;
    whole.setArrangement (a);
    split.setArrangement (a);

    std::vector<NoteEvent> x, y;
    whole.process (0.0, 8.0, x);
    for (double t = 0.0; t < 8.0; t += 0.0371)
        split.process (t, std::min (t + 0.0371, 8.0), y);

    REQUIRE (x.size() == y.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        CHECK (x[i].note == y[i].note);
        CHECK (x[i].velocity == y[i].velocity);
        CHECK (x[i].ppq == Catch::Approx (y[i].ppq));
    }
}

TEST_CASE ("engine jumps land on the same notes as playing through")
{
    auto a = arrangementOf ({ patternOf ({ 60, 62, 64 }), patternOf ({ 72, 74 }) });

    PatternEngine through, jumped;
    through.setArrangement (a);
    jumped.setArrangement (a);

    std::vector<NoteEvent> x, y;
    through.process (0.0, 10.0, x);
    jumped.process (0.0, 0.5, y);
    y.clear();
    jumped.process (9.0, 10.0, y); // jump ahead

    std::vector<int> expected;
    for (const auto& e : x)
        if (e.isNoteOn() && e.ppq >= 9.0)
            expected.push_back (e.note);

    CHECK (onPitches (y) == expected);
}
