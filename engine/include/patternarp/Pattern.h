#pragma once

#include <array>
#include <bit>
#include <bitset>
#include <cstdint>

namespace patternarp
{

constexpr int maxSteps = 16;
constexpr int maxPatterns = 8;
constexpr int maxOrderSteps = 16;
constexpr int numLetters = 26;

// Patterns are named by letter: 0 = A, 1 = B...
constexpr char patternLetter (int letter) { return static_cast<char> ('A' + letter); }

// Loop lengths are counted in 48ths of a beat, which every Timing divides into evenly.
constexpr int ticksPerBeat = 48;

// Step length of a pattern, shortest first. (A dotted 1/64 would be 4.5 ticks, so there is none.)
enum class Timing
{
    sixtyFourth,
    thirtySecond,
    sixteenthTriplet,
    thirtySecondDotted,
    sixteenth,
    eighthTriplet,
    sixteenthDotted,
    eighth,
    quarterTriplet,
    eighthDotted,
    quarter,
    quarterDotted,
    half,
    halfDotted,
    whole
};

constexpr int numTimings = 15;
double beatsPerStep (Timing timing); // in quarter notes
const char* timingName (Timing timing);

enum class Direction
{
    forward,
    backward,
    pingPong,
    random,
    randomWalk
};

constexpr int numDirections = 5;

// What a step that loses its chance roll does.
enum class ChanceMode
{
    rest,  // stays silent for the step's length
    skip,  // takes no time; the arp moves straight on
    extend // holds the note before it until this step's note would have ended
};

constexpr int numChanceModes = 3;

// An "A:B" chance plays on the A-th of every B times its step comes up, the way Elektron
// trig conditions do. Step::chance stores it as -(B + 100 * (A - 1)), so 1:N is -N.
struct ChanceRatio
{
    int a = 1, b = 2;
};

constexpr int ratioChance (ChanceRatio r) { return -(r.b + 100 * (r.a - 1)); }
constexpr ChanceRatio chanceRatio (int chance) { return { -chance / 100 + 1, -chance % 100 }; }

// The ratios the Chance lane offers, in order: 1:2, 2:2, 1:3, 2:3, 3:3 ... 8:8.
constexpr int numChanceRatios = 35;
constexpr int chanceRatioIndex (ChanceRatio r) { return (r.b - 2) * (r.b + 1) / 2 + r.a - 1; }
constexpr ChanceRatio chanceRatioAt (int index)
{
    int b = 2;
    while (index >= b)
        index -= b++;
    return { index + 1, b };
}

// Where a pattern's notes come from.
enum class NoteSource
{
    select, // the notes switched on in the pattern's keyboard
    midiIn  // the notes being held on the incoming MIDI (rests when none are held)
};

// The per-step lanes of a pattern.
enum class Lane
{
    notes,
    velocity,
    gate,
    octave,
    ratchet,
    chance,
    cc
};

constexpr int numLanes = 7;

struct Step
{
    float note = 0.0f; // 0..1: position within the pattern's enabled notes, bottom to top
    int velocity = 100; // 1..127
    int gate = 90;      // 1..100, percent of the step length
    int octave = 0;     // -3..+3
    int ratchet = 1;    // 1..8 notes in the step
    int chance = 100;   // 0..100 = percent chance; below 0 = an A:B ratio (see ChanceRatio)
    int cc = 64;        // 0..127, sent as the pattern's CC number with the step's notes

    bool operator== (const Step& other) const
    {
        // Bitwise on the float, so "unchanged" means exactly unchanged.
        return std::bit_cast<std::uint32_t> (note) == std::bit_cast<std::uint32_t> (other.note) && velocity == other.velocity
            && gate == other.gate && octave == other.octave && ratchet == other.ratchet && chance == other.chance && cc == other.cc;
    }
};

struct Pattern
{
    Timing timing = Timing::sixteenth;
    int length = 8; // 1..maxSteps

    // Steps past the length that are remembered from before the pattern was shortened (see
    // setPatternLength). Up to this many steps hold something; 0 or anything up to the
    // length means just the length.
    int storedSteps = 0;

    Direction direction = Direction::forward;
    ChanceMode chanceMode = ChanceMode::rest;

    // Notes the Notes lane picks from, as MIDI note numbers (C3 = 60), when the source is
    // `select`. With `midiIn` the held notes are used instead.
    NoteSource noteSource = NoteSource::select;
    std::bitset<128> enabledNotes;

    int defaultVelocity = 100;
    int defaultGate = 90;

    // The CC the CC lane sends, 0..127, or -1 for none.
    int ccNumber = -1;

    std::array<Step, maxSteps> steps {};

    // A lane can loop over fewer steps than the pattern (e.g. octave every 3 steps of a
    // 4-step pattern). 0 follows the pattern's length.
    std::array<int, numLanes> laneLength {};

    std::uint32_t seed = 1; // for random direction and percent chance

    bool playing = true; // a paused pattern's turns stay in the Order lane, quiet

    int colour = 0; // index into the editor's palette
    int letter = 0; // its name (see patternLetter); stays the same when others are deleted

    bool operator== (const Pattern&) const = default;
};

const char* laneName (Lane lane);

// Steps a lane loops over: its own length if set and shorter, else the pattern's.
int laneLength (const Pattern& pattern, Lane lane);

// Puts one lane of a step back to its default: the lowest note, the pattern's default velocity
// and gate, no octave shift, no ratchet, always plays.
void resetStep (Step& step, Lane lane, const Pattern& pattern);

// Copies one lane's value from a step to another, leaving the other lanes alone.
void copyLaneValue (const Step& from, Step& to, Lane lane);

// Gives each step a lane plays (up to its length) a random value, one that could have been
// drawn: an enabled note (any height, to the percent, for MIDI Thru), velocity 1..127, gate
// 1..100%, octave -3..+3, ratchet 1..8, chance in 5% steps or a ratio, CC 0..127. Other lanes
// and steps are left alone. The same seed gives the same values.
void randomizeLane (Pattern& pattern, Lane lane, std::uint64_t seed);

// A new pattern: a C major triad (C3 E3 G3) played up and down, so it sounds right away,
// with every step at the default velocity and gate.
Pattern makeDefaultPattern (int defaultVelocity = 100, int defaultGate = 90);

// Changes the number of steps at the end, like the lane handles. Shortening hides steps and
// remembers them; lengthening brings them back, and past every step the pattern has held so
// far, repeats it from its first step (looping as often as needed).
void setPatternLength (Pattern& pattern, int newLength);

// How many steps hold something: the length, or more if the pattern was shortened.
int storedSteps (const Pattern& pattern);

// MIDI Learn: a take starts the pattern afresh with the first note played (no notes, one
// step, every lane at its default and full length), then each note played is switched on and
// written into the Notes lane in the order played, growing the pattern a step at a time (up
// to maxSteps). The pattern's settings (timing, direction...) stay. Use a new learner for
// each take.
class NoteLearner
{
public:
    void learn (Pattern& pattern, int note);
    int numLearned() const { return count; }

private:
    std::array<int, maxSteps> played {};
    int count = 0;
};

// Clicking notes into an empty Piano Roll writes them into the Notes lane in the order they
// are added, keeping the pattern's length: the first note fills every step, each later one
// takes the next step, and once every step the lane plays has its own note, more notes only
// join the Piano Roll (the steps keep their pitches). Anything else that changes the notes or
// the Notes lane in between ends the entry; adding to a Piano Roll that was not empty just
// switches the note on.
class NoteEntry
{
public:
    void add (Pattern& pattern, int note);

private:
    bool untouched (const Pattern& pattern) const; // as the entry left it after its last note

    std::array<int, maxSteps> entered {};
    int count = 0;
    bool active = false;
    std::bitset<128> enabled;
    std::array<float, maxSteps> written {};
    int writtenSteps = 0;
};

// Keeps every value inside its legal range.
void sanitise (Pattern& pattern);

// Everything PatternArp plays: the stacked patterns plus global options.
struct Arrangement
{
    std::array<Pattern, maxPatterns> patterns {};
    int numPatterns = 0;

    // The Order lane: whose turn each step of the arp is, by pattern position, or -1 for a
    // rest. It loops over its first orderLength steps.
    std::array<int, maxOrderSteps> order {};
    int orderLength = 1;
    int storedTurns = 0; // like Pattern::storedSteps: turns remembered past the length

    // Quiet turns keep their place and their pattern's step length but play nothing, so
    // nothing else moves. Every turn of a paused pattern is quiet too.
    std::array<bool, maxOrderSteps> quiet {};

    // When above 0, everything starts over every loopLength ticks (see ticksPerBeat),
    // cutting the natural loop short. 0 plays the natural loop.
    int loopLength = 0;

    Arrangement();

    bool usesMidiIn() const; // a playing pattern takes its notes from MIDI In

    // The first palette colour no pattern uses yet (for a new pattern).
    int unusedColour() const;

    // The letter for a new pattern: the one after the highest in use, or once that would pass
    // Z, the first one free.
    int nextLetter() const;

    // Adds a pattern at the end, with a new colour and letter, and gives it the next step of
    // the Order lane (the last step, if the lane is already full). Returns false when there is
    // no room.
    bool addPattern (const Pattern& pattern);

    // The default velocity and gate: what new and reset steps get. They are set for every
    // pattern at once; steps already set keep their values.
    void setStepDefaults (int velocity, int gate);
    int defaultVelocity() const; // the first pattern's (they are all the same), or 100
    int defaultGate() const;

    // Deletes a pattern; its Order steps are removed and the steps after them close up.
    void removePattern (int index);

    // Makes the Order lane take every pattern in turn: A B C...
    void alternate();

    // Changes the lane's length at the end, like a pattern's Length: shortening hides turns
    // and remembers them, lengthening brings them back, then repeats the lane from its start
    // (A B becomes A B A B...).
    void setOrderLength (int newLength);
    int numStoredTurns() const; // the length, or more if the lane was shortened

    // Whether a turn plays: its pattern exists and is playing, and the turn is not quiet.
    bool isHeard (int turn) const;

    // Play / pause for a whole pattern: lights or quiets every one of its turns.
    void setPlaying (int pattern, bool play);

    // Quiets one turn or lights it again. Quieting a pattern's last lit turn pauses the
    // pattern; lighting a turn of a paused pattern plays that turn alone.
    void setQuiet (int turn, bool shouldBeQuiet);

    // Gives a turn to a pattern, lit.
    void setTurn (int turn, int pattern);
    // Pauses any pattern whose turns are all quiet, so its play button and the host's Play
    // parameter say so (setQuiet and setTurn do this themselves).
    void pauseFullyQuietPatterns();

    bool operator== (const Arrangement&) const = default;
};

// Keeps the pattern count and the Order lane inside their legal ranges (steps that name a
// pattern that does not exist become rests).
void sanitise (Arrangement& arrangement);

} // namespace patternarp
