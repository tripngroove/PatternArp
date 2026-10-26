#include "patternarp/Sequencer.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace patternarp
{

namespace
{
constexpr double epsilon = 1.0e-9;

std::uint64_t hash (std::uint64_t a, std::uint64_t b)
{
    // SplitMix64 over the combined inputs: cheap, well mixed and repeatable.
    auto z = a * 0x9e3779b97f4a7c15ULL ^ (b + 0x632be59bd9b4e019ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

std::int64_t floorMod (std::int64_t value, std::int64_t divisor)
{
    const auto r = value % divisor;
    return r < 0 ? r + divisor : r;
}

bool claims (const Pattern& p, std::int64_t slot)
{
    // Euclidean spread of `periodHits` over `periodSlots`, starting on the first slot.
    return floorMod (slot * p.periodHits, p.periodSlots) < p.periodHits;
}

double shortestStep (const Arrangement& a)
{
    double shortest = 4.0;
    for (int i = 0; i < a.numPatterns; ++i)
        shortest = std::min (shortest, beatsPerStep (a.patterns[static_cast<size_t> (i)].timing));
    return shortest;
}

std::int64_t directionCycle (const Pattern& p)
{
    switch (p.direction)
    {
        case Direction::forward:
        case Direction::backward:
            return p.length;
        case Direction::pingPong:
            return p.length > 1 ? 2 * p.length - 2 : 1;
        case Direction::random:
        case Direction::randomWalk:
            return 0;
    }
    return p.length;
}
} // namespace

Sequencer::Sequencer (const Arrangement& a)
{
    setArrangement (a);
}

void Sequencer::setArrangement (const Arrangement& a)
{
    // Paused patterns are left out, as if they were not there.
    arrangement.numPatterns = 0;
    for (int i = 0; i < std::clamp (a.numPatterns, 0, maxPatterns); ++i)
    {
        if (! a.patterns[static_cast<size_t> (i)].playing)
            continue;
        sourceIndex[static_cast<size_t> (arrangement.numPatterns)] = i;
        auto& p = arrangement.patterns[static_cast<size_t> (arrangement.numPatterns++)];
        p = a.patterns[static_cast<size_t> (i)];
        sanitise (p);
    }

    numFills = 0;
    for (int i = 0; i < arrangement.numPatterns; ++i)
    {
        const auto& p = arrangement.patterns[static_cast<size_t> (i)];
        if (p.isFill())
            fills[static_cast<size_t> (numFills++)] = i;

        auto& count = numEnabledNotes[static_cast<size_t> (i)];
        count = 0;
        for (int note = 0; note < 128; ++note)
            if (p.enabledNotes.test (static_cast<size_t> (note)))
                enabledNotes[static_cast<size_t> (i)][static_cast<size_t> (count++)] = note;
    }

    restart();
}

void Sequencer::setInputNotes (const int* notes, int count)
{
    numInputNotes = std::clamp (count, 0, 128);
    std::copy (notes, notes + numInputNotes, inputNotes.begin());
    std::sort (inputNotes.begin(), inputNotes.begin() + numInputNotes);
}

void Sequencer::restart()
{
    cursors = {};
    owed = {};
    slotIndex = 0;
    fillCounter = 0;
    nextStart = 0.0;
}

int Sequencer::ownerOfSlot (std::int64_t slot)
{
    // Patterns with a Period like 1:5 claim their slots first, top of the list winning.
    // A pattern that loses a slot is owed one and gets the next slot nobody claims, so every
    // pattern keeps its rate. Fill patterns (1:1) take turns over whatever is left.
    int winner = -1;
    for (int i = 0; i < arrangement.numPatterns; ++i)
    {
        const auto& p = arrangement.patterns[static_cast<size_t> (i)];
        if (p.isFill() || ! claims (p, slot))
            continue;

        if (winner < 0)
            winner = i;
        else
            owed[static_cast<size_t> (i)] = std::min (owed[static_cast<size_t> (i)] + 1, p.periodHits);
    }

    if (winner >= 0)
        return winner;

    for (int i = 0; i < arrangement.numPatterns; ++i)
    {
        if (owed[static_cast<size_t> (i)] > 0)
        {
            --owed[static_cast<size_t> (i)];
            return i;
        }
    }

    if (numFills == 0)
        return -1;

    return fills[static_cast<size_t> (floorMod (fillCounter++, numFills))];
}

int Sequencer::advanceCursor (int patternIndex)
{
    const auto& p = arrangement.patterns[static_cast<size_t> (patternIndex)];
    auto& c = cursors[static_cast<size_t> (patternIndex)];
    const auto n = c.played;
    const std::int64_t length = p.length;
    std::int64_t step = 0;

    switch (p.direction)
    {
        case Direction::forward:
            step = floorMod (n, length);
            break;

        case Direction::backward:
            step = length - 1 - floorMod (n, length);
            break;

        case Direction::pingPong:
            if (length > 1)
            {
                const auto cycle = 2 * length - 2;
                const auto pos = floorMod (n, cycle);
                step = pos < length ? pos : cycle - pos;
            }
            break;

        case Direction::random:
            step = static_cast<std::int64_t> (hash (p.seed, static_cast<std::uint64_t> (n)) % static_cast<std::uint64_t> (length));
            break;

        case Direction::randomWalk:
            if (n > 0 && length > 1)
            {
                const bool up = (hash (p.seed ^ 0x5bd1e995u, static_cast<std::uint64_t> (n)) & 1u) != 0;
                c.walk = static_cast<int> (floorMod (c.walk + (up ? 1 : -1), length));
            }
            step = c.walk;
            break;
    }

    ++c.played;
    ++c.visits[static_cast<size_t> (step)];
    return static_cast<int> (step);
}

bool Sequencer::passesChance (int patternIndex, int step)
{
    const auto& p = arrangement.patterns[static_cast<size_t> (patternIndex)];
    const auto& c = cursors[static_cast<size_t> (patternIndex)];
    const int chance = p.steps[static_cast<size_t> (step)].chance;

    if (chance >= 100)
        return true;

    if (chance > 0)
        return static_cast<int> (hash (p.seed + 0x9e37u, static_cast<std::uint64_t> (c.played)) % 100u) < chance;

    // 1:N plays on the first of every N passes through this step.
    return floorMod (c.visits[static_cast<size_t> (step)] - 1, -chance) == 0;
}

Sequencer::Outcome Sequencer::playSlot (Slot& slot)
{
    const int owner = ownerOfSlot (slotIndex++);

    slot.start = nextStart;
    slot.pattern = owner < 0 ? -1 : sourceIndex[static_cast<size_t> (owner)];
    slot.step = -1;
    slot.numNotes = 0;

    if (owner < 0)
    {
        slot.length = shortestStep (arrangement);
        nextStart += slot.length;
        return Outcome::rest;
    }

    const auto& p = arrangement.patterns[static_cast<size_t> (owner)];
    const double length = beatsPerStep (p.timing);
    const int step = advanceCursor (owner);
    slot.step = step;
    slot.length = length;

    if (! passesChance (owner, step))
    {
        if (p.chanceMode == ChanceMode::skip)
            return Outcome::skipped;

        nextStart += length;
        return Outcome::rest;
    }

    // Notes lane: pick from the enabled (or held) notes, bottom to top.
    const bool fromInput = p.noteSource == NoteSource::midiIn;
    const auto& enabled = fromInput ? inputNotes : enabledNotes[static_cast<size_t> (owner)];
    const int numEnabled = fromInput ? numInputNotes : numEnabledNotes[static_cast<size_t> (owner)];

    const auto& s = p.steps[static_cast<size_t> (step)];
    if (numEnabled > 0)
    {
        const auto index = static_cast<size_t> (std::lround (s.note * static_cast<float> (numEnabled - 1)));
        const int note = enabled[index] + 12 * s.octave;

        if (note >= 0 && note <= 127)
        {
            const double sub = length / s.ratchet;
            for (int r = 0; r < s.ratchet; ++r)
            {
                slot.notes[static_cast<size_t> (slot.numNotes++)] = {
                    nextStart + sub * r, std::max (sub * s.gate / 100.0, 0.001), note, s.velocity, slot.pattern
                };
            }
        }
    }

    nextStart += length;
    return Outcome::played;
}

bool Sequencer::next (Slot& slot)
{
    if (arrangement.numPatterns == 0)
        return false;

    // Skipped steps take no time, so keep going until a slot takes some (with a guard in
    // case every step is set to skip).
    for (int attempt = 0; attempt < 256; ++attempt)
        if (playSlot (slot) != Outcome::skipped)
            return true;

    // Everything skipped: rest for a moment so time still moves.
    slot.start = nextStart;
    slot.length = shortestStep (arrangement);
    slot.pattern = -1;
    slot.numNotes = 0;
    nextStart += slot.length;
    return true;
}

void Sequencer::fastForwardTo (double beat)
{
    Slot slot;
    while (nextStart < beat - epsilon && next (slot))
    {
    }
}

LoopInfo findLoop (const Arrangement& arrangement, std::int64_t maxSlots)
{
    // Percent chances are left out: the loop is measured as if every such step plays, so a
    // pattern with a little randomness still shows its underlying length.
    Arrangement certain = arrangement;
    for (auto& p : certain.patterns)
        for (auto& s : p.steps)
            if (s.chance > 0)
                s.chance = 100;

    LoopInfo info;
    Sequencer sequencer (certain);
    const auto& a = sequencer.arrangement; // sanitised copy
    if (a.numPatterns == 0)
        return info;

    // Every part of the sequencer is a counter that wraps: the Period cycle, the fill
    // rotation, slots owed after a clash and each pattern's step cycle (times any 1:N chance). The whole thing repeats
    // the first time they are all back at zero together.
    std::int64_t periodCycle = 1;
    std::array<std::int64_t, maxPatterns> patternCycle {};

    for (int i = 0; i < a.numPatterns; ++i)
    {
        const auto& p = a.patterns[static_cast<size_t> (i)];
        if (! p.isFill())
            periodCycle = std::lcm (periodCycle, static_cast<std::int64_t> (p.periodSlots));

        const auto cycle = directionCycle (p);
        if (cycle == 0)
            return info; // random directions never repeat exactly

        std::int64_t chanceCycle = 1;
        for (int s = 0; s < p.length; ++s)
        {
            const int chance = p.steps[static_cast<size_t> (s)].chance;
            if (chance < 0)
                chanceCycle = std::lcm (chanceCycle, static_cast<std::int64_t> (-chance));
        }
        patternCycle[static_cast<size_t> (i)] = cycle * chanceCycle;
    }

    Slot slot;
    for (std::int64_t n = 1; n <= maxSlots; ++n)
    {
        sequencer.playSlot (slot);

        bool aligned = sequencer.slotIndex % periodCycle == 0
                    && (sequencer.numFills == 0 || sequencer.fillCounter % sequencer.numFills == 0);
        for (int i = 0; i < a.numPatterns && aligned; ++i)
            aligned = sequencer.cursors[static_cast<size_t> (i)].played % patternCycle[static_cast<size_t> (i)] == 0
                   && sequencer.owed[static_cast<size_t> (i)] == 0;

        if (aligned)
        {
            info.repeats = true;
            info.lengthSlots = n;
            info.lengthBeats = sequencer.nextStart;
            return info;
        }
    }

    return info;
}

std::vector<SequencedNote> render (const Arrangement& arrangement, double beats, const std::vector<int>& inputNotes)
{
    std::vector<SequencedNote> notes;
    Sequencer sequencer (arrangement);
    sequencer.setInputNotes (inputNotes.data(), static_cast<int> (inputNotes.size()));

    Slot slot;
    while (sequencer.position() < beats - epsilon && sequencer.next (slot))
        for (int i = 0; i < slot.numNotes; ++i)
            if (slot.notes[static_cast<size_t> (i)].start < beats - epsilon)
                notes.push_back (slot.notes[static_cast<size_t> (i)]);

    return notes;
}

std::string describeLength (double beats)
{
    auto isWhole = [] (double v) { return std::abs (v - std::round (v)) < 1.0e-6; };
    auto plural = [] (long long n, const char* unit) {
        return std::to_string (n) + " " + unit + (n == 1 ? "" : "s");
    };

    if (isWhole (beats / 4.0))
        return plural (std::llround (beats / 4.0), "bar");
    if (isWhole (beats))
        return plural (std::llround (beats), "beat");
    if (isWhole (beats * 2.0))
        return plural (std::llround (beats * 2.0), "eighth");
    if (isWhole (beats * 4.0))
        return plural (std::llround (beats * 4.0), "sixteenth");
    if (isWhole (beats * 3.0))
        return plural (std::llround (beats * 3.0), "eighth triplet");
    if (isWhole (beats * 6.0))
        return plural (std::llround (beats * 6.0), "sixteenth triplet");
    if (isWhole (beats * 8.0))
        return plural (std::llround (beats * 8.0), "thirty-second");

    char text[32];
    std::snprintf (text, sizeof (text), "%.2f beats", beats);
    return text;
}

} // namespace patternarp
