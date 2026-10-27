#include "patternarp/Sequencer.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace patternarp
{

namespace
{
constexpr double epsilon = 1.0e-9;
constexpr std::uint64_t passSalt = 0x2545f4914f6cdd1dULL; // mixes the loop pass into random choices

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

double shortestStep (const Arrangement& a)
{
    double shortest = 4.0;
    for (int i = 0; i < a.numPatterns; ++i)
        shortest = std::min (shortest, beatsPerStep (a.patterns[static_cast<size_t> (i)].timing));
    return shortest;
}

// Plays until a lane of `length` steps is back at its start (0 = never, for random).
std::int64_t directionCycle (Direction direction, std::int64_t length)
{
    switch (direction)
    {
        case Direction::forward:
        case Direction::backward:
            return length;
        case Direction::pingPong:
            return length > 1 ? 2 * length - 2 : 1;
        case Direction::random:
        case Direction::randomWalk:
            return 0;
    }
    return length;
}
} // namespace

Sequencer::Sequencer (const Arrangement& a)
{
    setArrangement (a);
}

void Sequencer::setArrangement (const Arrangement& a)
{
    // Paused patterns are left out, as if they were not there.
    arrangement.loopLength = std::max (0, a.loopLength);
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

    // The Order lane, renumbered to the playing patterns. Turns of paused patterns are left
    // out, so they take no time.
    std::array<int, maxPatterns> playingIndex {};
    playingIndex.fill (-1);
    for (int i = 0; i < arrangement.numPatterns; ++i)
        playingIndex[static_cast<size_t> (sourceIndex[static_cast<size_t> (i)])] = i;

    numTurns = 0;
    const int numSource = std::clamp (a.numPatterns, 0, maxPatterns);
    for (int i = 0; i < std::clamp (a.orderLength, 0, maxOrderSteps); ++i)
    {
        const int owner = a.order[static_cast<size_t> (i)];
        if (owner < 0 || owner >= numSource)
            turns[static_cast<size_t> (numTurns++)] = -1;
        else if (const int playing = playingIndex[static_cast<size_t> (owner)]; playing >= 0)
            turns[static_cast<size_t> (numTurns++)] = playing;
    }

    for (int i = 0; i < arrangement.numPatterns; ++i)
    {
        const auto& p = arrangement.patterns[static_cast<size_t> (i)];
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
    startCycle (0.0);
}

void Sequencer::startCycle (double beat)
{
    cursors = {};
    slotIndex = 0;
    cycleStart = beat;
    nextStart = beat;
    const double loop = loopBeats();
    pass = loop > 0.0 ? static_cast<std::uint64_t> (std::llround (beat / loop)) : 0;
}

std::array<int, numLanes> Sequencer::advanceCursor (int patternIndex)
{
    // Every lane walks the pattern's direction over its own length, so lanes of the same
    // length always stay on the same step.
    const auto& p = arrangement.patterns[static_cast<size_t> (patternIndex)];
    auto& c = cursors[static_cast<size_t> (patternIndex)];
    const auto n = c.played;
    std::array<int, numLanes> steps {};

    for (int lane = 0; lane < numLanes; ++lane)
    {
        const std::int64_t length = laneLength (p, static_cast<Lane> (lane));
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
                step = static_cast<std::int64_t> (hash (p.seed + pass * passSalt, static_cast<std::uint64_t> (n)) % static_cast<std::uint64_t> (length));
                break;

            case Direction::randomWalk:
            {
                auto& walk = c.walk[static_cast<size_t> (lane)];
                if (n > 0 && length > 1)
                {
                    const bool up = (hash ((p.seed ^ 0x5bd1e995u) + pass * passSalt, static_cast<std::uint64_t> (n)) & 1u) != 0;
                    walk = static_cast<int> (floorMod (walk + (up ? 1 : -1), length));
                }
                step = walk;
                break;
            }
        }

        steps[static_cast<size_t> (lane)] = static_cast<int> (step);
    }

    ++c.played;
    ++c.visits[static_cast<size_t> (steps[static_cast<size_t> (Lane::chance)])];
    return steps;
}

bool Sequencer::passesChance (int patternIndex, int step)
{
    const auto& p = arrangement.patterns[static_cast<size_t> (patternIndex)];
    const auto& c = cursors[static_cast<size_t> (patternIndex)];
    const int chance = p.steps[static_cast<size_t> (step)].chance;

    if (chance >= 100)
        return true;

    if (chance > 0)
        return static_cast<int> (hash (p.seed + 0x9e37u + pass * passSalt, static_cast<std::uint64_t> (c.played)) % 100u) < chance;

    // 1:N plays on the first of every N passes through this step.
    return floorMod (c.visits[static_cast<size_t> (step)] - 1, -chance) == 0;
}

Sequencer::Outcome Sequencer::playSlot (Slot& slot)
{
    const int owner = turns[static_cast<size_t> (slotIndex++ % numTurns)];

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
    const auto steps = advanceCursor (owner);
    const auto stepOf = [&] (Lane lane) -> const Step& { return p.steps[static_cast<size_t> (steps[static_cast<size_t> (lane)])]; };
    slot.step = steps[static_cast<size_t> (Lane::notes)];
    slot.length = length;

    if (! passesChance (owner, steps[static_cast<size_t> (Lane::chance)]))
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

    if (numEnabled > 0)
    {
        const auto index = static_cast<size_t> (std::lround (stepOf (Lane::notes).note * static_cast<float> (numEnabled - 1)));
        const int note = enabled[index] + 12 * stepOf (Lane::octave).octave;
        const int ratchet = stepOf (Lane::ratchet).ratchet;
        const int gate = stepOf (Lane::gate).gate;
        const int velocity = stepOf (Lane::velocity).velocity;

        if (note >= 0 && note <= 127)
        {
            const double sub = length / ratchet;
            for (int r = 0; r < ratchet; ++r)
            {
                slot.notes[static_cast<size_t> (slot.numNotes++)] = {
                    nextStart + sub * r, std::max (sub * gate / 100.0, 0.001), note, velocity, slot.pattern
                };
            }
        }
    }

    nextStart += length;
    return Outcome::played;
}

bool Sequencer::next (Slot& slot)
{
    if (arrangement.numPatterns == 0 || numTurns == 0)
        return false;

    const double loop = loopBeats();
    if (loop <= 0.0)
        return nextInCycle (slot);

    const double cycleEnd = cycleStart + loop;
    if (nextStart >= cycleEnd - epsilon)
        startCycle (cycleEnd);

    nextInCycle (slot);

    // Nothing may sound past the end of the loop: cut the slot (and its ratchets) there.
    const double end = cycleStart + loop;
    slot.length = std::min (slot.length, end - slot.start);
    int kept = 0;
    for (int i = 0; i < slot.numNotes; ++i)
    {
        auto n = slot.notes[static_cast<size_t> (i)];
        if (n.start >= end - epsilon)
            continue;
        n.length = std::min (n.length, end - n.start);
        slot.notes[static_cast<size_t> (kept++)] = n;
    }
    slot.numNotes = kept;
    nextStart = std::min (nextStart, end);
    return true;
}

bool Sequencer::nextInCycle (Slot& slot)
{
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
    // With a set loop length, jump straight to the loop pass that contains `beat`.
    if (const double loop = loopBeats(); loop > 0.0)
        if (const double pass = std::floor ((beat + epsilon) / loop) * loop; pass > cycleStart + epsilon)
            startCycle (pass);

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
    if (arrangement.loopLength > 0)
    {
        info.repeats = true;
        info.lengthBeats = static_cast<double> (arrangement.loopLength) / ticksPerBeat;
        return info;
    }

    Sequencer sequencer (certain);
    const auto& a = sequencer.arrangement; // sanitised copy
    if (a.numPatterns == 0 || sequencer.numTurns == 0)
        return info;

    // Every part of the sequencer is a counter that wraps: the Order lane and each pattern's
    // step cycle (times any 1:N chance). The whole thing repeats the first time they are all
    // back at zero together.
    std::array<std::int64_t, maxPatterns> patternCycle {};

    for (int i = 0; i < a.numPatterns; ++i)
    {
        const auto& p = a.patterns[static_cast<size_t> (i)];

        // Each lane has its own cycle; the Chance lane's also waits for every 1:N count.
        std::int64_t cycle = 1;
        for (int lane = 0; lane < numLanes; ++lane)
        {
            const int length = laneLength (p, static_cast<Lane> (lane));
            auto laneCycle = directionCycle (p.direction, length);
            if (laneCycle == 0)
                return info; // random directions never repeat exactly

            if (static_cast<Lane> (lane) == Lane::chance)
                for (int s = 0; s < length; ++s)
                    if (const int chance = p.steps[static_cast<size_t> (s)].chance; chance < 0)
                        laneCycle = std::lcm (laneCycle, directionCycle (p.direction, length) * -chance);

            cycle = std::lcm (cycle, laneCycle);
        }
        patternCycle[static_cast<size_t> (i)] = cycle;
    }

    Slot slot;
    for (std::int64_t n = 1; n <= maxSlots; ++n)
    {
        sequencer.playSlot (slot);

        bool aligned = sequencer.slotIndex % sequencer.numTurns == 0;
        for (int i = 0; i < a.numPatterns && aligned; ++i)
            aligned = sequencer.cursors[static_cast<size_t> (i)].played % patternCycle[static_cast<size_t> (i)] == 0;

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

std::vector<SequencedNote> render (const Arrangement& arrangement, double fromBeat, double toBeat, const std::vector<int>& inputNotes)
{
    std::vector<SequencedNote> notes;
    Sequencer sequencer (arrangement);
    sequencer.setInputNotes (inputNotes.data(), static_cast<int> (inputNotes.size()));
    sequencer.fastForwardTo (fromBeat);

    Slot slot;
    while (sequencer.position() < toBeat - epsilon && sequencer.next (slot))
        for (int i = 0; i < slot.numNotes; ++i)
            if (const auto& n = slot.notes[static_cast<size_t> (i)]; n.start >= fromBeat - epsilon && n.start < toBeat - epsilon)
                notes.push_back (n);

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
