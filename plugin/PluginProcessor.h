#pragma once

#include "patternarp/PatternEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <bitset>
#include <vector>

namespace patternarp
{

class PatternArpProcessor final : public juce::AudioProcessor
{
public:
    PatternArpProcessor();

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

    // The patterns as they are now, including any host automation of their settings.
    Arrangement getArrangement() const;

    // Replaces the patterns; the audio thread picks the change up on its next block. Settings
    // the host can automate are sent to their parameters.
    void setArrangement (const Arrangement& newArrangement);

    juce::AudioParameterBool& getLatchParameter() { return *latch; }

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
    void addEvents (double blockStartPpq, double samplesPerBeat, int numSamples, juce::MidiBuffer& out);

    // Each pattern slot's settings are host parameters, so they can be automated. The step
    // lanes and note selection are not: they live only in the arrangement.
    struct PatternParameters
    {
        juce::AudioParameterChoice* timing = nullptr;
        juce::AudioParameterInt* length = nullptr;
        juce::AudioParameterChoice* direction = nullptr;
        juce::AudioParameterChoice* noteSource = nullptr;
        juce::AudioParameterChoice* chanceMode = nullptr;
        juce::AudioParameterBool* play = nullptr;
    };

    static constexpr int parametersPerPattern = 6;
    using ParameterSnapshot = std::array<int, maxPatterns * parametersPerPattern>;

    ParameterSnapshot readParameters() const;
    static void applyParameters (const ParameterSnapshot& values, Arrangement& a);
    void pushToParameters (const Arrangement& a);
    void setParameters (const ParameterSnapshot& values);

    juce::AudioParameterBool* latch = nullptr;
    std::array<PatternParameters, maxPatterns> patternParameters {};

    Arrangement arrangement; // message thread's copy

    // Hand-over to the audio thread without blocking it.
    juce::SpinLock pendingLock;
    Arrangement pendingArrangement;
    std::atomic<bool> hasPendingArrangement { true };

    // Audio thread: the latest arrangement from the editor, and the parameter values last
    // applied on top of it.
    Arrangement audioArrangement;
    Arrangement audioPlaying;
    ParameterSnapshot appliedParameters {};

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
