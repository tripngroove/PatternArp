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

// Step length of a pattern, shortest first.
enum class Timing
{
    sixtyFourth,
    thirtySecond,
    sixteenthTriplet,
    sixteenth,
    eighthTriplet,
    eighth,
    quarterTriplet,
    quarter,
    half,
    whole
};

constexpr int numTimings = 10;
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
    chance
};

constexpr int numLanes = 6;

struct Step
{
    float note = 0.0f; // 0..1: position within the pattern's enabled notes, bottom to top
    int velocity = 100; // 1..127
    int gate = 90;      // 1..100, percent of the step length
    int octave = 0;     // -3..+3
    int ratchet = 1;    // 1..8 notes in the step
    int chance = 100;   // 0..100 = percent chance; below 0 = an A:B ratio (see ChanceRatio)

    bool operator== (const Step& other) const
    {
        // Bitwise on the float, so "unchanged" means exactly unchanged.
        return std::bit_cast<std::uint32_t> (note) == std::bit_cast<std::uint32_t> (other.note) && velocity == other.velocity
            && gate == other.gate && octave == other.octave && ratchet == other.ratchet && chance == other.chance;
    }
};

struct Pattern
{
    Timing timing = Timing::sixteenth;
    int length = 8; // 1..maxSteps

    Direction direction = Direction::forward;
    ChanceMode chanceMode = ChanceMode::rest;

    // Notes the Notes lane picks from, as MIDI note numbers (C3 = 60), when the source is
    // `select`. With `midiIn` the held notes are used instead.
    NoteSource noteSource = NoteSource::select;
    std::bitset<128> enabledNotes;

    int defaultVelocity = 100;
    int defaultGate = 90;

    std::array<Step, maxSteps> steps {};

    // A lane can loop over fewer steps than the pattern (e.g. octave every 3 steps of a
    // 4-step pattern). 0 follows the pattern's length.
    std::array<int, numLanes> laneLength {};

    std::uint32_t seed = 1; // for random direction and percent chance

    bool playing = true; // paused patterns are taken out of the rotation entirely

    int colour = 0; // index into the editor's palette
    int letter = 0; // its name (see patternLetter); stays the same when others are deleted

    bool operator== (const Pattern&) const = default;
};

// Steps a lane loops over: its own length if set and shorter, else the pattern's.
int laneLength (const Pattern& pattern, Lane lane);

// Puts one lane of a step back to its default: the lowest note, the pattern's default velocity
// and gate, no octave shift, no ratchet, always plays.
void resetStep (Step& step, Lane lane, const Pattern& pattern);

// A new pattern: a C major triad (C3 E3 G3) played up and down, so it sounds right away.
Pattern makeDefaultPattern();

// Changes the number of steps, re-bucketing every lane so its shape is kept: smooth lanes
// (notes, velocity, gate) are interpolated, stepped lanes (octave, ratchet, chance) take
// the nearest original step.
void setPatternLength (Pattern& pattern, int newLength);

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

    // Deletes a pattern; its Order steps are removed and the steps after them close up.
    void removePattern (int index);

    // Makes the Order lane take every pattern in turn: A B C...
    void alternate();

    // Lengthening repeats the steps there already are (A B becomes A B A B...).
    void setOrderLength (int newLength);
    bool operator== (const Arrangement&) const = default;
};

// Keeps the pattern count and the Order lane inside their legal ranges (steps that name a
// pattern that does not exist become rests).
void sanitise (Arrangement& arrangement);

} // namespace patternarp
