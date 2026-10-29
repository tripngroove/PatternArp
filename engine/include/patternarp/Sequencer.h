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

    // The first note of a step whose pattern sends a CC carries it, to go out just before
    // the note: the CC number (-1 for none) and the CC lane's value.
    int controller = -1;
    int controllerValue = 0;
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

    // Set when the step lost its chance in extend mode: where its note would have ended. The
    // note before it is held until then.
    double tieEnd = 0.0;
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

    // Skips ahead until the next slot starts at or after `beat`. Returns the slot under way
    // at `beat` (empty if none): its later notes, a ratchet's, can still be to come.
    Slot fastForwardTo (double beat);

private:
    friend LoopInfo findLoop (const Arrangement&, const std::vector<int>&, std::int64_t);

    enum class Outcome
    {
        played,
        rest,
        skipped
    };

    // Plays exactly one step of the Order lane (a skipped step still uses up its turn).
    Outcome playSlot (Slot& slot);
    bool nextInCycle (Slot& slot);

    // The next slot, cut at the end of a hand-set loop. next() also runs this on a copy to
    // see steps coming that will hold the current note.
    void produce (Slot& slot);

    // Starts the arrangement over at `beat` (used for loop lengths set by hand).
    void startCycle (double beat);
    double loopBeats() const { return static_cast<double> (arrangement.loopLength) / ticksPerBeat; }

    struct Cursor
    {
        std::int64_t played = 0;                  // notes this pattern has contributed so far
        std::array<int, numLanes> walk {};        // position of each lane for random walk
        std::array<std::int64_t, maxSteps> visits {}; // per Chance-lane step, for A:B ratios
    };

    // Moves the pattern on one step; returns the step each lane is on.
    std::array<int, numLanes> advanceCursor (int patternIndex);
    bool passesChance (int patternIndex, int chanceStep);

    Arrangement arrangement; // only the playing patterns
    std::array<int, maxPatterns> sourceIndex {}; // each one's position in the full arrangement
    std::array<Cursor, maxPatterns> cursors {};
    std::array<int, maxOrderSteps> turns {}; // the Order lane, by playing pattern (-1 = rest)
    std::array<double, maxOrderSteps> restBeats {}; // how long each rest lasts (0 = the shortest step)
    int numTurns = 0;
    std::array<std::array<int, 128>, maxPatterns> enabledNotes {}; // per pattern, ascending
    std::array<int, maxPatterns> numEnabledNotes {};
    std::int64_t slotIndex = 0;
    double nextStart = 0.0;
    double cycleStart = 0.0;
    std::uint64_t pass = 0; // which pass of a hand-set loop; random choices differ per pass

    // How often each Chance-lane step comes up in one pass of a hand-set loop, so A:B ratios
    // keep counting from pass to pass.
    std::array<std::array<std::int64_t, maxSteps>, maxPatterns> visitsPerPass {};

    bool holdsNotes = false; // some playing pattern uses ChanceMode::extend
    std::array<int, 128> inputNotes {}; // ascending
    int numInputNotes = 0;
};

// Finds how often the arp repeats: the shortest stretch after which the notes come round
// again exactly. MIDI In patterns play `inputNotes` (ascending). Gives up (repeats = false)
// past `maxSlots`.
LoopInfo findLoop (const Arrangement& arrangement, const std::vector<int>& inputNotes = {}, std::int64_t maxSlots = 1 << 16);

// Plays the arrangement from the start for `beats` beats and returns every note that starts
// in that time, for the preview and for dragging out MIDI.
// MIDI In patterns pick from `inputNotes` (ascending).
std::vector<SequencedNote> render (const Arrangement& arrangement, double beats, const std::vector<int>& inputNotes = {});

// The notes that start between `fromBeat` and `toBeat`, at their song positions.
std::vector<SequencedNote> render (const Arrangement& arrangement, double fromBeat, double toBeat, const std::vector<int>& inputNotes);

// "6 beats", "4 bars", "1 bar 2 beats"... in the largest whole unit, assuming 4/4.
std::string describeLength (double beats);

} // namespace patternarp
