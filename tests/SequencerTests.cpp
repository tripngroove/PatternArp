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
    a.alternate();
    return a;
}

Arrangement withOrder (Arrangement a, std::initializer_list<int> order)
{
    a.orderLength = 0;
    for (int owner : order)
        a.order[static_cast<size_t> (a.orderLength++)] = owner;
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

TEST_CASE ("a new arrangement is A (C3 D3 E3) and B (F4 G4 A4 B4) taking turns")
{
    const Arrangement a;
    CHECK (firstNotes (a, 8) == std::vector<int> { 60, 77, 62, 79, 64, 81, 60, 83 });
    CHECK (a.patterns[0].letter == 0);
    CHECK (a.patterns[1].letter == 1);
    CHECK (findLoop (a).lengthBeats == Catch::Approx (6.0));
}

TEST_CASE ("the design doc example: two patterns alternate by default")
{
    const auto a = arrangementOf ({ patternOf ({ A, B, C }), patternOf ({ E, F }) });
    CHECK (firstNotes (a, 12) == std::vector<int> { A, E, B, F, C, E, A, F, B, E, C, F });

    const auto loop = findLoop (a);
    CHECK (loop.repeats);
    CHECK (loop.lengthSlots == 12);
    CHECK (loop.lengthBeats == Catch::Approx (3.0));
}

TEST_CASE ("a loop length set by hand starts everything over")
{
    auto a = arrangementOf ({ patternOf ({ A, B, C }), patternOf ({ E, F }) });
    a.loopLength = 2 * ticksPerBeat; // natural loop is 3 beats

    const std::vector<int> firstPass { A, E, B, F, C, E, A, F };
    auto twice = firstPass;
    twice.insert (twice.end(), firstPass.begin(), firstPass.end());
    CHECK (firstNotes (a, 16) == twice);

    const auto loop = findLoop (a);
    CHECK (loop.repeats);
    CHECK (loop.lengthBeats == Catch::Approx (2.0));

    SECTION ("notes are cut at the loop end")
    {
        auto p = patternOf ({ C, E });
        p.timing = Timing::quarter;
        auto b = arrangementOf ({ p });
        b.loopLength = 3 * ticksPerBeat / 2; // 1.5 beats

        const auto notes = render (b, 3.0);
        REQUIRE (notes.size() == 4);
        CHECK (notes[0].start == Catch::Approx (0.0));
        CHECK (notes[1].start == Catch::Approx (1.0));
        CHECK (notes[1].length == Catch::Approx (0.5));
        CHECK (notes[2].start == Catch::Approx (1.5));
        CHECK (notes[2].note == C);
        CHECK (notes[3].note == E);
    }

    SECTION ("random choices are rolled again on every pass")
    {
        auto p = patternOf ({ A, B, C, E, F });
        p.direction = Direction::random;
        auto r = arrangementOf ({ p });
        r.loopLength = 2 * ticksPerBeat;

        const auto pitches = [] (const std::vector<SequencedNote>& ns) {
            std::vector<int> out;
            for (const auto& n : ns)
                out.push_back (n.note);
            return out;
        };

        const auto pass0 = pitches (render (r, 0.0, 2.0, {}));
        const auto pass1 = pitches (render (r, 2.0, 4.0, {}));
        CHECK (pass0.size() == 8);
        CHECK (pass0 != pass1);
        CHECK (pass1 == pitches (render (r, 2.0, 4.0, {}))); // still repeatable
        CHECK (pitches (render (r, 4.0)).size() == 16);
        CHECK (pitches (render (r, 4.0)) == [&] { auto both = pass0; both.insert (both.end(), pass1.begin(), pass1.end()); return both; }());
    }

    SECTION ("jumping ahead lands on the same notes")
    {
        const auto all = render (a, 12.0);
        Sequencer sequencer (a);
        sequencer.fastForwardTo (7.5);

        std::vector<SequencedNote> after;
        Slot slot;
        while (sequencer.position() < 12.0 - 1.0e-9 && sequencer.next (slot))
            for (int i = 0; i < slot.numNotes; ++i)
                after.push_back (slot.notes[static_cast<size_t> (i)]);

        std::vector<SequencedNote> expected;
        for (const auto& n : all)
            if (n.start >= 7.5 - 1.0e-9)
                expected.push_back (n);

        REQUIRE (after.size() == expected.size());
        for (size_t i = 0; i < after.size(); ++i)
        {
            CHECK (after[i].start == Catch::Approx (expected[i].start));
            CHECK (after[i].note == expected[i].note);
        }
    }
}

TEST_CASE ("chance ratios are Elektron's, 1:2 to 8:8")
{
    CHECK (chanceRatioAt (0).a == 1);
    CHECK (chanceRatioAt (0).b == 2);
    CHECK (chanceRatioAt (1).a == 2);
    CHECK (chanceRatioAt (1).b == 2);
    CHECK (chanceRatioAt (2).a == 1);
    CHECK (chanceRatioAt (2).b == 3);
    CHECK (chanceRatioAt (numChanceRatios - 1).a == 8);
    CHECK (chanceRatioAt (numChanceRatios - 1).b == 8);

    for (int i = 0; i < numChanceRatios; ++i)
    {
        const auto r = chanceRatioAt (i);
        CHECK (chanceRatioIndex (r) == i);
        CHECK (chanceRatio (ratioChance (r)).a == r.a);
        CHECK (chanceRatio (ratioChance (r)).b == r.b);
    }

    for (int b = 2; b <= 8; ++b)
        CHECK (ratioChance ({ 1, b }) == -b); // older saves stored 1:N as -N
}

TEST_CASE ("A:B ratios keep counting through a loop set by hand")
{
    auto p = patternOf ({ C, E });
    p.steps[0].chance = ratioChance ({ 1, 4 });
    auto a = arrangementOf ({ p });
    a.loopLength = ticksPerBeat / 2; // one pass of the pattern

    // C comes up once a pass, so it plays every fourth pass.
    std::vector<int> pitches;
    for (const auto& n : render (a, 4.0))
        pitches.push_back (n.note);
    CHECK (pitches == std::vector<int> { C, E, E, E, E, C, E, E, E, E });

    std::vector<int> jumped;
    for (const auto& n : render (a, 2.0, 2.5, {}))
        jumped.push_back (n.note);
    CHECK (jumped == std::vector<int> { C, E });
}

TEST_CASE ("extend holds the note before a step that lost its chance")
{
    auto p = patternOf ({ C, E, F });
    p.chanceMode = ChanceMode::extend;
    for (auto& s : p.steps)
        s.gate = 50;
    p.steps[1].chance = 0;

    const auto notes = render (arrangementOf ({ p }), 0.75);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].note == C);
    CHECK (notes[0].length == Catch::Approx (0.375)); // to where E's note would have ended
    CHECK (notes[1].note == F);
    CHECK (notes[1].start == Catch::Approx (0.5));
    CHECK (notes[1].length == Catch::Approx (0.125));

    SECTION ("a run of them makes one long note")
    {
        p.steps[2].chance = 0;
        const auto held = render (arrangementOf ({ p }), 0.75);
        REQUIRE (held.size() == 1);
        CHECK (held[0].length == Catch::Approx (0.625));
    }

    SECTION ("it holds the last repeat of a ratchet")
    {
        p.steps[0].ratchet = 2;
        const auto held = render (arrangementOf ({ p }), 0.75);
        REQUIRE (held.size() == 3);
        CHECK (held[0].length == Catch::Approx (0.0625));
        CHECK (held[1].start == Catch::Approx (0.125));
        CHECK (held[1].length == Catch::Approx (0.25));
    }

    SECTION ("it holds whatever note came before, from any pattern")
    {
        auto tie = patternOf ({ E });
        tie.chanceMode = ChanceMode::extend;
        tie.steps[0].chance = 0;
        const auto held = render (arrangementOf ({ patternOf ({ C }), tie }), 1.0);
        REQUIRE (held.size() == 2);
        CHECK (held[0].note == C);
        CHECK (held[0].length == Catch::Approx (0.475)); // through E's 90% gate
        CHECK (held[1].start == Catch::Approx (0.5));
    }

    SECTION ("after a rest there is nothing to hold")
    {
        auto silent = patternOf ({ C });
        silent.steps[0].chance = 0;
        auto tie = patternOf ({ E });
        tie.chanceMode = ChanceMode::extend;
        tie.steps[0].chance = 0;
        CHECK (render (arrangementOf ({ silent, tie }), 2.0).empty());
    }

    SECTION ("a loop set by hand does not hold a note into the next pass")
    {
        auto q = patternOf ({ C, E });
        q.chanceMode = ChanceMode::extend;
        q.steps[0].chance = 0;
        q.steps[1].gate = 50;
        auto a = arrangementOf ({ q });
        a.loopLength = ticksPerBeat / 2;

        const auto held = render (a, 1.0);
        REQUIRE (held.size() == 2);
        CHECK (held[0].note == E);
        CHECK (held[0].length == Catch::Approx (0.125));
        CHECK (held[1].start == Catch::Approx (0.75));
    }

    SECTION ("jumping ahead lands on the same notes")
    {
        const auto a = arrangementOf ({ p });
        std::vector<SequencedNote> expected;
        for (const auto& n : render (a, 3.0))
            if (n.start >= 1.5 - 1.0e-9)
                expected.push_back (n);

        const auto jumped = render (a, 1.5, 3.0, {});
        REQUIRE (jumped.size() == expected.size());
        for (size_t i = 0; i < jumped.size(); ++i)
        {
            CHECK (jumped[i].start == Catch::Approx (expected[i].start));
            CHECK (jumped[i].length == Catch::Approx (expected[i].length));
        }
    }
}

TEST_CASE ("a lane can loop over fewer steps than its pattern")
{
    auto p = patternOf ({ A, B, C, E });
    p.steps[0].octave = 1;
    p.laneLength[static_cast<size_t> (Lane::octave)] = 3;
    const auto a = arrangementOf ({ p });

    // The octave lane repeats every 3 steps against the 4-step notes lane.
    CHECK (firstNotes (a, 12) == std::vector<int> { A + 12, B, C, E + 12, A, B, C + 12, E, A, B + 12, C, E });

    const auto loop = findLoop (a);
    CHECK (loop.lengthSlots == 12);
    CHECK (loop.lengthBeats == Catch::Approx (3.0));

    SECTION ("a lane longer than the pattern follows the pattern")
    {
        auto q = p;
        q.laneLength[static_cast<size_t> (Lane::octave)] = 8;
        CHECK (laneLength (q, Lane::octave) == 4);
        CHECK (firstNotes (arrangementOf ({ q }), 4) == std::vector<int> { A + 12, B, C, E });
    }

    SECTION ("lanes follow the pattern's direction over their own length")
    {
        p.direction = Direction::backward;
        p.steps[2].octave = 1; // octave lane plays steps 2, 1, 0, 2, 1, 0...
        p.steps[0].octave = 0;
        CHECK (firstNotes (arrangementOf ({ p }), 6) == std::vector<int> { E + 12, C, B, A + 12, E, C });
    }
}

TEST_CASE ("a paused pattern is taken out of the rotation")
{
    auto paused = patternOf ({ E, F });
    paused.playing = false;
    const auto a = arrangementOf ({ patternOf ({ A, B, C }), paused, patternOf ({ C }) });

    CHECK (firstNotes (a, 6) == std::vector<int> { A, C, B, C, C, C });
    CHECK (owners (a, 4) == std::vector<int> { 0, 2, 0, 2 }); // indices still match the editor
    CHECK (findLoop (a).lengthSlots == 6);

    auto midiIn = patternOf ({ C });
    midiIn.noteSource = NoteSource::midiIn;
    midiIn.playing = false;
    CHECK_FALSE (arrangementOf ({ patternOf ({ C }), midiIn }).usesMidiIn());
}

TEST_CASE ("the Order lane decides whose turn each step is")
{
    const auto fill = patternOf ({ C });
    const auto insert = patternOf ({ E });

    SECTION ("A A A A B puts B on every fifth note")
    {
        const auto a = withOrder (arrangementOf ({ fill, insert }), { 0, 0, 0, 0, 1 });
        CHECK (owners (a, 10) == std::vector<int> { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 });
    }

    SECTION ("an empty step is a rest as long as the shortest timing")
    {
        auto slow = fill;
        slow.timing = Timing::eighth;
        const auto a = withOrder (arrangementOf ({ slow, insert }), { 0, -1, 1 });
        CHECK (owners (a, 6) == std::vector<int> { 0, -1, 1, 0, -1, 1 });
        CHECK (findLoop (a).lengthBeats == Catch::Approx (0.5 + 0.25 + 0.25));
    }

    SECTION ("a pattern with no steps in the lane stays silent")
    {
        const auto a = withOrder (arrangementOf ({ fill, insert }), { 1 });
        CHECK (owners (a, 3) == std::vector<int> { 1, 1, 1 });
    }

    SECTION ("steps naming a pattern that does not exist are rests")
    {
        const auto a = withOrder (arrangementOf ({ fill }), { 0, 5 });
        CHECK (owners (a, 4) == std::vector<int> { 0, -1, 0, -1 });
    }
}

TEST_CASE ("editing patterns keeps the Order lane in step")
{
    auto a = arrangementOf ({ patternOf ({ A }), patternOf ({ B }) });
    REQUIRE (a.orderLength == 2);

    SECTION ("a new pattern takes the next step")
    {
        REQUIRE (a.addPattern (patternOf ({ C })));
        CHECK (a.numPatterns == 3);
        CHECK (a.orderLength == 3);
        CHECK (a.order[2] == 2);
        CHECK (a.patterns[2].colour != a.patterns[0].colour);
        CHECK (a.patterns[2].colour != a.patterns[1].colour);
    }

    SECTION ("a new pattern takes the last step of a full lane")
    {
        a.setOrderLength (maxOrderSteps);
        REQUIRE (a.addPattern (patternOf ({ C })));
        CHECK (a.orderLength == maxOrderSteps);
        CHECK (a.order[maxOrderSteps - 1] == 2);
    }

    SECTION ("no more than eight patterns")
    {
        while (a.numPatterns < maxPatterns)
            REQUIRE (a.addPattern (patternOf ({ C })));
        CHECK_FALSE (a.addPattern (patternOf ({ C })));
    }

    SECTION ("deleting a pattern closes up its steps and renumbers the rest")
    {
        a.addPattern (patternOf ({ C }));
        a = withOrder (a, { 0, 1, 2, 1, -1, 2 });
        a.removePattern (1);
        CHECK (a.numPatterns == 2);
        CHECK (a.orderLength == 4);
        CHECK (std::vector<int> (a.order.begin(), a.order.begin() + a.orderLength) == std::vector<int> { 0, 1, -1, 1 });
    }

    SECTION ("deleting the only pattern in the lane alternates the others")
    {
        a.addPattern (patternOf ({ C }));
        a = withOrder (a, { 1, 1 });
        a.removePattern (1);
        CHECK (a.orderLength == 2);
        CHECK (a.order[0] == 0);
        CHECK (a.order[1] == 1);
    }

    SECTION ("letters stay with their pattern; new ones follow the highest")
    {
        a.patterns[0].letter = 0;
        a.patterns[1].letter = 1;
        a.addPattern (patternOf ({ C })); // C
        a.removePattern (1);              // A C
        REQUIRE (a.addPattern (patternOf ({ C })));
        CHECK (a.patterns[0].letter == 0);
        CHECK (a.patterns[1].letter == 2);
        CHECK (a.patterns[2].letter == 3); // D, leaving no B

        a.patterns[2].letter = numLetters - 1; // Z
        REQUIRE (a.addPattern (patternOf ({ C })));
        CHECK (a.patterns[3].letter == 1); // past Z: the first free letter, B
    }

    SECTION ("lengthening repeats the steps there are")
    {
        a = withOrder (a, { 0, 0, 1 });
        a.setOrderLength (7);
        CHECK (std::vector<int> (a.order.begin(), a.order.begin() + 7) == std::vector<int> { 0, 0, 1, 0, 0, 1, 0 });
        a.setOrderLength (99);
        CHECK (a.orderLength == maxOrderSteps);
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

    SECTION ("A:B chance plays on the A-th of every B times its step comes up, rest keeps time")
    {
        p.steps[1].chance = -2; // 1:2, as saved before ratios had an A
        const auto notes = firstNotes (arrangementOf ({ p }), 4);
        CHECK (notes == std::vector<int> { 60, 64, 60, 60 });

        const auto loop = findLoop (arrangementOf ({ p }));
        CHECK (loop.repeats);
        CHECK (loop.lengthBeats == Catch::Approx (1.0)); // 4 slots incl. the rest

        p.steps[1].chance = ratioChance ({ 2, 3 });
        CHECK (firstNotes (arrangementOf ({ p }), 7) == std::vector<int> { 60, 60, 64, 60, 60, 60, 64 });
        CHECK (findLoop (arrangementOf ({ p })).lengthBeats == Catch::Approx (1.5));
    }

    SECTION ("0% never plays")
    {
        p.steps[1].chance = 0;
        CHECK (firstNotes (arrangementOf ({ p }), 3) == std::vector<int> { 60, 60, 60 });
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

    SECTION ("percent chance is repeatable and left out of the loop length")
    {
        const auto withoutChance = findLoop (arrangementOf ({ p }));
        REQUIRE (withoutChance.repeats);
        p.steps[0].chance = 50;

        for (const auto mode : { ChanceMode::rest, ChanceMode::skip })
        {
            p.chanceMode = mode;
            CHECK (firstNotes (arrangementOf ({ p }), 16) == firstNotes (arrangementOf ({ p }), 16));

            const auto loop = findLoop (arrangementOf ({ p }));
            CHECK (loop.repeats);
            CHECK (loop.lengthBeats == Catch::Approx (withoutChance.lengthBeats));
        }
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

TEST_CASE ("the loop is as long as it sounds, not as long as its counters")
{
    SECTION ("a shortened lane whose steps are all alike changes nothing")
    {
        auto p = patternOf ({ A, B, C, E });
        p.laneLength[static_cast<size_t> (Lane::octave)] = 3; // every octave step is 0
        const auto loop = findLoop (arrangementOf ({ p }));
        CHECK (loop.lengthBeats == Catch::Approx (1.0)); // 4 sixteenths, not 12
        CHECK (loop.lengthSlots == 4);
    }

    SECTION ("a pattern that repeats itself")
    {
        auto p = patternOf ({ C, E });
        p.length = 4;
        p.steps[2] = p.steps[0];
        p.steps[3] = p.steps[1];
        CHECK (findLoop (arrangementOf ({ p })).lengthBeats == Catch::Approx (0.5));
    }

    SECTION ("MIDI In patterns are measured with the held notes")
    {
        auto p = patternOf ({ C, E, F });
        p.noteSource = NoteSource::midiIn;
        CHECK (findLoop (arrangementOf ({ p }), { C, E, F }).lengthBeats == Catch::Approx (0.75));
        CHECK (findLoop (arrangementOf ({ p }), { C }).lengthBeats == Catch::Approx (0.25)); // one note held
    }
}

TEST_CASE ("loop length across different Order and pattern lengths")
{
    auto a = patternOf ({ 60, 62, 64 });
    auto b = patternOf ({ 67, 69 });
    b.timing = Timing::eighth;
    const auto arrangement = withOrder (arrangementOf ({ a, b }), { 0, 0, 0, 1 });

    const auto loop = findLoop (arrangement);
    REQUIRE (loop.repeats);
    CHECK (loop.lengthSlots == 8); // two passes of the lane, so B plays both its notes

    // Rendering two loops' worth must repeat the first loop exactly.
    const auto notes = render (arrangement, loop.lengthBeats * 2.0);
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

TEST_CASE ("MIDI Learn starts afresh and plays the notes back in the order played")
{
    auto p = patternOf ({ A, B, C, E, F });
    p.steps[0].velocity = 30;
    p.laneLength[static_cast<size_t> (Lane::octave)] = 2;

    NoteLearner learner;
    learner.learn (p, E);
    CHECK (p.length == 1); // a take starts from one step and no notes
    CHECK (p.enabledNotes.count() == 1);
    CHECK (p.steps[0].velocity == p.defaultVelocity);
    CHECK (laneLength (p, Lane::octave) == 1);

    for (int note : { C, F, C })
        learner.learn (p, note);
    CHECK (learner.numLearned() == 4);
    CHECK (p.length == 4);
    CHECK (p.enabledNotes.count() == 3);
    CHECK (laneLength (p, Lane::octave) == 4);
    CHECK (firstNotes (arrangementOf ({ p }), 8) == std::vector<int> { E, C, F, C, E, C, F, C });

    SECTION ("a take stops adding steps after the last one")
    {
        NoteLearner take;
        auto q = patternOf ({ C });
        for (int i = 0; i < maxSteps + 4; ++i)
            take.learn (q, 40 + i);
        CHECK (q.length == maxSteps);
        CHECK (q.enabledNotes.count() == static_cast<size_t> (maxSteps + 4));
        CHECK (firstNotes (arrangementOf ({ q }), 1) == std::vector<int> { 40 });
    }
}
