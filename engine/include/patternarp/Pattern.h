#pragma once

#include <array>
#include <bit>
#include <bitset>
#include <cstdint>

namespace patternarp
{

constexpr int maxSteps = 16;
constexpr int maxPatterns = 8;

// Step length of a pattern, shortest first.
enum class Timing
{
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

constexpr int numTimings = 9;
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
    rest, // stays silent for the step's length
    skip  // takes no time; the arp moves straight on
};

// Where a pattern's notes come from.
enum class NoteSource
{
    select, // the notes switched on in the pattern's keyboard
    midiIn  // the notes being held on the incoming MIDI (rests when none are held)
};

struct Step
{
    float note = 0.0f; // 0..1: position within the pattern's enabled notes, bottom to top
    int velocity = 100; // 1..127
    int gate = 90;      // 1..100, percent of the step length
    int octave = 0;     // -3..+3
    int ratchet = 1;    // 1..8 notes in the step
    int chance = 100;   // 1..100 = percent chance; -2..-8 = plays every Nth pass ("1:N")

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

    // Period n:m - this pattern claims n of every m notes of the arp. n == m (e.g. 1:1)
    // makes it a fill pattern that plays the slots no other pattern claimed.
    int periodHits = 1;
    int periodSlots = 1;

    Direction direction = Direction::forward;
    ChanceMode chanceMode = ChanceMode::rest;

    // Notes the Notes lane picks from, as MIDI note numbers (C4 = 60), when the source is
    // `select`. With `midiIn` the held notes are used instead.
    NoteSource noteSource = NoteSource::select;
    std::bitset<128> enabledNotes;

    int defaultVelocity = 100;
    int defaultGate = 90;

    std::array<Step, maxSteps> steps {};

    std::uint32_t seed = 1; // for random direction and percent chance

    bool playing = true; // paused patterns are taken out of the rotation entirely

    bool isFill() const { return periodHits >= periodSlots; }
    bool operator== (const Pattern&) const = default;
};

// A new pattern: C major from C4 to C5 and a rising Notes shape, so it sounds right away.
Pattern makeDefaultPattern();

// Changes the number of steps, re-bucketing every lane so its shape is kept: smooth lanes
// (notes, velocity, gate) are interpolated, stepped lanes (octave, ratchet, chance) take
// the nearest original step.
void setPatternLength (Pattern& pattern, int newLength);

// The Period settings offered in the editor and to host automation, sparsest first.
struct Period
{
    int hits;
    int slots;
};

inline constexpr std::array<Period, 20> periodChoices { {
    { 1, 8 }, { 1, 7 }, { 1, 6 }, { 1, 5 }, { 1, 4 }, { 2, 7 }, { 1, 3 }, { 3, 8 }, { 2, 5 }, { 3, 7 },
    { 1, 2 }, { 3, 5 }, { 5, 8 }, { 2, 3 }, { 5, 7 }, { 3, 4 }, { 4, 5 }, { 5, 6 }, { 7, 8 }, { 1, 1 },
} };

// Index into periodChoices closest to hits:slots.
int nearestPeriodChoice (int hits, int slots);

// Keeps every value inside its legal range.
void sanitise (Pattern& pattern);

// Everything PatternArp plays: the stacked patterns plus global options.
struct Arrangement
{
    std::array<Pattern, maxPatterns> patterns {};
    int numPatterns = 0;

    Arrangement();

    bool usesMidiIn() const; // a playing pattern takes its notes from MIDI In
    bool operator== (const Arrangement&) const = default;
};

} // namespace patternarp
