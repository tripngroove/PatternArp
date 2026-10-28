#include "patternarp/Pattern.h"

#include <algorithm>
#include <cmath>

namespace patternarp
{

double beatsPerStep (Timing timing)
{
    switch (timing)
    {
        case Timing::sixtyFourth:      return 0.0625;
        case Timing::thirtySecond:     return 0.125;
        case Timing::sixteenthTriplet: return 1.0 / 6.0;
        case Timing::sixteenth:        return 0.25;
        case Timing::eighthTriplet:    return 1.0 / 3.0;
        case Timing::eighth:           return 0.5;
        case Timing::quarterTriplet:   return 2.0 / 3.0;
        case Timing::quarter:          return 1.0;
        case Timing::half:             return 2.0;
        case Timing::whole:            return 4.0;
    }
    return 0.25;
}

const char* timingName (Timing timing)
{
    switch (timing)
    {
        case Timing::sixtyFourth:      return "1/64";
        case Timing::thirtySecond:     return "1/32";
        case Timing::sixteenthTriplet: return "1/16T";
        case Timing::sixteenth:        return "1/16";
        case Timing::eighthTriplet:    return "1/8T";
        case Timing::eighth:           return "1/8";
        case Timing::quarterTriplet:   return "1/4T";
        case Timing::quarter:          return "1/4";
        case Timing::half:             return "1/2";
        case Timing::whole:            return "1/1";
    }
    return "1/16";
}

int laneLength (const Pattern& p, Lane lane)
{
    const int own = p.laneLength[static_cast<size_t> (lane)];
    return own > 0 ? std::min (own, p.length) : p.length;
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

    // Steps pick from the enabled notes, lowest to highest, so a new note shifts the others:
    // point every learned step at its note again.
    std::array<int, 128> rank {};
    int numEnabled = 0;
    for (int n = 0; n < 128; ++n)
        if (p.enabledNotes.test (static_cast<size_t> (n)))
            rank[static_cast<size_t> (n)] = numEnabled++;

    for (int i = 0; i < count; ++i)
        p.steps[static_cast<size_t> (i)].note =
            numEnabled > 1 ? static_cast<float> (rank[static_cast<size_t> (played[static_cast<size_t> (i)])]) / static_cast<float> (numEnabled - 1) : 0.0f;
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
    }
}

Pattern makeDefaultPattern()
{
    Pattern p;
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

    if (orderLength < maxOrderSteps)
        ++orderLength;
    order[static_cast<size_t> (orderLength - 1)] = numPatterns++;
    quiet[static_cast<size_t> (orderLength - 1)] = false;
    pauseFullyQuietPatterns(); // in case the last step was another pattern's only lit turn
    return true;
}

void Arrangement::removePattern (int index)
{
    if (index < 0 || index >= numPatterns)
        return;

    std::move (patterns.begin() + index + 1, patterns.begin() + numPatterns, patterns.begin() + index);
    --numPatterns;

    // Close up the Order lane, and renumber the steps of the patterns that moved up.
    int kept = 0;
    for (int i = 0; i < orderLength; ++i)
    {
        const int owner = order[static_cast<size_t> (i)];
        if (owner == index)
            continue;
        quiet[static_cast<size_t> (kept)] = quiet[static_cast<size_t> (i)];
        order[static_cast<size_t> (kept++)] = owner > index ? owner - 1 : owner;
    }
    orderLength = kept;

    if (orderLength == 0)
        alternate();
}

void Arrangement::alternate()
{
    orderLength = std::max (1, numPatterns);
    for (int i = 0; i < maxOrderSteps; ++i)
        order[static_cast<size_t> (i)] = numPatterns > 0 ? i % numPatterns : -1;
    quiet.fill (false);
}

void Arrangement::setOrderLength (int newLength)
{
    newLength = std::clamp (newLength, 1, maxOrderSteps);
    const int oldLength = std::clamp (orderLength, 1, maxOrderSteps);
    for (int i = oldLength; i < newLength; ++i)
    {
        order[static_cast<size_t> (i)] = order[static_cast<size_t> (i % oldLength)];
        quiet[static_cast<size_t> (i)] = quiet[static_cast<size_t> (i % oldLength)];
    }
    orderLength = newLength;
    pauseFullyQuietPatterns(); // shortening can leave a pattern only its quiet turns
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

// A pattern whose turns are all quiet is paused instead, so its play button and the host's
// Play parameter say so.
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
