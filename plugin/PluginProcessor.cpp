#include "PluginProcessor.h"

namespace patternarp
{

namespace
{
const juce::Identifier stateType { "PatternArpState" };
constexpr int stateVersion = 2;

namespace ids
{
const juce::ParameterID rate { "rate", 1 };
const juce::ParameterID pitchMode { "pitchMode", 1 };
const juce::ParameterID octaves { "octaves", 1 };
const juce::ParameterID gate { "gate", 1 };
const juce::ParameterID rhythmHits { "rhythmHits", 1 };
const juce::ParameterID rhythmSteps { "rhythmSteps", 1 };
const juce::ParameterID rhythmRotation { "rhythmRotation", 1 };
} // namespace ids

bool operator== (const ArpSettings& a, const ArpSettings& b)
{
    return a.rate == b.rate && a.pitchMode == b.pitchMode && a.octaves == b.octaves && juce::exactlyEqual (a.gate, b.gate)
        && a.rhythmHits == b.rhythmHits && a.rhythmSteps == b.rhythmSteps && a.rhythmRotation == b.rhythmRotation
        && a.randomSeed == b.randomSeed;
}
} // namespace

juce::AudioProcessor::BusesProperties PatternArpProcessor::makeBuses()
{
    // A MIDI effect has no audio. The VST3/CLAP build loads as an instrument, and hosts
    // expect an instrument to have an audio output, so it gets a silent stereo one.
    if constexpr (JucePlugin_IsMidiEffect)
        return {};
    else
        return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

juce::AudioProcessorValueTreeState::ParameterLayout PatternArpProcessor::createParameters()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterChoice> (ids::rate, "Rate",
                                                        StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 3));
    layout.add (std::make_unique<AudioParameterChoice> (ids::pitchMode, "Direction",
                                                        StringArray { "Up", "Down", "Up/Down", "As Played", "Random" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (ids::octaves, "Octaves", 1, 4, 1));
    layout.add (std::make_unique<AudioParameterFloat> (ids::gate, "Gate", NormalisableRange<float> (5.0f, 100.0f, 1.0f), 50.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterInt> (ids::rhythmHits, "Rhythm Hits", 0, 32, 16));
    layout.add (std::make_unique<AudioParameterInt> (ids::rhythmSteps, "Rhythm Steps", 1, 32, 16));
    layout.add (std::make_unique<AudioParameterInt> (ids::rhythmRotation, "Rhythm Rotate", 0, 31, 0));
    return layout;
}

PatternArpProcessor::PatternArpProcessor()
    : AudioProcessor (makeBuses()),
      parameters (*this, nullptr, stateType, createParameters())
{
    rate = dynamic_cast<juce::AudioParameterChoice*> (parameters.getParameter (ids::rate.getParamID()));
    pitchMode = dynamic_cast<juce::AudioParameterChoice*> (parameters.getParameter (ids::pitchMode.getParamID()));
    octaves = dynamic_cast<juce::AudioParameterInt*> (parameters.getParameter (ids::octaves.getParamID()));
    gate = dynamic_cast<juce::AudioParameterFloat*> (parameters.getParameter (ids::gate.getParamID()));
    rhythmHits = dynamic_cast<juce::AudioParameterInt*> (parameters.getParameter (ids::rhythmHits.getParamID()));
    rhythmSteps = dynamic_cast<juce::AudioParameterInt*> (parameters.getParameter (ids::rhythmSteps.getParamID()));
    rhythmRotation = dynamic_cast<juce::AudioParameterInt*> (parameters.getParameter (ids::rhythmRotation.getParamID()));
    jassert (rate && pitchMode && octaves && gate && rhythmHits && rhythmSteps && rhythmRotation);

    events.reserve (1024);
}

void PatternArpProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    output.ensureSize (4096);
    wasPlaying = false;
    freeRunPpq = 0.0;
    restartFreeRun = true;
}

void PatternArpProcessor::releaseResources() {}

bool PatternArpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.inputBuses.isEmpty() && layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;

    if constexpr (JucePlugin_IsMidiEffect)
        return layouts.outputBuses.isEmpty() || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled();

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

ArpSettings PatternArpProcessor::readSettings() const
{
    ArpSettings s;
    s.rate = static_cast<Rate> (rate->getIndex());
    s.pitchMode = static_cast<PitchMode> (pitchMode->getIndex());
    s.octaves = octaves->get();
    s.gate = static_cast<double> (gate->get()) / 100.0;
    s.rhythmHits = rhythmHits->get();
    s.rhythmSteps = rhythmSteps->get();
    s.rhythmRotation = rhythmRotation->get();
    return s;
}

void PatternArpProcessor::addEvents (double blockStartPpq, double samplesPerBeat, int numSamples, juce::MidiBuffer& out)
{
    for (const auto& e : events)
    {
        const auto offset = juce::jlimit (0, numSamples - 1,
                                          static_cast<int> (std::round ((e.ppq - blockStartPpq) * samplesPerBeat)));
        const auto message = e.isNoteOn()
                               ? juce::MidiMessage::noteOn (midiChannel, e.note, static_cast<juce::uint8> (e.velocity))
                               : juce::MidiMessage::noteOff (midiChannel, e.note);
        out.addEvent (message, offset);
    }
    events.clear();
}

void PatternArpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    if (const auto settings = readSettings(); ! settingsInitialised || ! (settings == lastSettings))
    {
        arp.setSettings (settings);
        lastSettings = settings;
        settingsInitialised = true;
    }

    // Incoming notes feed the arp instead of passing through; everything else (CC, pitch
    // bend, aftertouch...) passes straight on to the next device.
    output.clear();
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            midiChannel = message.getChannel();
            arp.noteOn (message.getNoteNumber(), message.getVelocity());
        }
        else if (message.isNoteOff())
        {
            arp.noteOff (message.getNoteNumber());
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            arp.releaseAllHeld();
            output.addEvent (message, metadata.samplePosition);
        }
        else
        {
            output.addEvent (message, metadata.samplePosition);
        }
    }

    // Timing: follow the host's song position while it plays, otherwise run our own clock
    // at the host tempo so the arp also works for live playing with the transport stopped.
    double bpm = 120.0;
    bool isPlaying = false;
    juce::Optional<double> hostPpq;
    if (auto* ph = getPlayHead())
    {
        if (const auto position = ph->getPosition())
        {
            bpm = position->getBpm().orFallback (120.0);
            isPlaying = position->getIsPlaying();
            hostPpq = position->getPpqPosition();
        }
    }

    const double samplesPerBeat = sampleRate * 60.0 / juce::jmax (1.0, bpm);
    const double blockBeats = numSamples / samplesPerBeat;

    if (wasPlaying && ! isPlaying)
    {
        // Transport stopped: silence hanging notes at the start of this block.
        arp.stopAll (0.0, events);
        addEvents (0.0, samplesPerBeat, numSamples, output);
    }

    double startPpq = 0.0;
    if (isPlaying && hostPpq.hasValue())
    {
        startPpq = *hostPpq;
        restartFreeRun = true;
    }
    else
    {
        // Our own clock keeps running so released notes still get their note-offs, and
        // restarts from zero on the next key press so the pattern starts at once.
        if (! arp.hasHeldNotes())
        {
            restartFreeRun = true;
        }
        else if (restartFreeRun)
        {
            freeRunPpq = 0.0;
            restartFreeRun = false;
        }
        startPpq = freeRunPpq;
        freeRunPpq += blockBeats;
    }
    wasPlaying = isPlaying;

    arp.process (startPpq, startPpq + blockBeats, events);
    addEvents (startPpq, samplesPerBeat, numSamples, output);

    midi.swapWith (output);
}

juce::AudioProcessorEditor* PatternArpProcessor::createEditor()
{
    // A plain list of the controls for now; a custom lane editor comes later.
    return new juce::GenericAudioProcessorEditor (*this);
}

void PatternArpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("version", stateVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PatternArpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateType))
        return;

    parameters.replaceState (juce::ValueTree::fromXml (*xml));

    // Tell the host every value may have changed, so Bitwig refreshes its knobs. (The CLAP
    // wrapper turns a "program changed" notice into a parameter value rescan.)
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

} // namespace patternarp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new patternarp::PatternArpProcessor();
}
