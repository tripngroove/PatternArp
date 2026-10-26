#include "patternarp/PatternEngine.h"

#include <algorithm>
#include <cmath>

namespace patternarp
{

namespace
{
constexpr double epsilon = 1.0e-9;
} // namespace

PatternEngine::PatternEngine()
{
    sounding.reserve (256);
    setArrangement (arrangement);
}

void PatternEngine::setArrangement (const Arrangement& newArrangement)
{
    arrangement = newArrangement;
    sequencer.setArrangement (arrangement);
    sequencer.setInputNotes (input.data(), numInput);
    needsResync = true;
}

void PatternEngine::setLatch (bool shouldLatch)
{
    latch = shouldLatch;
    if (! latch && numHeld == 0)
        updateInput();
}

void PatternEngine::updateInput()
{
    // With latch on, letting go of every key keeps the last chord.
    if (numHeld == 0 && latch)
        return;

    numInput = numHeld;
    std::copy (held.begin(), held.begin() + numHeld, input.begin());
    sequencer.setInputNotes (input.data(), numInput);
}

void PatternEngine::noteOn (int note)
{
    // The first key after letting go of a latched chord starts a new chord.
    if (latch && numHeld == 0)
        numInput = 0;

    const auto end = held.begin() + numHeld;
    if (std::find (held.begin(), end, note) == end && numHeld < static_cast<int> (held.size()))
        held[static_cast<size_t> (numHeld++)] = note;

    updateInput();
}

void PatternEngine::noteOff (int note)
{
    const auto end = held.begin() + numHeld;
    numHeld = static_cast<int> (std::remove (held.begin(), end, note) - held.begin());
    updateInput();
}

void PatternEngine::releaseAllHeld()
{
    numHeld = 0;
    numInput = 0;
    sequencer.setInputNotes (input.data(), 0);
}

void PatternEngine::resync (double ppq)
{
    sequencer.restart();
    sequencer.fastForwardTo (ppq);
    queueHead = queueSize = 0;
    needsResync = false;
}

void PatternEngine::emitNoteOffsUpTo (double ppq, bool inclusive, std::vector<NoteEvent>& out)
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

void PatternEngine::startNote (const SequencedNote& n, double ppqStart, std::vector<NoteEvent>& out)
{
    const double at = std::max (n.start, ppqStart);
    emitNoteOffsUpTo (at, true, out);

    // Retriggering a note that is still sounding: end it first so hosts and hardware see a
    // clean off/on pair.
    const auto same = std::find_if (sounding.begin(), sounding.end(), [&] (const SoundingNote& s) { return s.note == n.note; });
    if (same != sounding.end())
    {
        out.push_back ({ at, same->note, 0 });
        sounding.erase (same);
    }

    out.push_back ({ at, n.note, std::clamp (n.velocity, 1, 127) });
    if (sounding.size() < sounding.capacity())
        sounding.push_back ({ n.note, n.start + n.length });
}

void PatternEngine::stopAll (double ppq, std::vector<NoteEvent>& out)
{
    for (const auto& s : sounding)
        out.push_back ({ ppq, s.note, 0 });
    sounding.clear();
    expectedNextPpq = -1.0;
    needsResync = true;
}

void PatternEngine::process (double ppqStart, double ppqEnd, std::vector<NoteEvent>& out)
{
    if (expectedNextPpq >= 0.0 && std::abs (ppqStart - expectedNextPpq) > 1.0e-6)
        stopAll (ppqStart, out);

    expectedNextPpq = ppqEnd;

    if (ppqEnd <= ppqStart)
        return;

    if (needsResync)
        resync (ppqStart);

    for (;;)
    {
        while (queueHead < queueSize && queue[static_cast<size_t> (queueHead)].start < ppqEnd - epsilon)
        {
            startNote (queue[static_cast<size_t> (queueHead)], ppqStart, out);
            ++queueHead;
        }

        if (queueHead < queueSize || sequencer.position() >= ppqEnd - epsilon)
            break;

        Slot slot;
        if (! sequencer.next (slot))
            break;

        queueHead = 0;
        queueSize = slot.numNotes;
        std::copy (slot.notes.begin(), slot.notes.begin() + slot.numNotes, queue.begin());
    }

    emitNoteOffsUpTo (ppqEnd, false, out);
}

} // namespace patternarp
