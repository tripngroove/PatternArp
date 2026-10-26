#pragma once

#include "patternarp/Pattern.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace patternarp
{

// One note the arp plays, in beats (quarter notes) from the start of the pattern.
struct SequencedNote
{
    double start;
    double length;
    int note;
    int velocity;
    int pattern; // which pattern produced it
};

// One slot of the arp: a step from one pattern, or a rest. Ratchets give several notes.
struct Slot
{
    double start = 0.0;
    double length = 0.0;
    int pattern = -1; // -1 when no pattern owned the slot
    int step = -1;
    int numNotes = 0;
    std::array<SequencedNote, 8> notes {};
};

// How long until the whole arrangement repeats exactly.
struct LoopInfo
{
    bool repeats = false;      // false when random directions are used (percent chances are ignored)
    double lengthBeats = 0.0;  // when it repeats
    std::int64_t lengthSlots = 0;
};


// Walks the arrangement slot by slot from the very beginning.
//
// For every slot it decides which pattern plays (Period), advances that pattern's own step
// cursor (Direction), and turns the step's lanes into notes. Each pattern only moves on
// when it plays, so patterns of different lengths and periods drift against each other.
// The result depends only on the arrangement and the number of slots played, so any song
// position can be reproduced exactly.
class Sequencer
{
public:
    Sequencer() = default;
    explicit Sequencer (const Arrangement& arrangement);

    void setArrangement (const Arrangement& arrangement); // also restarts from the beginning
    void restart();

    // The held notes that MIDI In patterns pick from (any order; empty = nothing held).
    void setInputNotes (const int* notes, int count);

    // Produces the next slot. Returns false if the arrangement has no patterns.
    bool next (Slot& slot);

    // Beat position where the next slot will start.
    double position() const { return nextStart; }

    // Skips ahead until the next slot starts at or after `beat`.
    void fastForwardTo (double beat);

private:
    friend LoopInfo findLoop (const Arrangement&, std::int64_t);

    enum class Outcome
    {
        played,
        rest,
        skipped
    };

    // Plays exactly one slot of the Period cycle (a skipped step still uses up its slot).
    Outcome playSlot (Slot& slot);

    struct Cursor
    {
        std::int64_t played = 0; // notes this pattern has contributed so far
        int walk = 0;            // position for random walk
        std::array<std::int64_t, maxSteps> visits {};
    };

    int ownerOfSlot (std::int64_t slot);
    int advanceCursor (int patternIndex);
    bool passesChance (int patternIndex, int step);

    Arrangement arrangement; // only the playing patterns
    std::array<int, maxPatterns> sourceIndex {}; // each one's position in the full arrangement
    std::array<Cursor, maxPatterns> cursors {};
    std::array<int, maxPatterns> owed {}; // slots a pattern lost to a higher one and gets back
    std::array<int, maxPatterns> fills {};
    std::array<std::array<int, 128>, maxPatterns> enabledNotes {}; // per pattern, ascending
    std::array<int, maxPatterns> numEnabledNotes {};
    int numFills = 0;
    std::int64_t slotIndex = 0;
    std::int64_t fillCounter = 0;
    double nextStart = 0.0;
    std::array<int, 128> inputNotes {}; // ascending
    int numInputNotes = 0;
};

// Finds the loop length by working out when every pattern, the Period cycle and the fill
// rotation all line up again. Gives up (repeats = false) past `maxSlots`.
LoopInfo findLoop (const Arrangement& arrangement, std::int64_t maxSlots = 1 << 16);

// Plays the arrangement from the start for `beats` beats and returns every note that starts
// in that time, for the preview and for dragging out MIDI.
// MIDI In patterns pick from `inputNotes` (ascending).
std::vector<SequencedNote> render (const Arrangement& arrangement, double beats, const std::vector<int>& inputNotes = {});

// "6 beats", "4 bars", "1 bar 2 beats"... in the largest whole unit, assuming 4/4.
std::string describeLength (double beats);

} // namespace patternarp
