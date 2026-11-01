#pragma once

#include "patternarp/Sequencer.h"

#include <array>
#include <vector>

namespace patternarp
{

// A note on or off, or a CC.
struct NoteEvent
{
    double ppq; // position in beats (quarter notes) on the song timeline
    int note;
    int velocity; // 0 for note-off
    int controller = -1; // 0..127 for a CC (with its value in `velocity`) instead of a note
    bool isController() const { return controller >= 0; }
    bool isNoteOn() const { return ! isController() && velocity > 0; }
};

// Plays an arrangement in real time, locked to the song position.
//
// It plays on its own; held notes only feed patterns whose note source is MIDI In. The
// pattern runs from song position 0, so jumping around the timeline or looping always lands
// on the same notes.
//
// Nothing here allocates after construction, so it is safe to call from the audio thread.
class PatternEngine
{
public:
    PatternEngine();

    // Takes effect from the current position without cutting sounding notes.
    void setArrangement (const Arrangement& arrangement);

    // Keep the last held chord for MIDI In patterns after the keys are released.
    void setLatch (bool shouldLatch);

    void noteOn (int note);
    void noteOff (int note);
    void releaseAllHeld();
    bool hasHeldNotes() const { return numHeld > 0; }

    // True while notes are held or latched (used to free-run with the transport stopped).
    bool hasInput() const { return numInput > 0; }

    // The notes MIDI In patterns are using now (held or latched), ascending.
    const int* inputNotes() const { return input.data(); }
    int numInputNotes() const { return numInput; }

    // Generates events for the half-open range [ppqStart, ppqEnd), appending them to `out`
    // in time order. If the range does not continue from the previous call (a loop or jump),
    // sounding notes are ended at ppqStart first.
    void process (double ppqStart, double ppqEnd, std::vector<NoteEvent>& out);

    // Ends every sounding note at `ppq` (transport stop, reset).
    void stopAll (double ppq, std::vector<NoteEvent>& out);

private:
    struct SoundingNote
    {
        int note;
        double offPpq;
    };

    void resync (double ppq);
    void emitNoteOffsUpTo (double ppq, bool inclusive, std::vector<NoteEvent>& out);
    void startNote (const SequencedNote& note, double ppqStart, std::vector<NoteEvent>& out);
    void updateInput();

    Arrangement arrangement;
    Sequencer sequencer;
    bool needsResync = true;
    double expectedNextPpq = -1.0;

    // The notes of the slot under way still to start: queue.notes[queueHead] onwards. A
    // member, not on the audio thread's stack, as a slot is large.
    Slot queue;
    int queueHead = 0;

    std::array<int, 128> held {}; // keys down right now
    int numHeld = 0;
    std::array<int, 128> input {}; // what MIDI In patterns use: held, or the latched chord
    int numInput = 0;
    bool latch = false;

    std::vector<SoundingNote> sounding;
};

} // namespace patternarp
