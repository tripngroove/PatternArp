#include "PluginProcessor.h"

#include "ArrangementState.h"
#include "PluginEditor.h"

namespace patternarp
{

namespace
{
const juce::Identifier stateType { "PatternArpState" };
// 5: 1/64 joined the timings (every timing index moved up one) and loop lengths went
//    from 24 to 48 ticks a beat.
constexpr int stateVersion = 5;

const juce::ParameterID latchId { "latch", 1 };

// The Loop Length parameter counts sixteenths, up to the 16 bars the preview shows.
constexpr int ticksPerSixteenth = ticksPerBeat / 4;
constexpr int maxLoopSixteenths = 256;

int loopSixteenths (int ticks)
{
    return std::clamp (static_cast<int> (std::lround (static_cast<double> (ticks) / ticksPerSixteenth)), 0, maxLoopSixteenths);
}

juce::String describeSixteenths (int sixteenths)
{
    if (sixteenths == 0)
        return "Natural";
    if (sixteenths % 16 == 0)
        return juce::String (sixteenths / 16) + (sixteenths == 16 ? " bar" : " bars");
    if (sixteenths % 4 == 0)
        return juce::String (sixteenths / 4) + (sixteenths == 4 ? " beat" : " beats");
    return juce::String (sixteenths) + "/16";
}

// Reads back what describeSixteenths writes (or a plain number of sixteenths).
int parseSixteenths (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    if (t.startsWith ("natural"))
        return 0;
    const int n = t.getIntValue();
    if (t.contains ("bar"))
        return n * 16;
    if (t.contains ("beat"))
        return n * 4;
    return n;
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

PatternArpProcessor::PatternArpProcessor() : AudioProcessor (makeBuses())
{
    const Arrangement initial;

    // The whole arp: Latch, and the preview's and Rotation lane's drag handles.
    auto latchParameter = std::make_unique<juce::AudioParameterBool> (latchId, "Latch", false);
    latch = latchParameter.get();
    addParameter (latchParameter.release());

    auto loopParameter = std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "looplength", 1 }, "Loop Length", 0, maxLoopSixteenths, 0,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int v, int) { return describeSixteenths (v); })
            .withValueFromStringFunction ([] (const juce::String& text) { return parseSixteenths (text); }));
    loopLength = loopParameter.get();
    addParameter (loopParameter.release());

    auto rotationParameter = std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "rotationlength", 1 }, "Rotation Length", 1,
                                                                        maxOrderSteps, initial.orderLength);
    rotationLength = rotationParameter.get();
    addParameter (rotationParameter.release());

    juce::StringArray timings, directions;
    for (int i = 0; i < numTimings; ++i)
        timings.add (timingName (static_cast<Timing> (i)));
    directions.addArray ({ "Forward", "Backward", "Ping-pong", "Random", "Random walk" });

    // Each slot's defaults match the pattern a new instance starts with there, so the host
    // sees the same values before and after the arrangement is pushed to the parameters.
    // Every slot gets its own group, which Bitwig shows as a folder.
    const auto newPattern = makeDefaultPattern();
    for (int i = 0; i < maxPatterns; ++i)
    {
        const auto slot = juce::String (i + 1);
        const auto id = [&slot] (const juce::String& name) { return juce::ParameterID ("p" + slot + "_" + name, 1); };
        // Named by slot, not letter: a slot's letter changes when an earlier pattern is deleted.
        const auto label = [&slot] (const juce::String& name) { return "P" + slot + " " + name; };
        auto& pp = patternParameters[static_cast<size_t> (i)];
        const auto& defaults = i < initial.numPatterns ? initial.patterns[static_cast<size_t> (i)] : newPattern;

        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("p" + slot, "Pattern Slot " + slot, " | ");
        auto add = [&group] (auto parameter) {
            auto* raw = parameter.get();
            group->addChild (std::move (parameter));
            return raw;
        };

        pp.timing = add (std::make_unique<juce::AudioParameterChoice> (id ("timing"), label ("Timing"), timings,
                                                                       static_cast<int> (defaults.timing)));
        pp.length = add (std::make_unique<juce::AudioParameterInt> (id ("length"), label ("Length"), 1, maxSteps, defaults.length));
        pp.direction = add (std::make_unique<juce::AudioParameterChoice> (id ("direction"), label ("Direction"), directions, 0));
        pp.noteSource = add (std::make_unique<juce::AudioParameterChoice> (id ("notesource"), label ("Note Source"),
                                                                           juce::StringArray { "Piano Roll", "MIDI Thru" }, 0));
        pp.chanceMode = add (std::make_unique<juce::AudioParameterChoice> (id ("chancemode"), label ("Chance Mode"),
                                                                           juce::StringArray { "Rest", "Skip", "Extend" }, 0));
        pp.play = add (std::make_unique<juce::AudioParameterBool> (id ("play"), label ("Play"), true));

        for (int lane = 0; lane < numLanes; ++lane)
        {
            const juce::String name = laneName (static_cast<Lane> (lane));
            pp.laneLength[static_cast<size_t> (lane)] = add (std::make_unique<juce::AudioParameterInt> (
                id (name.toLowerCase() + "length"), label (name + " Length"), 0, maxSteps, 0,
                juce::AudioParameterIntAttributes()
                    .withStringFromValueFunction ([] (int v, int) { return v == 0 ? juce::String ("Full") : juce::String (v); })
                    .withValueFromStringFunction ([] (const juce::String& text) {
                        return text.trim().equalsIgnoreCase ("full") ? 0 : text.getIntValue();
                    })));
        }

        addParameterGroup (std::move (group));
    }

    pushToParameters (arrangement);
    pendingArrangement = arrangement;
    events.reserve (1024);
}

std::array<juce::RangedAudioParameter*, 6 + numLanes> PatternArpProcessor::PatternParameters::all() const
{
    return { timing, length, direction, noteSource, chanceMode, play,
             laneLength[0], laneLength[1], laneLength[2], laneLength[3], laneLength[4], laneLength[5] };
}

PatternArpProcessor::ParameterSnapshot PatternArpProcessor::readParameters() const
{
    ParameterSnapshot values {};
    auto* v = values.data();
    for (const auto& pp : patternParameters)
        for (auto* parameter : pp.all())
            *v++ = juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue()));
    values[globalsAt] = loopLength->get();
    values[globalsAt + 1] = rotationLength->get();
    return values;
}

void PatternArpProcessor::applyParameters (const ParameterSnapshot& values, Arrangement& a)
{
    for (int i = 0; i < a.numPatterns; ++i)
    {
        const auto* v = values.data() + i * parametersPerPattern;
        auto& p = a.patterns[static_cast<size_t> (i)];
        p.timing = static_cast<Timing> (v[0]);
        setPatternLength (p, v[1]); // the same as the editor: from the end, hidden steps remembered
        p.direction = static_cast<Direction> (v[2]);
        p.noteSource = static_cast<NoteSource> (v[3]);
        p.chanceMode = static_cast<ChanceMode> (v[4]);
        p.playing = v[5] != 0;
        for (int lane = 0; lane < numLanes; ++lane)
            p.laneLength[static_cast<size_t> (lane)] = v[6 + lane];
        sanitise (p);
    }

    // The loop is set in ticks, finer than the parameter: it only takes over once moved
    // off the value it was given.
    if (const int loop = values[globalsAt]; loop != loopSixteenths (a.loopLength))
        a.loopLength = loop * ticksPerSixteenth;
    a.setOrderLength (values[globalsAt + 1]);
}

namespace
{
void setPlainValue (juce::RangedAudioParameter& parameter, int value)
{
    const auto normalised = parameter.convertTo0to1 (static_cast<float> (value));
    if (std::abs (parameter.getValue() - normalised) < 1.0e-6f)
        return;
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (normalised);
    parameter.endChangeGesture();
}
} // namespace

void PatternArpProcessor::pushToParameters (const Arrangement& a)
{
    for (int i = 0; i < a.numPatterns; ++i)
    {
        const auto& p = a.patterns[static_cast<size_t> (i)];
        auto& pp = patternParameters[static_cast<size_t> (i)];
        setPlainValue (*pp.timing, static_cast<int> (p.timing));
        setPlainValue (*pp.length, p.length);
        setPlainValue (*pp.direction, static_cast<int> (p.direction));
        setPlainValue (*pp.noteSource, static_cast<int> (p.noteSource));
        setPlainValue (*pp.chanceMode, static_cast<int> (p.chanceMode));
        setPlainValue (*pp.play, p.playing ? 1 : 0);
        for (int lane = 0; lane < numLanes; ++lane)
            setPlainValue (*pp.laneLength[static_cast<size_t> (lane)], p.laneLength[static_cast<size_t> (lane)]);
    }
    setPlainValue (*loopLength, loopSixteenths (a.loopLength));
    setPlainValue (*rotationLength, a.orderLength);
}

void PatternArpProcessor::setParameters (const ParameterSnapshot& values)
{
    const auto* v = values.data();
    for (auto& pp : patternParameters)
        for (auto* parameter : pp.all())
            setPlainValue (*parameter, *v++);
    setPlainValue (*loopLength, values[globalsAt]);
    setPlainValue (*rotationLength, values[globalsAt + 1]);
}

std::vector<int> PatternArpProcessor::takePlayedNotes()
{
    std::vector<int> notes;
    const auto read = playedFifo.read (playedFifo.getNumReady());
    read.forEach ([&] (int index) { notes.push_back (playedNotes[static_cast<size_t> (index)]); });
    return notes;
}

std::bitset<128> PatternArpProcessor::getInputNotes() const
{
    std::bitset<128> notes;
    for (size_t word = 0; word < 2; ++word)
    {
        const auto bits = inputMask[word].load (std::memory_order_relaxed);
        for (size_t b = 0; b < 64; ++b)
            if ((bits >> b) & 1u)
                notes.set (word * 64 + b);
    }
    return notes;
}

Arrangement PatternArpProcessor::getArrangement() const
{
    auto a = arrangement;
    applyParameters (readParameters(), a);
    return a;
}

void PatternArpProcessor::setArrangement (const Arrangement& newArrangement)
{
    arrangement = newArrangement;
    pushToParameters (arrangement);
    {
        const juce::SpinLock::ScopedLockType lock (pendingLock);
        pendingArrangement = arrangement;
        hasPendingArrangement = true;
    }

    // Lets the host know the project has unsaved changes.
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
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

    // Pick up edits from the editor if we can do so without waiting, and any automation.
    bool changed = false;
    if (hasPendingArrangement.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (pendingLock);
        if (lock.isLocked())
        {
            audioArrangement = pendingArrangement;
            hasPendingArrangement = false;
            changed = true;
        }
    }

    if (const auto values = readParameters(); changed || values != appliedParameters)
    {
        appliedParameters = values;
        audioPlaying = audioArrangement;
        applyParameters (values, audioPlaying);
        engine.setArrangement (audioPlaying);
        consumeNotes = audioPlaying.usesMidiIn();
    }

    if (const bool latchNow = latch->get(); latchNow != lastLatch)
    {
        engine.setLatch (latchNow);
        lastLatch = latchNow;
    }

    // Incoming notes feed MIDI In patterns. They pass on to the next device too unless a
    // pattern uses them; everything else (CC, pitch bend, aftertouch...) always passes on.
    output.clear();
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            midiChannel = message.getChannel();
            engine.noteOn (message.getNoteNumber());
            if (playedFifo.getFreeSpace() > 0)
                playedFifo.write (1).forEach ([&] (int index) { playedNotes[static_cast<size_t> (index)] = static_cast<std::uint8_t> (message.getNoteNumber()); });
            if (! consumeNotes)
                output.addEvent (message, metadata.samplePosition);
        }
        else if (message.isNoteOff())
        {
            engine.noteOff (message.getNoteNumber());
            if (! consumeNotes)
                output.addEvent (message, metadata.samplePosition);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            engine.releaseAllHeld();
            output.addEvent (message, metadata.samplePosition);
        }
        else
        {
            output.addEvent (message, metadata.samplePosition);
        }
    }

    std::array<std::uint64_t, 2> mask {};
    for (int i = 0; i < engine.numInputNotes(); ++i)
    {
        const int n = engine.inputNotes()[i];
        mask[static_cast<size_t> (n / 64)] |= std::uint64_t { 1 } << (n % 64);
    }
    inputMask[0].store (mask[0], std::memory_order_relaxed);
    inputMask[1].store (mask[1], std::memory_order_relaxed);

    // Timing: follow the host's song position while it plays, otherwise run our own clock
    // at the host tempo so it also works for live playing with the transport stopped.
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
        engine.stopAll (0.0, events);
        addEvents (0.0, samplesPerBeat, numSamples, output);
    }

    // Follow the song while the transport runs. When it is stopped, play only while notes are
    // held (or latched) for a MIDI In pattern, on our own clock from the first key press.
    if (isPlaying && hostPpq.hasValue())
    {
        restartFreeRun = true;
        engine.process (*hostPpq, *hostPpq + blockBeats, events);
        addEvents (*hostPpq, samplesPerBeat, numSamples, output);
        playPosition.store (*hostPpq, std::memory_order_relaxed);
    }
    else if (consumeNotes && engine.hasInput())
    {
        if (restartFreeRun)
        {
            freeRunPpq = 0.0;
            restartFreeRun = false;
        }
        engine.process (freeRunPpq, freeRunPpq + blockBeats, events);
        addEvents (freeRunPpq, samplesPerBeat, numSamples, output);
        playPosition.store (freeRunPpq, std::memory_order_relaxed);
        freeRunPpq += blockBeats;
    }
    else
    {
        if (! restartFreeRun)
        {
            // Keys released while free-running: end the notes now.
            engine.stopAll (0.0, events);
            addEvents (0.0, samplesPerBeat, numSamples, output);
            restartFreeRun = true;
        }
        playPosition.store (-1.0, std::memory_order_relaxed);
    }
    wasPlaying = isPlaying;

    midi.swapWith (output);
}

juce::AudioProcessorEditor* PatternArpProcessor::createEditor()
{
    return new PatternArpEditor (*this);
}

void PatternArpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state (stateType);
    state.setProperty ("version", stateVersion, nullptr);
    state.setProperty ("latch", latch->get(), nullptr);
    state.appendChild (toValueTree (getArrangement()), nullptr);

    // Every slot's settings, including slots with no pattern yet, so the host gets back
    // exactly the parameter values it saved.
    juce::StringArray slotValues;
    for (const int v : readParameters())
        slotValues.add (juce::String (v));
    state.setProperty ("slots", slotValues.joinIntoString (","), nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PatternArpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateType))
        return;

    const auto state = juce::ValueTree::fromXml (*xml);
    *latch = static_cast<bool> (state.getProperty ("latch", false));

    const int version = state.getProperty ("version", 0);
    const bool before64ths = version < 5;

    // Older saves (before patterns existed) have no arrangement; keep the default one.
    Arrangement loaded;
    if (fromValueTree (state.getChildWithName ("Arrangement"), loaded))
    {
        if (before64ths)
        {
            loaded.loopLength *= 2;
            for (auto& p : loaded.patterns)
                p.timing = static_cast<Timing> (static_cast<int> (p.timing) + 1);
        }
        setArrangement (loaded);
    }

    // The saved parameter values, which can differ from the arrangement for slots with no
    // pattern. Saves from before the lane lengths and handles were parameters have only the
    // first six of each slot; the rest come from the arrangement just loaded.
    const auto slotValues = juce::StringArray::fromTokens (state.getProperty ("slots").toString(), ",", {});
    const bool current = slotValues.size() == static_cast<int> (ParameterSnapshot {}.size());
    const bool old = slotValues.size() == maxPatterns * oldParametersPerPattern;
    if (current || old)
    {
        auto values = readParameters();
        const int stride = current ? parametersPerPattern : oldParametersPerPattern;
        for (int slot = 0; slot < maxPatterns; ++slot)
            for (int i = 0; i < stride; ++i)
                values[static_cast<size_t> (slot * parametersPerPattern + i)] = slotValues[slot * stride + i].getIntValue();
        if (current)
            for (size_t i = globalsAt; i < values.size(); ++i)
                values[i] = slotValues[static_cast<int> (i)].getIntValue();
        if (before64ths)
            for (size_t i = 0; i < globalsAt; i += parametersPerPattern)
                ++values[i]; // timing
        setParameters (values);
    }

    arrangementReplaced.sendChangeMessage();

    // Tell the host every value may have changed, so it refreshes its controls. (The CLAP
    // wrapper turns a "program changed" notice into a parameter value rescan.)
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

} // namespace patternarp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new patternarp::PatternArpProcessor();
}
