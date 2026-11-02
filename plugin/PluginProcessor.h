#pragma once

#include "patternarp/PatternEngine.h"
#include "patternarp/Variations.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <bitset>
#include <vector>

namespace patternarp
{

class PatternArpProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    PatternArpProcessor();
    ~PatternArpProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return JucePlugin_IsMidiEffect; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Used by the editor, on the message thread ------------------------------------

    // The longest loop the handle (and the Loop Length parameter) can set: 16 bars.
    static constexpr double maxLoopBeats = 64.0;

    // The patterns as they are now, including any host automation of their settings.
    Arrangement getArrangement() const;

    // Replaces the patterns; the audio thread picks the change up on its next block. Settings
    // the host can automate are sent to their parameters.
    void setArrangement (const Arrangement& newArrangement);

    juce::AudioParameterBool& getLatchParameter() { return *latch; }

    // --- Variations ---------------------------------------------------------------------

    // Which variations hold something, which one is being edited and played, which one a
    // click is waiting to switch to (-1: none) and which one the Variation parameter asks
    // for (it may be empty, and then the current one keeps playing).
    struct VariationStatus
    {
        std::array<bool, maxVariations> filled {};
        int active = 0;
        int waiting = -1;
        int requested = 0;
        bool operator== (const VariationStatus&) const = default;
    };
    VariationStatus getVariationStatus() const;

    // A click on a variation. An empty one first gets a copy of the current one. While the
    // song plays the switch waits for `timing`; otherwise it happens now. Clicking the
    // current one cancels a switch that is waiting.
    void selectVariation (int index, SwitchTiming timing);
    void copyVariation (int from, int to);
    void copyVariationToEmpty (int from);
    void clearVariation (int index); // not the current one

    // The default velocity and gate for new and reset steps are one setting for the whole
    // project: sets them in every variation but the current one (the editor sets that one).
    void setOtherVariationsStepDefaults (int velocity, int gate);

    // The incoming notes MIDI In patterns are using now (held or latched), for display.
    std::bitset<128> getInputNotes() const;

    // Notes played in since the last call, in the order they came in, for MIDI Learn.
    std::vector<int> takePlayedNotes();

    // Where playback is, in beats from the song start, or -1 when nothing is playing.
    double getPlayPosition() const { return playPosition.load (std::memory_order_relaxed); }

    // Fires when the arrangement is replaced from outside the editor (project or preset load).
    juce::ChangeBroadcaster arrangementReplaced;

private:
    static BusesProperties makeBuses();
    void timerCallback() override; // follows switches the audio thread made
    void switchVariation (int index); // message thread: leave the current one as it is, take up `index`
    void sendVariations(); // to the audio thread
    void addEvents (double blockStartPpq, double samplesPerBeat, int numSamples, juce::MidiBuffer& out);

    // Each pattern slot's settings and lane lengths are host parameters, so they can be
    // automated, and so are the loop and Rotation lengths. The step lanes and note selection
    // are not: they live only in the arrangement.
    struct PatternParameters
    {
        juce::AudioParameterChoice* timing = nullptr;
        juce::AudioParameterInt* length = nullptr;
        juce::AudioParameterChoice* direction = nullptr;
        juce::AudioParameterChoice* noteSource = nullptr;
        juce::AudioParameterChoice* chanceMode = nullptr;
        juce::AudioParameterBool* play = nullptr;
        std::array<juce::AudioParameterInt*, numLanes> laneLength {}; // 0 = the pattern's length
        juce::AudioParameterInt* ccNumber = nullptr; // -1 = off

        // In the order of a ParameterSnapshot: the settings, the lane lengths up to CC, the
        // CC number, then the Repeat lane's length (it came later).
        std::array<juce::RangedAudioParameter*, 6 + numLanes + 1> all() const;
    };

    static constexpr int parametersPerPattern = 6 + numLanes + 1;
    static constexpr int globalsAt = maxPatterns * parametersPerPattern; // then loop length, Rotation length
    using ParameterSnapshot = std::array<int, globalsAt + 2>;

    // Saved parameter lists from earlier versions: per slot, the six settings, then (once
    // they were parameters) the six lane lengths before the CC lane, then the CC lane's
    // length and number before the Repeat lane, then the two globals. Each newer list adds
    // to the end of a slot, so the values an older one has stay in place.
    static constexpr int oldParametersPerPattern = 6;
    static constexpr int preCcParametersPerPattern = 12;
    static constexpr int preRepeatParametersPerPattern = 14;

    ParameterSnapshot readParameters() const;
    static void applyParameters (const ParameterSnapshot& values, Arrangement& a);
    // Gestures tell the host a person moved the controls (so it can record them); a switch
    // of variation moves them without one.
    void pushToParameters (const Arrangement& a, bool gesture = true);
    void setParameters (const ParameterSnapshot& values);

    // Audio thread: plays variation `index` from the latest copies, with the parameter values
    // on top if they belong to it.
    void playAudioVariation (int index, const ParameterSnapshot& values);

    juce::AudioParameterBool* latch = nullptr;
    juce::AudioParameterInt* loopLength = nullptr; // in sixteenths, 0 = the natural loop
    juce::AudioParameterInt* rotationLength = nullptr;
    juce::AudioParameterChoice* variation = nullptr;
    std::array<PatternParameters, maxPatterns> patternParameters {};

    // Message thread's copy. The host parameters always belong to `variations.active`, whose
    // arrangement is the one being edited.
    Variations variations;
    Arrangement& arrangement() { return variations.arrangements[static_cast<size_t> (variations.active)]; }
    const Arrangement& arrangement() const { return variations.arrangements[static_cast<size_t> (variations.active)]; }
    int lastSeenPlaying = 0; // the audio thread's choice when the message thread last looked

    // Hand-over to the audio thread without blocking it: the audio thread swaps the two.
    juce::SpinLock pendingLock;
    std::unique_ptr<Variations> pendingVariations = std::make_unique<Variations>();
    std::atomic<bool> hasPendingVariations { true };

    // Audio thread: the latest variations from the message thread, the one playing, the
    // arrangement playing (with the parameter values on top) and those values.
    std::unique_ptr<Variations> audioVariations = std::make_unique<Variations>();
    int audioActive = 0;
    Arrangement audioPlaying;
    ParameterSnapshot appliedParameters {};
    int clickWaiting = -1; // a clicked variation waiting for its bar or beat
    SwitchTiming clickTiming = SwitchTiming::nextBar;

    // Between the threads.
    std::atomic<int> playingVariation { 0 }; // what the audio thread plays
    std::atomic<int> clickRequest { -1 }; // a click for the audio thread to time
    std::atomic<int> clickRequestTiming { 0 };
    std::atomic<int> waitingVariation { -1 }; // shown pulsing
    std::atomic<bool> transportPlaying { false };

    PatternEngine engine;
    std::vector<NoteEvent> events;
    juce::MidiBuffer output;

    std::array<std::atomic<std::uint64_t>, 2> inputMask {}; // written by the audio thread

    // Note-ons from the audio thread to the editor. Notes that arrive while it is full (the
    // editor is closed) are dropped.
    juce::AbstractFifo playedFifo { 256 };
    std::array<std::uint8_t, 256> playedNotes {};
    std::atomic<double> playPosition { -1.0 };

    double sampleRate = 44100.0;
    bool wasPlaying = false;
    bool lastLatch = false;
    bool consumeNotes = false; // a pattern uses MIDI In, so input notes do not pass through
    double freeRunPpq = 0.0; // our own clock while the host transport is stopped
    bool restartFreeRun = true;
    int midiChannel = 1; // follows the channel of the incoming notes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternArpProcessor)
};

} // namespace patternarp
