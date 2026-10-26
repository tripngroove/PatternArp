#include "patternarp/Pattern.h"

#include <algorithm>
#include <cmath>

namespace patternarp
{

double beatsPerStep (Timing timing)
{
    switch (timing)
    {
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

Pattern makeDefaultPattern()
{
    Pattern p;
    for (int note : { 60, 62, 64, 65, 67, 69, 71, 72 })
        p.enabledNotes.set (static_cast<size_t> (note));

    for (int i = 0; i < maxSteps; ++i)
    {
        auto& s = p.steps[static_cast<size_t> (i)];
        s.note = p.length > 1 ? std::min (1.0f, static_cast<float> (i) / static_cast<float> (p.length - 1)) : 0.0f;
        s.velocity = p.defaultVelocity;
        s.gate = p.defaultGate;
    }
    return p;
}

void setPatternLength (Pattern& pattern, int newLength)
{
    newLength = std::clamp (newLength, 1, maxSteps);
    const int oldLength = std::clamp (pattern.length, 1, maxSteps);
    if (newLength == oldLength)
        return;

    const auto old = pattern.steps;

    // Sample each lane at the centre of each new step, measured in old steps.
    for (int i = 0; i < newLength; ++i)
    {
        const double pos = (static_cast<double> (i) + 0.5) * oldLength / newLength - 0.5;
        const int lo = std::clamp (static_cast<int> (std::floor (pos)), 0, oldLength - 1);
        const int hi = std::min (lo + 1, oldLength - 1);
        const double t = std::clamp (pos - lo, 0.0, 1.0);
        const int nearest = std::clamp (static_cast<int> (std::lround (pos)), 0, oldLength - 1);

        const auto& a = old[static_cast<size_t> (lo)];
        const auto& b = old[static_cast<size_t> (hi)];
        const auto& n = old[static_cast<size_t> (nearest)];
        auto& s = pattern.steps[static_cast<size_t> (i)];

        s.note = static_cast<float> (a.note + (b.note - a.note) * t);
        s.velocity = static_cast<int> (std::lround (a.velocity + (b.velocity - a.velocity) * t));
        s.gate = static_cast<int> (std::lround (a.gate + (b.gate - a.gate) * t));
        s.octave = n.octave;
        s.ratchet = n.ratchet;
        s.chance = n.chance;
    }

    // Steps past the new end keep sensible values in case the pattern grows again later.
    for (int i = newLength; i < maxSteps; ++i)
        pattern.steps[static_cast<size_t> (i)] = pattern.steps[static_cast<size_t> (newLength - 1)];

    pattern.length = newLength;
}

void sanitise (Pattern& p)
{
    p.timing = static_cast<Timing> (std::clamp (static_cast<int> (p.timing), 0, numTimings - 1));
    p.direction = static_cast<Direction> (std::clamp (static_cast<int> (p.direction), 0, numDirections - 1));
    p.noteSource = p.noteSource == NoteSource::midiIn ? NoteSource::midiIn : NoteSource::select;
    p.length = std::clamp (p.length, 1, maxSteps);
    p.periodSlots = std::clamp (p.periodSlots, 1, 16);
    p.periodHits = std::clamp (p.periodHits, 1, p.periodSlots);
    p.defaultVelocity = std::clamp (p.defaultVelocity, 1, 127);
    p.defaultGate = std::clamp (p.defaultGate, 1, 100);

    for (auto& s : p.steps)
    {
        s.note = std::clamp (s.note, 0.0f, 1.0f);
        s.velocity = std::clamp (s.velocity, 1, 127);
        s.gate = std::clamp (s.gate, 1, 100);
        s.octave = std::clamp (s.octave, -3, 3);
        s.ratchet = std::clamp (s.ratchet, 1, 8);
        if (s.chance < 0)
            s.chance = std::clamp (s.chance, -8, -2);
        else
            s.chance = std::clamp (s.chance, 1, 100);
    }
}

int nearestPeriodChoice (int hits, int slots)
{
    const double ratio = static_cast<double> (std::max (1, hits)) / std::max (1, slots);
    int best = 0;
    double bestDistance = 1e9;
    for (int i = 0; i < static_cast<int> (periodChoices.size()); ++i)
    {
        const auto& c = periodChoices[static_cast<size_t> (i)];
        if (c.hits == hits && c.slots == slots)
            return i;

        const double distance = std::abs (static_cast<double> (c.hits) / c.slots - ratio);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

Arrangement::Arrangement()
{
    patterns[0] = makeDefaultPattern();
    numPatterns = 1;
}

bool Arrangement::usesMidiIn() const
{
    for (int i = 0; i < numPatterns; ++i)
        if (patterns[static_cast<size_t> (i)].noteSource == NoteSource::midiIn)
            return true;
    return false;
}

} // namespace patternarp
