#include "patternarp/Arpeggiator.h"

#include "patternarp/Euclid.h"

#include <algorithm>
#include <cmath>

namespace patternarp
{

namespace
{
constexpr double epsilon = 1.0e-9;

std::int64_t floorMod (std::int64_t value, std::int64_t divisor)
{
    const auto r = value % divisor;
    return r < 0 ? r + divisor : r;
}

// SplitMix64: a small, well-mixed hash so "random" picks repeat exactly at the same step.
std::uint64_t hashStep (std::int64_t step, std::uint32_t seed)
{
    auto z = static_cast<std::uint64_t> (step) ^ (static_cast<std::uint64_t> (seed) << 32);
    z += 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
} // namespace

double stepsPerBeat (Rate rate)
{
    switch (rate)
    {
        case Rate::quarter:          return 1.0;
        case Rate::eighth:           return 2.0;
        case Rate::eighthTriplet:    return 3.0;
        case Rate::sixteenth:        return 4.0;
        case Rate::sixteenthTriplet: return 6.0;
        case Rate::thirtySecond:     return 8.0;
    }
    return 4.0;
}

Arpeggiator::Arpeggiator()
{
    held.reserve (128);
    pool.reserve (128 * 4);
    sounding.reserve (128);
    rhythm.reserve (32);
}

void Arpeggiator::setSettings (const ArpSettings& newSettings)
{
    settings = newSettings;
    settings.octaves = std::clamp (settings.octaves, 1, 4);
    settings.rhythmSteps = std::clamp (settings.rhythmSteps, 1, 32);
    settings.rhythmHits = std::clamp (settings.rhythmHits, 0, settings.rhythmSteps);
    settings.gate = std::clamp (settings.gate, 0.05, 1.0);
    rhythm = euclid (settings.rhythmHits, settings.rhythmSteps, settings.rhythmRotation);
    rebuildPool();
}

void Arpeggiator::noteOn (int note, int velocity)
{
    noteOff (note);
    if (held.size() < held.capacity())
        held.push_back ({ note, std::clamp (velocity, 1, 127) });
    rebuildPool();
}

void Arpeggiator::noteOff (int note)
{
    held.erase (std::remove_if (held.begin(), held.end(), [note] (const HeldNote& h) { return h.note == note; }),
                held.end());
    rebuildPool();
}

void Arpeggiator::releaseAllHeld()
{
    held.clear();
    pool.clear();
}

void Arpeggiator::rebuildPool()
{
    pool.clear();
    for (int octave = 0; octave < settings.octaves; ++octave)
        for (const auto& h : held)
            if (h.note + 12 * octave <= 127)
                pool.push_back ({ h.note + 12 * octave, h.velocity });

    // Within each octave block, order by pitch unless the mode keeps press order.
    if (settings.pitchMode != PitchMode::asPlayed)
        std::sort (pool.begin(), pool.end(), [] (const HeldNote& a, const HeldNote& b) { return a.note < b.note; });
}

bool Arpeggiator::rhythmPlaysAtStep (std::int64_t step) const
{
    const auto length = static_cast<std::int64_t> (rhythm.size());
    return length > 0 && rhythm[static_cast<size_t> (floorMod (step, length))];
}

bool Arpeggiator::pickAtStep (std::int64_t step, HeldNote& picked) const
{
    if (pool.empty())
        return false;

    const auto length = static_cast<std::int64_t> (pool.size());
    std::int64_t index = 0;

    switch (settings.pitchMode)
    {
        case PitchMode::up:
        case PitchMode::asPlayed:
            index = floorMod (step, length);
            break;

        case PitchMode::down:
            index = length - 1 - floorMod (step, length);
            break;

        case PitchMode::upDown:
            if (length > 1)
            {
                const auto cycle = 2 * length - 2;
                const auto pos = floorMod (step, cycle);
                index = pos < length ? pos : cycle - pos;
            }
            break;

        case PitchMode::random:
            index = static_cast<std::int64_t> (hashStep (step, settings.randomSeed) % static_cast<std::uint64_t> (length));
            break;
    }

    picked = pool[static_cast<size_t> (index)];
    return true;
}

int Arpeggiator::pitchAtStep (std::int64_t step) const
{
    HeldNote picked {};
    return pickAtStep (step, picked) ? picked.note : -1;
}

void Arpeggiator::emitNoteOffsUpTo (double ppq, bool inclusive, std::vector<NoteEvent>& out)
{
    // Emit due offs earliest first. Only a handful of notes sound at once, so a repeated
    // scan is cheap and avoids allocating.
    for (;;)
    {
        auto earliest = sounding.end();
        for (auto it = sounding.begin(); it != sounding.end(); ++it)
        {
            const bool due = inclusive ? it->offPpq <= ppq + epsilon : it->offPpq < ppq - epsilon;
            if (due && (earliest == sounding.end() || it->offPpq < earliest->offPpq))
                earliest = it;
        }

        if (earliest == sounding.end())
            return;

        out.push_back ({ earliest->offPpq, earliest->note, 0 });
        sounding.erase (earliest);
    }
}

void Arpeggiator::stopAll (double ppq, std::vector<NoteEvent>& out)
{
    for (const auto& s : sounding)
        out.push_back ({ ppq, s.note, 0 });
    sounding.clear();
    expectedNextPpq = -1.0;
}

void Arpeggiator::process (double ppqStart, double ppqEnd, std::vector<NoteEvent>& out)
{
    if (expectedNextPpq >= 0.0 && std::abs (ppqStart - expectedNextPpq) > 1.0e-6)
        stopAll (ppqStart, out);

    expectedNextPpq = ppqEnd;

    if (ppqEnd <= ppqStart)
        return;

    const double spb = stepsPerBeat (settings.rate);
    const double stepLength = 1.0 / spb;

    for (auto step = static_cast<std::int64_t> (std::ceil (ppqStart * spb - epsilon));; ++step)
    {
        const double stepPpq = static_cast<double> (step) / spb;
        if (stepPpq >= ppqEnd - epsilon)
            break;

        emitNoteOffsUpTo (stepPpq, true, out);

        HeldNote picked {};
        if (! rhythmPlaysAtStep (step) || ! pickAtStep (step, picked))
            continue;

        // Retriggering a note that is still sounding: end it first so hosts and hardware
        // see a clean off/on pair.
        const auto same = std::find_if (sounding.begin(), sounding.end(),
                                        [&] (const SoundingNote& s) { return s.note == picked.note; });
        if (same != sounding.end())
        {
            out.push_back ({ stepPpq, same->note, 0 });
            sounding.erase (same);
        }

        out.push_back ({ stepPpq, picked.note, picked.velocity });
        sounding.push_back ({ picked.note, stepPpq + stepLength * settings.gate });
    }

    emitNoteOffsUpTo (ppqEnd, false, out);
}

} // namespace patternarp
