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
constexpr int maxHeldSteps = 64; // how far a note held by extend steps is followed

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
    // Sub-patterns follow their parent's notes, so their own timing plays no part.
    double shortest = 4.0;
    for (int i = 0; i < a.numPatterns; ++i)
        if (const auto& p = a.patterns[static_cast<size_t> (i)]; ! p.isSub())
            shortest = std::min (shortest, beatsPerStep (p.timing));
    return shortest;
}

// Whether every step the Repeat lane plays is set to play once.
bool repeatsNothing (const Pattern& p)
{
    for (int s = 0; s < laneLength (p, Lane::repeat); ++s)
        if (p.steps[static_cast<size_t> (s)].repeat > 1)
            return false;
    return true;
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
    // Only playing patterns get a place here; turns of paused ones are quiet (see below). A
    // sub-pattern plays only while its parent (an ordinary pattern) does.
    const int numSource = std::clamp (a.numPatterns, 0, maxPatterns);
    const auto isSubOf = [&] (int i) {
        const int parent = a.patterns[static_cast<size_t> (i)].parent;
        return parent >= 0 && parent < numSource && parent != i && ! a.patterns[static_cast<size_t> (parent)].isSub() ? parent : -1;
    };
    const auto plays = [&] (int i) {
        const auto& p = a.patterns[static_cast<size_t> (i)];
        if (! p.playing)
            return false;
        if (! p.isSub())
            return true;
        const int parent = isSubOf (i);
        return parent >= 0 && a.patterns[static_cast<size_t> (parent)].playing;
    };

    arrangement.loopLength = std::max (0, a.loopLength);
    arrangement.numPatterns = 0;
    for (int i = 0; i < numSource; ++i)
    {
        if (! plays (i))
            continue;
        sourceIndex[static_cast<size_t> (arrangement.numPatterns)] = i;
        auto& p = arrangement.patterns[static_cast<size_t> (arrangement.numPatterns++)];
        p = a.patterns[static_cast<size_t> (i)];
        sanitise (p);
    }

    std::array<int, maxPatterns> playingIndex {};
    playingIndex.fill (-1);
    for (int i = 0; i < arrangement.numPatterns; ++i)
        playingIndex[static_cast<size_t> (sourceIndex[static_cast<size_t> (i)])] = i;

    // Parents renumbered to the playing patterns too.
    for (int i = 0; i < arrangement.numPatterns; ++i)
        if (auto& p = arrangement.patterns[static_cast<size_t> (i)]; p.isSub())
            p.parent = playingIndex[static_cast<size_t> (isSubOf (sourceIndex[static_cast<size_t> (i)]))];

    // The Order lane, renumbered to the playing patterns. Quiet turns (all of a paused
    // pattern's) stay, as rests as long as their pattern's steps, so nothing else moves.
    // Sub-patterns take no turns: a turn given to one is a rest.
    numTurns = 0;
    for (int i = 0; i < std::clamp (a.orderLength, 0, maxOrderSteps); ++i)
    {
        const int owner = a.order[static_cast<size_t> (i)];
        const bool known = owner >= 0 && owner < numSource && ! a.patterns[static_cast<size_t> (owner)].isSub();
        const bool heard = known && a.isHeard (i);
        turns[static_cast<size_t> (numTurns)] = heard ? playingIndex[static_cast<size_t> (owner)] : -1;
        restBeats[static_cast<size_t> (numTurns)] = known && ! heard ? beatsPerStep (a.patterns[static_cast<size_t> (owner)].timing) : 0.0;
        ++numTurns;
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

    holdsNotes = mainHoldsNotes = false;
    for (int i = 0; i < arrangement.numPatterns; ++i)
    {
        const auto& p = arrangement.patterns[static_cast<size_t> (i)];
        holdsNotes = holdsNotes || p.chanceMode == ChanceMode::extend;
        mainHoldsNotes = mainHoldsNotes || (p.chanceMode == ChanceMode::extend && ! p.isSub());
    }

    // A hand-set loop starts over every pass, so measure how often each Chance-lane step
    // comes up in one pass; A:B ratios carry their count on from there.
    visitsPerPass = {};
    if (const double loop = loopBeats(); loop > 0.0 && arrangement.numPatterns > 0 && numTurns > 0)
    {
        Sequencer probe = *this;
        probe.startCycle (0.0);
        Slot slot;
        while (probe.nextStart < loop - epsilon)
            probe.nextInCycle (slot);
        for (int i = 0; i < arrangement.numPatterns; ++i)
            visitsPerPass[static_cast<size_t> (i)] = probe.cursors[static_cast<size_t> (i)].visits;
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
    slotIndex = 0;
    cycleStart = beat;
    nextStart = beat;
    const double loop = loopBeats();
    pass = loop > 0.0 ? static_cast<std::uint64_t> (std::llround (beat / loop)) : 0;

    // A:B ratios count on from earlier passes. 840 is a multiple of every B, so the count
    // keeps its exact remainder without growing.
    cursors = {};
    for (int i = 0; i < arrangement.numPatterns; ++i)
        for (size_t s = 0; s < maxSteps; ++s)
            cursors[static_cast<size_t> (i)].visits[s] = static_cast<std::int64_t> (pass % 840) * visitsPerPass[static_cast<size_t> (i)][s];
}

bool Sequencer::advanceCursor (int patternIndex)
{
    const auto& p = arrangement.patterns[static_cast<size_t> (patternIndex)];
    auto& c = cursors[static_cast<size_t> (patternIndex)];

    // A repeating step uses up this turn as it is.
    if (c.repeatsLeft > 0)
    {
        --c.repeatsLeft;
        return false;
    }

    // Every lane walks the pattern's direction over its own length, so lanes of the same
    // length always stay on the same step.
    for (int lane = 0; lane < numLanes; ++lane)
        if (! movesPerNote (p, static_cast<Lane> (lane)))
            moveLane (patternIndex, static_cast<Lane> (lane), c.moves);

    ++c.moves;
    ++c.visits[static_cast<size_t> (c.at[static_cast<size_t> (Lane::chance)])];
    c.repeatsLeft = p.steps[static_cast<size_t> (c.at[static_cast<size_t> (Lane::repeat)])].repeat - 1;
    return true;
}

void Sequencer::advanceNoteLanes (int patternIndex)
{
    const auto& p = arrangement.patterns[static_cast<size_t> (patternIndex)];
    auto& c = cursors[static_cast<size_t> (patternIndex)];
    for (int lane = 0; lane < numLanes; ++lane)
        if (movesPerNote (p, static_cast<Lane> (lane)))
            moveLane (patternIndex, static_cast<Lane> (lane), c.notes);
    ++c.notes;
}

void Sequencer::moveLane (int patternIndex, Lane lane, std::int64_t n)
{
    const auto& p = arrangement.patterns[static_cast<size_t> (patternIndex)];
    auto& c = cursors[static_cast<size_t> (patternIndex)];
    const std::int64_t length = laneLength (p, lane);
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

    c.at[static_cast<size_t> (lane)] = static_cast<int> (step);
}

bool Sequencer::passesChance (int patternIndex, int step)
{
    const auto& p = arrangement.patterns[static_cast<size_t> (patternIndex)];
    const auto& c = cursors[static_cast<size_t> (patternIndex)];
    const int chance = p.steps[static_cast<size_t> (step)].chance;

    if (chance >= 100)
        return true;

    if (chance >= 0)
        return static_cast<int> (hash (p.seed + 0x9e37u + pass * passSalt, static_cast<std::uint64_t> (c.moves)) % 100u) < chance;

    // A:B plays on the A-th of every B times this step comes up.
    const auto ratio = chanceRatio (chance);
    return floorMod (c.visits[static_cast<size_t> (step)] - 1, ratio.b) == ratio.a - 1;
}

Sequencer::Outcome Sequencer::playSlot (Slot& slot)
{
    const auto turn = static_cast<size_t> (slotIndex++ % numTurns);
    const int owner = turns[turn];

    slot.start = nextStart;
    slot.pattern = owner < 0 ? -1 : sourceIndex[static_cast<size_t> (owner)];
    slot.step = -1;
    slot.numNotes = 0;
    slot.tieEnd = 0.0;
    slot.subTieEnd = {};

    if (owner < 0)
    {
        slot.length = restBeats[turn] > 0.0 ? restBeats[turn] : shortestStep (arrangement);
        nextStart += slot.length;
        return Outcome::rest;
    }

    const auto& p = arrangement.patterns[static_cast<size_t> (owner)];
    const double length = beatsPerStep (p.timing);
    auto& cursor = cursors[static_cast<size_t> (owner)];
    const auto& steps = cursor.at;
    const auto stepOf = [&] (Lane lane) -> const Step& { return p.steps[static_cast<size_t> (steps[static_cast<size_t> (lane)])]; };

    // Chance is rolled when the pattern moves on to a step, and stands for all its repeats.
    if (advanceCursor (owner))
        cursor.passes = passesChance (owner, steps[static_cast<size_t> (Lane::chance)]);

    slot.step = steps[static_cast<size_t> (Lane::notes)];
    slot.length = length;

    // Lanes in Note mode move on for every note the step would play, heard or not, so a
    // failed chance only silences notes and never shifts what the ones after it get.
    const int ratchet = stepOf (Lane::ratchet).ratchet;
    const double sub = length / ratchet;

    if (! cursor.passes)
    {
        for (int r = 0; r < ratchet; ++r)
            advanceNoteLanes (owner);

        if (p.chanceMode == ChanceMode::skip)
            return Outcome::skipped;

        // Held until the step's last note would have ended.
        if (p.chanceMode == ChanceMode::extend)
            slot.tieEnd = nextStart + length - sub + sub * stepOf (Lane::gate).gate / 100.0;

        nextStart += length;
        return Outcome::rest;
    }

    const int pitch = pitchOf (owner, stepOf (Lane::notes));
    const bool ccPerNote = movesPerNote (p, Lane::cc);

    std::array<int, maxPatterns> lastSubNote {};
    lastSubNote.fill (-1);
    bool anySubNotes = false;

    for (int r = 0; r < ratchet; ++r)
    {
        advanceNoteLanes (owner);
        if (pitch < 0)
            continue;

        const int note = pitch + 12 * stepOf (Lane::octave).octave;
        if (note < 0 || note > 127)
            continue;

        const int index = slot.numNotes++;
        auto& n = slot.notes[static_cast<size_t> (index)];
        n = { nextStart + sub * r, std::max (sub * stepOf (Lane::gate).gate / 100.0, 0.001), note, stepOf (Lane::velocity).velocity, slot.pattern };

        // The CC lane goes out with the step's first note, or with every note in Note mode.
        if (index == 0 || ccPerNote)
        {
            n.controller = p.ccNumber;
            n.controllerValue = stepOf (Lane::cc).cc;
        }

        // Every note heard moves the pattern's sub-patterns on a step, over this note's share
        // of the step.
        for (int s = 0; s < arrangement.numPatterns; ++s)
            if (arrangement.patterns[static_cast<size_t> (s)].parent == owner)
            {
                playSubStep (s, nextStart + sub * r, sub, slot, lastSubNote);
                anySubNotes = true;
            }
    }

    // Sub-patterns' ratchets fall between the parent's notes: put everything in time order.
    if (anySubNotes)
        std::stable_sort (slot.notes.begin(), slot.notes.begin() + slot.numNotes,
                          [] (const SequencedNote& x, const SequencedNote& y) { return x.start < y.start; });

    nextStart += length;
    return Outcome::played;
}

int Sequencer::pitchOf (int patternIndex, const Step& step) const
{
    // Notes lane: pick from the enabled (or held) notes, bottom to top.
    const bool fromInput = arrangement.patterns[static_cast<size_t> (patternIndex)].noteSource == NoteSource::midiIn;
    const auto& enabled = fromInput ? inputNotes : enabledNotes[static_cast<size_t> (patternIndex)];
    const int numEnabled = fromInput ? numInputNotes : numEnabledNotes[static_cast<size_t> (patternIndex)];
    return numEnabled > 0 ? enabled[static_cast<size_t> (std::lround (step.note * static_cast<float> (numEnabled - 1)))] : -1;
}

void Sequencer::playSubStep (int subIndex, double start, double length, Slot& slot, std::array<int, maxPatterns>& lastNote)
{
    const auto& p = arrangement.patterns[static_cast<size_t> (subIndex)];
    auto& cursor = cursors[static_cast<size_t> (subIndex)];
    const auto& steps = cursor.at;
    const auto stepOf = [&] (Lane lane) -> const Step& { return p.steps[static_cast<size_t> (steps[static_cast<size_t> (lane)])]; };
    const auto source = sourceIndex[static_cast<size_t> (subIndex)];
    auto& last = lastNote[static_cast<size_t> (subIndex)];

    // The same as a pattern's turn, with the parent's note for the step.
    if (advanceCursor (subIndex))
        cursor.passes = passesChance (subIndex, steps[static_cast<size_t> (Lane::chance)]);

    const int ratchet = stepOf (Lane::ratchet).ratchet;
    const double hit = length / ratchet;

    if (! cursor.passes)
    {
        for (int r = 0; r < ratchet; ++r)
            advanceNoteLanes (subIndex);

        // There is no time to skip, so skip rests too. Extend holds the sub-pattern's own
        // note before this one: here if it is in this slot, else from an earlier slot.
        if (p.chanceMode == ChanceMode::extend)
        {
            const double tieEnd = start + length - hit + hit * stepOf (Lane::gate).gate / 100.0;
            if (last >= 0)
            {
                auto& held = slot.notes[static_cast<size_t> (last)];
                held.length = std::max (held.length, tieEnd - held.start);
            }
            else
            {
                auto& tie = slot.subTieEnd[static_cast<size_t> (source)];
                tie = std::max (tie, tieEnd);
            }
        }
        return;
    }

    const int pitch = pitchOf (subIndex, stepOf (Lane::notes));
    const bool ccPerNote = movesPerNote (p, Lane::cc);
    bool sentCc = false;

    for (int r = 0; r < ratchet; ++r)
    {
        advanceNoteLanes (subIndex);
        if (pitch < 0)
            continue;

        const int note = pitch + 12 * stepOf (Lane::octave).octave;
        const double at = start + hit * r;
        if (note < 0 || note > 127 || slot.numNotes >= maxSlotNotes)
            continue;

        // A key that already starts here (the parent's, or another sub-pattern's) is not
        // played twice: the two would cut each other short.
        const auto begin = slot.notes.begin(), end = begin + slot.numNotes;
        if (std::any_of (begin, end, [&] (const SequencedNote& n) { return n.note == note && std::abs (n.start - at) < 1.0e-6; }))
            continue;

        last = slot.numNotes++;
        auto& n = slot.notes[static_cast<size_t> (last)];
        n = { at, std::max (hit * stepOf (Lane::gate).gate / 100.0, 0.001), note, stepOf (Lane::velocity).velocity, source };

        // The CC lane goes out with the step's first note, or with every note in Note mode.
        if (! sentCc || ccPerNote)
        {
            n.controller = p.ccNumber;
            n.controllerValue = stepOf (Lane::cc).cc;
            sentCc = true;
        }
    }
}

bool Sequencer::next (Slot& slot)
{
    if (arrangement.numPatterns == 0 || numTurns == 0)
        return false;

    produce (slot);
    if (holdsNotes && slot.numNotes > 0)
        holdNotes (slot);
    return true;
}

void Sequencer::holdNotes (Slot& slot)
{
    // Steps coming up that lose their chance in extend mode hold the note before them: look
    // ahead on a copy (so held MIDI In notes are still read live) and lengthen it. The
    // patterns taking turns hold the slot's last note of its own pattern while the slots
    // straight after it are tied. Each sub-pattern in extend mode holds its own last note
    // until a slot plays a note of it, however many slots its parent sits out.
    const auto lastOf = [&slot] (int source) {
        for (int i = slot.numNotes; --i >= 0;)
            if (slot.notes[static_cast<size_t> (i)].pattern == source)
                return i;
        return -1;
    };

    const int mainHeld = mainHoldsNotes && slot.pattern >= 0 ? lastOf (slot.pattern) : -1;
    std::array<int, maxPatterns> subHeld {};
    subHeld.fill (-1);
    bool open = mainHeld >= 0;
    for (int s = 0; s < arrangement.numPatterns; ++s)
        if (const auto& p = arrangement.patterns[static_cast<size_t> (s)]; p.isSub() && p.chanceMode == ChanceMode::extend)
        {
            subHeld[static_cast<size_t> (s)] = lastOf (sourceIndex[static_cast<size_t> (s)]);
            open = open || subHeld[static_cast<size_t> (s)] >= 0;
        }
    if (! open)
        return;

    const auto hold = [&slot] (int index, double tieEnd) {
        auto& held = slot.notes[static_cast<size_t> (index)];
        held.length = std::max (held.length, tieEnd - held.start);
    };

    bool mainOpen = mainHeld >= 0;
    Sequencer ahead = *this;
    Slot tied;
    for (int i = 0; i < maxHeldSteps && open; ++i)
    {
        ahead.produce (tied);
        const bool samePass = ahead.cycleStart == cycleStart; // a new pass of a hand-set loop starts afresh
        open = false;

        if (mainOpen)
        {
            mainOpen = samePass && tied.tieEnd > 0.0;
            if (mainOpen)
                hold (mainHeld, tied.tieEnd);
            open = mainOpen;
        }

        for (int s = 0; s < arrangement.numPatterns; ++s)
        {
            auto& held = subHeld[static_cast<size_t> (s)];
            if (held < 0)
                continue;
            const int source = sourceIndex[static_cast<size_t> (s)];
            if (! samePass)
            {
                held = -1;
                continue;
            }
            if (const double tieEnd = tied.subTieEnd[static_cast<size_t> (source)]; tieEnd > 0.0)
                hold (held, tieEnd);
            const auto end = tied.notes.begin() + tied.numNotes;
            if (std::any_of (tied.notes.begin(), end, [source] (const SequencedNote& n) { return n.pattern == source; }))
                held = -1;
            open = open || held >= 0;
        }
    }
}

void Sequencer::produce (Slot& slot)
{
    const double loop = loopBeats();
    if (loop <= 0.0)
    {
        nextInCycle (slot);
        return;
    }

    const double cycleEnd = cycleStart + loop;
    const bool newPass = nextStart >= cycleEnd - epsilon;
    if (newPass)
        startCycle (cycleEnd);

    nextInCycle (slot);

    // A pass starts afresh, so its first step has no note before it to hold.
    if (newPass)
    {
        slot.tieEnd = 0.0;
        slot.subTieEnd = {};
    }

    // Nothing may sound past the end of the loop: cut the slot (and its ratchets) there.
    const double end = cycleStart + loop;
    slot.length = std::min (slot.length, end - slot.start);
    slot.tieEnd = std::min (slot.tieEnd, end);
    for (auto& tie : slot.subTieEnd)
        tie = std::min (tie, end);
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
}

bool Sequencer::nextInCycle (Slot& slot)
{
    // Skipped steps take no time, so keep going until a slot takes some (with a guard in
    // case every step is set to skip). The longest legitimate run is every step of a pattern,
    // repeated 8 times, losing to an 8:8 chance seven times over.
    for (int attempt = 0; attempt < maxSteps * 8 * 8; ++attempt)
        if (playSlot (slot) != Outcome::skipped)
            return true;

    // Everything skipped: rest for a moment so time still moves.
    slot.start = nextStart;
    slot.length = shortestStep (arrangement);
    slot.pattern = -1;
    slot.numNotes = 0;
    slot.tieEnd = 0.0;
    slot.subTieEnd = {};
    nextStart += slot.length;
    return true;
}

void Sequencer::fastForwardTo (double beat, Slot& underWay)
{
    // With a set loop length, jump straight to the loop pass that contains `beat`.
    if (const double loop = loopBeats(); loop > 0.0)
        if (const double passStart = std::floor ((beat + epsilon) / loop) * loop; passStart > cycleStart + epsilon)
            startCycle (passStart);

    underWay.numNotes = 0;
    while (nextStart < beat - epsilon && next (underWay))
    {
    }
}

namespace
{
// Every counter lining up again is when the arp is sure to repeat, but it can sound the same
// sooner (a shortened lane whose steps are all alike, say). The shortest repeat divides that
// length, so try each whole number of ticks that does against the notes themselves.
LoopInfo shortestRepeat (const Arrangement& certain, LoopInfo info, const std::vector<int>& inputNotes)
{
    const auto ticks = std::llround (info.lengthBeats * ticksPerBeat);
    if (info.lengthSlots > (1 << 13) || std::abs (static_cast<double> (ticks) - info.lengthBeats * ticksPerBeat) > 1.0e-6)
        return info;

    const auto notes = render (certain, 2.0 * info.lengthBeats, inputNotes); // in time order
    if (notes.empty())
        return info;

    // The notes of the first loop, moved on by `period`, are exactly the ones that follow.
    const auto repeatsEvery = [&] (double period) {
        size_t j = 0;
        while (j < notes.size() && notes[j].start < period - epsilon)
            ++j;
        for (size_t i = 0; i < notes.size() && notes[i].start < info.lengthBeats - epsilon; ++i, ++j)
        {
            if (j == notes.size())
                return false;
            const auto& a = notes[i];
            const auto& b = notes[j];
            if (std::abs (a.start + period - b.start) > 1.0e-6 || std::abs (a.length - b.length) > 1.0e-6 || a.note != b.note
                || a.velocity != b.velocity || a.controller != b.controller || a.controllerValue != b.controllerValue)
                return false;
        }
        return true;
    };

    for (std::int64_t d = 1; d < ticks; ++d)
    {
        if (ticks % d != 0 || ! repeatsEvery (static_cast<double> (d) / ticksPerBeat))
            continue;

        info.lengthBeats = static_cast<double> (d) / ticksPerBeat;
        Sequencer sequencer (certain);
        sequencer.setInputNotes (inputNotes.data(), static_cast<int> (inputNotes.size()));
        Slot slot;
        info.lengthSlots = 0;
        while (sequencer.position() < info.lengthBeats - epsilon && sequencer.next (slot))
            ++info.lengthSlots;
        break;
    }
    return info;
}
} // namespace

LoopInfo findLoop (const Arrangement& arrangement, const std::vector<int>& inputNotes, std::int64_t maxSlots)
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
    // step cycle (times any A:B chance), counted in steps moved on rather than turns, since a
    // step can repeat, and the cycle of its lanes in Note mode, counted in notes. The whole
    // thing repeats the first time they are all back at zero together, with no step part-way
    // through its repeats.
    std::array<std::int64_t, maxPatterns> patternCycle {};
    std::array<std::int64_t, maxPatterns> noteCycle {};

    for (int i = 0; i < a.numPatterns; ++i)
    {
        const auto& p = a.patterns[static_cast<size_t> (i)];

        // Each lane has its own cycle; the Chance lane's also waits for every A:B count.
        std::int64_t cycle = 1, notesCycle = 1;
        for (int lane = 0; lane < numLanes; ++lane)
        {
            if (static_cast<Lane> (lane) == Lane::cc && p.ccNumber < 0)
                continue; // sends nothing, so it cannot change what repeats

            if (static_cast<Lane> (lane) == Lane::repeat && repeatsNothing (p))
                continue; // every step plays once, so it cannot either

            const int length = laneLength (p, static_cast<Lane> (lane));
            auto laneCycle = directionCycle (p.direction, length);
            if (laneCycle == 0)
                return info; // random directions never repeat exactly

            if (static_cast<Lane> (lane) == Lane::chance)
                for (int s = 0; s < length; ++s)
                    if (const int chance = p.steps[static_cast<size_t> (s)].chance; chance < 0)
                        laneCycle = std::lcm (laneCycle, directionCycle (p.direction, length) * chanceRatio (chance).b);

            auto& counted = movesPerNote (p, static_cast<Lane> (lane)) ? notesCycle : cycle;
            counted = std::lcm (counted, laneCycle);
        }
        patternCycle[static_cast<size_t> (i)] = cycle;
        noteCycle[static_cast<size_t> (i)] = notesCycle;
    }

    Slot slot;
    for (std::int64_t n = 1; n <= maxSlots; ++n)
    {
        sequencer.playSlot (slot);

        bool aligned = sequencer.slotIndex % sequencer.numTurns == 0;
        for (int i = 0; i < a.numPatterns && aligned; ++i)
        {
            const auto& c = sequencer.cursors[static_cast<size_t> (i)];
            aligned = c.repeatsLeft == 0 && c.moves % patternCycle[static_cast<size_t> (i)] == 0
                   && c.notes % noteCycle[static_cast<size_t> (i)] == 0;
        }

        if (aligned)
        {
            info.repeats = true;
            info.lengthSlots = n;
            info.lengthBeats = sequencer.nextStart;
            return shortestRepeat (certain, info, inputNotes);
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
    const auto keep = [&] (const Slot& slot) {
        for (int i = 0; i < slot.numNotes; ++i)
            if (const auto& n = slot.notes[static_cast<size_t> (i)]; n.start >= fromBeat - epsilon && n.start < toBeat - epsilon)
                notes.push_back (n);
    };

    Slot slot;
    sequencer.fastForwardTo (fromBeat, slot);
    keep (slot);

    while (sequencer.position() < toBeat - epsilon && sequencer.next (slot))
        keep (slot);

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
    if (isWhole (beats * 16.0))
        return plural (std::llround (beats * 16.0), "sixty-fourth");
    if (isWhole (beats * 12.0))
        return plural (std::llround (beats * 12.0), "thirty-second triplet");
    if (isWhole (beats * 24.0))
        return plural (std::llround (beats * 24.0), "sixty-fourth triplet");
    if (isWhole (beats * 32.0 / 3.0))
        return plural (std::llround (beats * 32.0 / 3.0), "dotted sixty-fourth");

    char text[32];
    std::snprintf (text, sizeof (text), "%.2f beats", beats);
    return text;
}

} // namespace patternarp
