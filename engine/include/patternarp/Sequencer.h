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
    int pattern = -1; // -1 for a rest
    int step = -1;
    int numNotes = 0;
    std::array<SequencedNote, 8> notes {};
};

// How long until the whole arrangement repeats exactly.
struct LoopInfo
{
    bool repeats = false;      // false when random directions are used (percent chances are ignored)
    double lengthBeats = 0.0;  // when it repeats
    std::int64_t lengthSlots = 0; // 0 when the loop length was set by hand
};


// Walks the arrangement slot by slot from the very beginning.
//
// For every slot it asks the Order lane whose turn it is, advances that pattern's own step
// cursor (Direction), and turns the step's lanes into notes. Each pattern only moves on
// when it plays, so patterns and an Order lane of different lengths drift against each other.
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

    // Plays exactly one step of the Order lane (a skipped step still uses up its turn).
    Outcome playSlot (Slot& slot);
    bool nextInCycle (Slot& slot);

    // Starts the arrangement over at `beat` (used for loop lengths set by hand).
    void startCycle (double beat);
    double loopBeats() const { return static_cast<double> (arrangement.loopLength) / ticksPerBeat; }

    struct Cursor
    {
        std::int64_t played = 0;                  // notes this pattern has contributed so far
        std::array<int, numLanes> walk {};        // position of each lane for random walk
        std::array<std::int64_t, maxSteps> visits {}; // per Chance-lane step, for 1:N
    };

    // Moves the pattern on one step; returns the step each lane is on.
    std::array<int, numLanes> advanceCursor (int patternIndex);
    bool passesChance (int patternIndex, int chanceStep);

    Arrangement arrangement; // only the playing patterns
    std::array<int, maxPatterns> sourceIndex {}; // each one's position in the full arrangement
    std::array<Cursor, maxPatterns> cursors {};
    std::array<int, maxOrderSteps> turns {}; // the Order lane, by playing pattern (-1 = rest)
    int numTurns = 0;
    std::array<std::array<int, 128>, maxPatterns> enabledNotes {}; // per pattern, ascending
    std::array<int, maxPatterns> numEnabledNotes {};
    std::int64_t slotIndex = 0;
    double nextStart = 0.0;
    double cycleStart = 0.0;
    std::uint64_t pass = 0; // which pass of a hand-set loop; random choices differ per pass
    std::array<int, 128> inputNotes {}; // ascending
    int numInputNotes = 0;
};

// Finds the loop length by working out when every pattern and the Order lane all line up
// again. Gives up (repeats = false) past `maxSlots`.
LoopInfo findLoop (const Arrangement& arrangement, std::int64_t maxSlots = 1 << 16);

// Plays the arrangement from the start for `beats` beats and returns every note that starts
// in that time, for the preview and for dragging out MIDI.
// MIDI In patterns pick from `inputNotes` (ascending).
std::vector<SequencedNote> render (const Arrangement& arrangement, double beats, const std::vector<int>& inputNotes = {});

// The notes that start between `fromBeat` and `toBeat`, at their song positions.
std::vector<SequencedNote> render (const Arrangement& arrangement, double fromBeat, double toBeat, const std::vector<int>& inputNotes);

// "6 beats", "4 bars", "1 bar 2 beats"... in the largest whole unit, assuming 4/4.
std::string describeLength (double beats);

} // namespace patternarp
