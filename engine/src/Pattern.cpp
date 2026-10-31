#include "patternarp/Pattern.h"

#include <algorithm>
#include <cmath>

namespace patternarp
{

double beatsPerStep (Timing timing)
{
    // Each note, then its triplet (2/3) and dotted (3/2) versions.
    switch (timing)
    {
        case Timing::sixtyFourth:         return 1.0 / 16.0;
        case Timing::thirtySecond:        return 1.0 / 8.0;
        case Timing::sixteenth:           return 1.0 / 4.0;
        case Timing::eighth:              return 1.0 / 2.0;
        case Timing::quarter:             return 1.0;
        case Timing::half:                return 2.0;
        case Timing::whole:               return 4.0;

        case Timing::sixtyFourthTriplet:  return 1.0 / 24.0;
        case Timing::thirtySecondTriplet: return 1.0 / 12.0;
        case Timing::sixteenthTriplet:    return 1.0 / 6.0;
        case Timing::eighthTriplet:       return 1.0 / 3.0;
        case Timing::quarterTriplet:      return 2.0 / 3.0;
        case Timing::halfTriplet:         return 4.0 / 3.0;
        case Timing::wholeTriplet:        return 8.0 / 3.0;

        case Timing::sixtyFourthDotted:   return 3.0 / 32.0;
        case Timing::thirtySecondDotted:  return 3.0 / 16.0;
        case Timing::sixteenthDotted:     return 3.0 / 8.0;
        case Timing::eighthDotted:        return 3.0 / 4.0;
        case Timing::quarterDotted:       return 3.0 / 2.0;
        case Timing::halfDotted:          return 3.0;
        case Timing::wholeDotted:         return 6.0;
    }
    return 0.25;
}

const char* timingName (Timing timing)
{
    switch (timing)
    {
        case Timing::sixtyFourth:         return "1/64";
        case Timing::thirtySecond:        return "1/32";
        case Timing::sixteenth:           return "1/16";
        case Timing::eighth:              return "1/8";
        case Timing::quarter:             return "1/4";
        case Timing::half:                return "1/2";
        case Timing::whole:               return "1/1";

        case Timing::sixtyFourthTriplet:  return "1/64T";
        case Timing::thirtySecondTriplet: return "1/32T";
        case Timing::sixteenthTriplet:    return "1/16T";
        case Timing::eighthTriplet:       return "1/8T";
        case Timing::quarterTriplet:      return "1/4T";
        case Timing::halfTriplet:         return "1/2T";
        case Timing::wholeTriplet:        return "1/1T";

        case Timing::sixtyFourthDotted:   return "1/64D";
        case Timing::thirtySecondDotted:  return "1/32D";
        case Timing::sixteenthDotted:     return "1/16D";
        case Timing::eighthDotted:        return "1/8D";
        case Timing::quarterDotted:       return "1/4D";
        case Timing::halfDotted:          return "1/2D";
        case Timing::wholeDotted:         return "1/1D";
    }
    return "1/16";
}

int laneLength (const Pattern& p, Lane lane)
{
    const int own = p.laneLength[static_cast<size_t> (lane)];
    return own > 0 ? std::min (own, p.length) : p.length;
}

const char* laneName (Lane lane)
{
    switch (lane)
    {
        case Lane::notes:    return "Notes";
        case Lane::velocity: return "Velocity";
        case Lane::gate:     return "Gate";
        case Lane::octave:   return "Octave";
        case Lane::ratchet:  return "Ratchet";
        case Lane::chance:   return "Chance";
        case Lane::cc:       return "CC";
    }
    return "";
}

void resetStep (Step& s, Lane lane, const Pattern& p)
{
    switch (lane)
    {
        case Lane::notes:    s.note = 0.0f; break;
        case Lane::velocity: s.velocity = p.defaultVelocity; break;
        case Lane::gate:     s.gate = p.defaultGate; break;
        case Lane::octave:   s.octave = 0; break;
        case Lane::ratchet:  s.ratchet = 1; break;
        case Lane::chance:   s.chance = 100; break;
        case Lane::cc:       s.cc = 64; break;
    }
}

namespace
{
// SplitMix64: cheap, well mixed and the same on every platform (unlike std's distributions).
struct Random
{
    std::uint64_t state;

    std::uint64_t next()
    {
        auto z = (state += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    int below (int n) { return static_cast<int> (next() % static_cast<std::uint64_t> (n)); }
};

// Steps pick from the enabled notes, lowest to highest, by a value from 0 (lowest) to 1
// (highest), so a new note shifts the others. This gives the value that picks each enabled
// note, to point steps at their notes again.
std::array<float, 128> stepValues (const std::bitset<128>& enabled)
{
    std::array<float, 128> value {};
    const auto numEnabled = static_cast<int> (enabled.count());
    int rank = 0;
    for (size_t n = 0; n < value.size(); ++n)
        if (enabled.test (n))
            value[n] = numEnabled > 1 ? static_cast<float> (rank++) / static_cast<float> (numEnabled - 1) : 0.0f;
    return value;
}
} // namespace

void randomizeLane (Pattern& p, Lane lane, std::uint64_t seed)
{
    Random random { seed };
    const int numEnabled = static_cast<int> (p.enabledNotes.count());
    constexpr int numPercents = 21; // 0%, 5% ... 100%

    for (int i = 0; i < laneLength (p, lane); ++i)
    {
        auto& s = p.steps[static_cast<size_t> (i)];
        switch (lane)
        {
            case Lane::notes:
                if (p.noteSource == NoteSource::midiIn)
                    s.note = static_cast<float> (random.below (101)) / 100.0f;
                else
                    s.note = numEnabled > 1 ? static_cast<float> (random.below (numEnabled)) / static_cast<float> (numEnabled - 1) : 0.0f;
                break;
            case Lane::velocity: s.velocity = 1 + random.below (127); break;
            case Lane::gate:     s.gate = 1 + random.below (100); break;
            case Lane::octave:   s.octave = random.below (7) - 3; break;
            case Lane::ratchet:  s.ratchet = 1 + random.below (8); break;
            case Lane::cc:       s.cc = random.below (128); break;
            case Lane::chance:
            {
                const int pick = random.below (numPercents + numChanceRatios);
                s.chance = pick < numPercents ? pick * 5 : ratioChance (chanceRatioAt (pick - numPercents));
                break;
            }
        }
    }
}

void NoteLearner::learn (Pattern& p, int note)
{
    if (note < 0 || note > 127)
        return;

    if (count == 0)
    {
        p.enabledNotes.reset();
        p.laneLength.fill (0);
        p.length = 0; // the step for this note is added, at its defaults, below
    }

    p.enabledNotes.set (static_cast<size_t> (note));
    if (count < maxSteps)
        played[static_cast<size_t> (count++)] = note;

    for (; p.length < count; ++p.length)
        for (int lane = 0; lane < numLanes; ++lane)
            resetStep (p.steps[static_cast<size_t> (p.length)], static_cast<Lane> (lane), p);
    p.storedSteps = p.length; // a take forgets steps hidden from before it

    // A new note shifts the others: point every learned step at its note again.
    const auto value = stepValues (p.enabledNotes);
    for (int i = 0; i < count; ++i)
        p.steps[static_cast<size_t> (i)].note = value[static_cast<size_t> (played[static_cast<size_t> (i)])];
}

void NoteEntry::add (Pattern& p, int note)
{
    if (note < 0 || note > 127)
        return;

    // A new entry starts in an empty Piano Roll. One under way carries on only while nothing
    // else has changed the notes or the Notes lane since its last note.
    if (p.enabledNotes.none())
    {
        active = true;
        count = 0;
    }
    else if (active && ! untouched (p))
    {
        active = false;
    }

    p.enabledNotes.set (static_cast<size_t> (note));
    if (! active)
        return;

    // Each note takes the next step of the lane until every step that plays has its own; the
    // steps not reached yet play the first note.
    if (count < laneLength (p, Lane::notes))
        entered[static_cast<size_t> (count++)] = note;

    const auto value = stepValues (p.enabledNotes);
    for (int i = 0; i < p.length; ++i)
    {
        auto& step = p.steps[static_cast<size_t> (i)];
        step.note = value[static_cast<size_t> (entered[static_cast<size_t> (i < count ? i : 0)])];
        written[static_cast<size_t> (i)] = step.note;
    }
    writtenSteps = p.length;
    enabled = p.enabledNotes;
}

bool NoteEntry::untouched (const Pattern& p) const
{
    if (p.enabledNotes != enabled)
        return false;

    // A longer pattern's new steps were never written, so only the ones that were count.
    for (int i = 0; i < std::min (p.length, writtenSteps); ++i)
        if (std::abs (p.steps[static_cast<size_t> (i)].note - written[static_cast<size_t> (i)]) > 1.0e-6f)
            return false;
    return true;
}

void copyLaneValue (const Step& from, Step& to, Lane lane)
{
    switch (lane)
    {
        case Lane::notes:    to.note = from.note; break;
        case Lane::velocity: to.velocity = from.velocity; break;
        case Lane::gate:     to.gate = from.gate; break;
        case Lane::octave:   to.octave = from.octave; break;
        case Lane::ratchet:  to.ratchet = from.ratchet; break;
        case Lane::chance:   to.chance = from.chance; break;
        case Lane::cc:       to.cc = from.cc; break;
    }
}

Pattern makeDefaultPattern (int defaultVelocity, int defaultGate)
{
    Pattern p;
    p.defaultVelocity = std::clamp (defaultVelocity, 1, 127);
    p.defaultGate = std::clamp (defaultGate, 1, 100);
    for (int note : { 60, 64, 67 })
        p.enabledNotes.set (static_cast<size_t> (note));

    // Up and down the triad: C E G E, C E G E...
    constexpr float upDown[] = { 0.0f, 0.5f, 1.0f, 0.5f };
    for (int i = 0; i < maxSteps; ++i)
    {
        auto& s = p.steps[static_cast<size_t> (i)];
        s.note = upDown[i % 4];
        s.velocity = p.defaultVelocity;
        s.gate = p.defaultGate;
    }
    return p;
}

void setPatternLength (Pattern& pattern, int newLength)
{
    newLength = std::clamp (newLength, 1, maxSteps);
    const int stored = storedSteps (pattern);

    // Steps no one has used yet repeat the pattern from its start.
    for (int i = stored; i < newLength; ++i)
        pattern.steps[static_cast<size_t> (i)] = pattern.steps[static_cast<size_t> (i % stored)];

    pattern.length = newLength;
    pattern.storedSteps = std::max (stored, newLength);
}

int storedSteps (const Pattern& p)
{
    return std::clamp (std::max (p.length, p.storedSteps), 1, maxSteps);
}

void sanitise (Pattern& p)
{
    p.timing = static_cast<Timing> (std::clamp (static_cast<int> (p.timing), 0, numTimings - 1));
    p.direction = static_cast<Direction> (std::clamp (static_cast<int> (p.direction), 0, numDirections - 1));
    p.chanceMode = static_cast<ChanceMode> (std::clamp (static_cast<int> (p.chanceMode), 0, numChanceModes - 1));
    p.noteSource = p.noteSource == NoteSource::midiIn ? NoteSource::midiIn : NoteSource::select;
    p.length = std::clamp (p.length, 1, maxSteps);
    p.storedSteps = std::clamp (p.storedSteps, 0, maxSteps);
    p.defaultVelocity = std::clamp (p.defaultVelocity, 1, 127);
    p.defaultGate = std::clamp (p.defaultGate, 1, 100);
    p.ccNumber = std::clamp (p.ccNumber, -1, 127);
    p.colour = std::clamp (p.colour, 0, maxPatterns - 1);
    p.letter = std::clamp (p.letter, 0, numLetters - 1);
    for (auto& l : p.laneLength)
        l = std::clamp (l, 0, maxSteps);

    for (auto& s : p.steps)
    {
        s.note = std::clamp (s.note, 0.0f, 1.0f);
        s.velocity = std::clamp (s.velocity, 1, 127);
        s.gate = std::clamp (s.gate, 1, 100);
        s.octave = std::clamp (s.octave, -3, 3);
        s.ratchet = std::clamp (s.ratchet, 1, 8);
        if (s.chance < 0)
        {
            auto r = chanceRatio (s.chance);
            r.b = std::clamp (r.b, 2, 8);
            r.a = std::clamp (r.a, 1, r.b);
            s.chance = ratioChance (r);
        }
        else
        {
            s.chance = std::clamp (s.chance, 0, 100);
        }
        s.cc = std::clamp (s.cc, 0, 127);
    }
}

namespace
{
// A pattern that walks up `notes` (ascending MIDI note numbers), one per step.
Pattern makeRisingPattern (std::initializer_list<int> notes, int colourAndLetter)
{
    Pattern p = makeDefaultPattern();
    p.enabledNotes.reset();
    for (int note : notes)
        p.enabledNotes.set (static_cast<size_t> (note));

    p.length = static_cast<int> (notes.size());
    for (int i = 0; i < maxSteps; ++i)
    {
        const int step = i % p.length; // steps past the end repeat, in case it is lengthened
        p.steps[static_cast<size_t> (i)].note = p.length > 1 ? static_cast<float> (step) / static_cast<float> (p.length - 1) : 0.0f;
    }
    p.colour = colourAndLetter;
    p.letter = colourAndLetter;
    return p;
}
} // namespace

Arrangement::Arrangement()
{
    // Two patterns of different lengths taking turns, so the stacking is heard straight away:
    // C3 D3 E3 against F4 G4 A4 B4 (C3 = MIDI 60, as in most DAWs).
    patterns[0] = makeRisingPattern ({ 60, 62, 64 }, 0);
    patterns[1] = makeRisingPattern ({ 77, 79, 81, 83 }, 1);
    numPatterns = 2;
    alternate();
}

int Arrangement::nextLetter() const
{
    const auto used = patterns.begin() + numPatterns;
    int highest = -1;
    for (auto p = patterns.begin(); p != used; ++p)
        highest = std::max (highest, p->letter);
    if (highest + 1 < numLetters)
        return highest + 1;

    for (int letter = 0; letter < numLetters; ++letter)
        if (std::none_of (patterns.begin(), used, [letter] (const Pattern& p) { return p.letter == letter; }))
            return letter;
    return 0;
}

bool Arrangement::addPattern (const Pattern& pattern)
{
    if (numPatterns >= maxPatterns)
        return false;

    const int letter = nextLetter();
    auto& added = patterns[static_cast<size_t> (numPatterns)];
    added = pattern;
    added.colour = unusedColour();
    added.letter = letter;

    if (numPatterns == 0)
    {
        // Nothing had a turn (every pattern was deleted): the new one takes every step, and
        // the lane keeps its length.
        numPatterns = 1;
        order.fill (0);
        quiet.fill (false);
        storedTurns = 0;
        return true;
    }

    if (orderLength < maxOrderSteps)
        ++orderLength;
    order[static_cast<size_t> (orderLength - 1)] = numPatterns++;
    quiet[static_cast<size_t> (orderLength - 1)] = false;
    storedTurns = std::max (storedTurns, orderLength);
    pauseFullyQuietPatterns(); // in case the last step was another pattern's only lit turn
    return true;
}

void Arrangement::setStepDefaults (int velocity, int gate)
{
    for (auto& p : patterns)
    {
        p.defaultVelocity = std::clamp (velocity, 1, 127);
        p.defaultGate = std::clamp (gate, 1, 100);
    }
}

int Arrangement::defaultVelocity() const
{
    return numPatterns > 0 ? patterns[0].defaultVelocity : Pattern {}.defaultVelocity;
}

int Arrangement::defaultGate() const
{
    return numPatterns > 0 ? patterns[0].defaultGate : Pattern {}.defaultGate;
}

void Arrangement::removePattern (int index)
{
    if (index < 0 || index >= numPatterns)
        return;

    std::move (patterns.begin() + index + 1, patterns.begin() + numPatterns, patterns.begin() + index);
    --numPatterns;

    // Close up the Order lane (hidden turns too), and renumber the steps of the patterns that
    // moved up.
    const int stored = numStoredTurns();
    int kept = 0, keptShown = 0;
    for (int i = 0; i < stored; ++i)
    {
        const int owner = order[static_cast<size_t> (i)];
        if (owner == index)
            continue;
        quiet[static_cast<size_t> (kept)] = quiet[static_cast<size_t> (i)];
        order[static_cast<size_t> (kept++)] = owner > index ? owner - 1 : owner;
        if (i < orderLength)
            ++keptShown;
    }
    orderLength = keptShown;
    storedTurns = kept;

    if (orderLength == 0)
        alternate();
}

void Arrangement::alternate()
{
    orderLength = std::max (1, numPatterns);
    for (int i = 0; i < maxOrderSteps; ++i)
        order[static_cast<size_t> (i)] = numPatterns > 0 ? i % numPatterns : -1;
    quiet.fill (false);
    storedTurns = 0;
}

void Arrangement::setOrderLength (int newLength)
{
    newLength = std::clamp (newLength, 1, maxOrderSteps);
    const int stored = numStoredTurns();
    for (int i = stored; i < newLength; ++i)
    {
        order[static_cast<size_t> (i)] = order[static_cast<size_t> (i % stored)];
        quiet[static_cast<size_t> (i)] = quiet[static_cast<size_t> (i % stored)];
    }
    orderLength = newLength;
    storedTurns = std::max (stored, newLength);
}

int Arrangement::numStoredTurns() const
{
    return std::clamp (std::max (orderLength, storedTurns), 1, maxOrderSteps);
}

bool Arrangement::isHeard (int turn) const
{
    if (turn < 0 || turn >= orderLength)
        return false;
    const int owner = order[static_cast<size_t> (turn)];
    return owner >= 0 && owner < numPatterns && patterns[static_cast<size_t> (owner)].playing && ! quiet[static_cast<size_t> (turn)];
}

void Arrangement::setPlaying (int pattern, bool play)
{
    if (pattern < 0 || pattern >= numPatterns)
        return;
    patterns[static_cast<size_t> (pattern)].playing = play;
    for (int i = 0; i < maxOrderSteps; ++i)
        if (order[static_cast<size_t> (i)] == pattern)
            quiet[static_cast<size_t> (i)] = false;
}

void Arrangement::setQuiet (int turn, bool shouldBeQuiet)
{
    if (turn < 0 || turn >= orderLength)
        return;
    const int owner = order[static_cast<size_t> (turn)];
    if (owner < 0 || owner >= numPatterns)
        return;

    auto& p = patterns[static_cast<size_t> (owner)];
    if (! shouldBeQuiet && ! p.playing)
    {
        // Every turn of a paused pattern is quiet: play it again with only this one lit.
        p.playing = true;
        for (int i = 0; i < orderLength; ++i)
            if (order[static_cast<size_t> (i)] == owner)
                quiet[static_cast<size_t> (i)] = true;
    }
    quiet[static_cast<size_t> (turn)] = shouldBeQuiet;
    pauseFullyQuietPatterns();
}

void Arrangement::setTurn (int turn, int pattern)
{
    if (turn < 0 || turn >= orderLength || pattern < 0 || pattern >= numPatterns)
        return;
    order[static_cast<size_t> (turn)] = pattern;
    quiet[static_cast<size_t> (turn)] = true;
    setQuiet (turn, false);
}

void Arrangement::pauseFullyQuietPatterns()
{
    for (int p = 0; p < numPatterns; ++p)
    {
        bool hasTurns = false, anyLit = false;
        for (int i = 0; i < orderLength; ++i)
        {
            if (order[static_cast<size_t> (i)] != p)
                continue;
            hasTurns = true;
            anyLit = anyLit || ! quiet[static_cast<size_t> (i)];
        }
        if (hasTurns && ! anyLit && patterns[static_cast<size_t> (p)].playing)
            setPlaying (p, false);
    }
}

void sanitise (Arrangement& a)
{
    a.numPatterns = std::clamp (a.numPatterns, 0, maxPatterns);
    a.orderLength = std::clamp (a.orderLength, 1, maxOrderSteps);
    a.storedTurns = std::clamp (a.storedTurns, 0, maxOrderSteps);
    for (size_t i = 0; i < a.order.size(); ++i)
        if (a.order[i] < 0 || a.order[i] >= a.numPatterns)
        {
            a.order[i] = -1;
            a.quiet[i] = false;
        }
}

int Arrangement::unusedColour() const
{
    for (int c = 0; c < maxPatterns; ++c)
        if (std::none_of (patterns.begin(), patterns.begin() + numPatterns, [c] (const Pattern& p) { return p.colour == c; }))
            return c;
    return numPatterns % maxPatterns;
}

bool Arrangement::usesMidiIn() const
{
    for (int i = 0; i < numPatterns; ++i)
        if (patterns[static_cast<size_t> (i)].playing && patterns[static_cast<size_t> (i)].noteSource == NoteSource::midiIn)
            return true;
    return false;
}

} // namespace patternarp
