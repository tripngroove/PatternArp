#pragma once

#include <cstdint>
#include <vector>

namespace patternarp
{

enum class PitchMode
{
    up,
    down,
    upDown,
    asPlayed,
    random
};

// How many arp steps fit in one beat (quarter note).
enum class Rate
{
    quarter,       // 1/4
    eighth,        // 1/8
    eighthTriplet, // 1/8T
    sixteenth,     // 1/16
    sixteenthTriplet,
    thirtySecond
};

double stepsPerBeat (Rate rate);

struct ArpSettings
{
    Rate rate = Rate::sixteenth;

    // Pitch lane: walks through the held notes, spread over `octaves` octaves.
    PitchMode pitchMode = PitchMode::up;
    int octaves = 1;

    // Rhythm lane: a Euclidean pattern deciding which steps play.
    int rhythmHits = 16;
    int rhythmSteps = 16;
    int rhythmRotation = 0;

    // Note length as a fraction of one step, 0.05 to 1.
    double gate = 0.5;

    std::uint32_t randomSeed = 1;
};

struct NoteEvent
{
    double ppq; // position in beats (quarter notes) on the song timeline
    int note;
    int velocity; // 0 for note-off
    bool isNoteOn() const { return velocity > 0; }
};

// Turns held notes into a pattern locked to the song position.
//
// Every lane is indexed by the absolute step number (song position x steps per beat),
// not by a running counter, so lanes of different lengths drift against each other and
// the same song position always produces the same note.
//
// Nothing here allocates after construction, so it is safe to call from the audio thread.
class Arpeggiator
{
public:
    Arpeggiator();

    void setSettings (const ArpSettings& newSettings);
    const ArpSettings& getSettings() const { return settings; }

    void noteOn (int note, int velocity);
    void noteOff (int note);
    void releaseAllHeld();
    bool hasHeldNotes() const { return ! held.empty(); }

    // Generates events for the half-open range [ppqStart, ppqEnd), appending them to `out`
    // in time order. If the range does not continue from the previous call (a loop or jump),
    // sounding notes are ended at ppqStart first.
    void process (double ppqStart, double ppqEnd, std::vector<NoteEvent>& out);

    // Ends every sounding note at `ppq` (transport stop, bypass, reset).
    void stopAll (double ppq, std::vector<NoteEvent>& out);

    // The note the pitch lane picks at a given absolute step, or -1 if nothing is held.
    int pitchAtStep (std::int64_t step) const;

private:
    struct HeldNote
    {
        int note;
        int velocity;
    };

    struct SoundingNote
    {
        int note;
        double offPpq;
    };

    bool rhythmPlaysAtStep (std::int64_t step) const;
    bool pickAtStep (std::int64_t step, HeldNote& picked) const;
    void rebuildPool();
    void emitNoteOffsUpTo (double ppq, bool inclusive, std::vector<NoteEvent>& out);

    ArpSettings settings;
    std::vector<HeldNote> held; // in the order they were pressed
    std::vector<HeldNote> pool; // what the pitch lane walks: held notes ordered and spread over octaves
    std::vector<SoundingNote> sounding;
    std::vector<bool> rhythm = std::vector<bool> (16, true);
    double expectedNextPpq = -1.0;
};

} // namespace patternarp
