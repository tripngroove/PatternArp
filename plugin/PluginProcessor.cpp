#include "PluginProcessor.h"

namespace patternarp
{

namespace
{
class PlaceholderEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PlaceholderEditor (juce::AudioProcessor& p) : AudioProcessorEditor (p)
    {
        setSize (420, 160);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1e1f24));
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText ("PatternArp", getLocalBounds().removeFromTop (100), juce::Justification::centredBottom);
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("v" JucePlugin_VersionString " - MIDI passes through unchanged",
                    getLocalBounds().removeFromBottom (60), juce::Justification::centredTop);
    }
};
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

PatternArpProcessor::PatternArpProcessor() : AudioProcessor (makeBuses()) {}

void PatternArpProcessor::prepareToPlay (double, int) {}

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

void PatternArpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Phase 0: no pattern engine yet. Incoming MIDI is left in the buffer, so it passes
    // straight through to whatever comes next; the audio output stays silent.
    buffer.clear();
}

juce::AudioProcessorEditor* PatternArpProcessor::createEditor()
{
    return new PlaceholderEditor (*this);
}

namespace
{
const juce::Identifier stateType { "PatternArpState" };
constexpr int stateVersion = 1;
} // namespace

void PatternArpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Nothing to remember yet beyond a version tag. Later phases add the lane settings here.
    juce::ValueTree state (stateType);
    state.setProperty ("version", stateVersion, nullptr);

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void PatternArpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes));
    if (! state.hasType (stateType))
        return;
}

} // namespace patternarp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new patternarp::PatternArpProcessor();
}
