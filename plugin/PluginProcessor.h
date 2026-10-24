#pragma once

#include "patternarp/Arpeggiator.h"

#include <juce_audio_processors/juce_audio_processors.h>

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

private:
    static BusesProperties makeBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();

    ArpSettings readSettings() const;
    void addEvents (double blockStartPpq, double samplesPerBeat, int numSamples, juce::MidiBuffer& out);

    juce::AudioProcessorValueTreeState parameters;

    juce::AudioParameterChoice* rate = nullptr;
    juce::AudioParameterChoice* pitchMode = nullptr;
    juce::AudioParameterInt* octaves = nullptr;
    juce::AudioParameterFloat* gate = nullptr;
    juce::AudioParameterInt* rhythmHits = nullptr;
    juce::AudioParameterInt* rhythmSteps = nullptr;
    juce::AudioParameterInt* rhythmRotation = nullptr;

    Arpeggiator arp;
    ArpSettings lastSettings;
    bool settingsInitialised = false;

    std::vector<NoteEvent> events;
    juce::MidiBuffer output;

    double sampleRate = 44100.0;
    bool wasPlaying = false;
    double freeRunPpq = 0.0; // our own clock while the host transport is stopped
    bool restartFreeRun = true;
    int midiChannel = 1;     // follows the channel of the incoming notes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternArpProcessor)
};

} // namespace patternarp
