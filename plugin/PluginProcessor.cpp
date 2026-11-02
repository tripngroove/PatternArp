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
// 6: five dotted timings joined (a build tried on 2026-10-31, before the full set).
// 7: every note got its triplet and dotted versions, and loop lengths went from 48 to 96
//    ticks a beat (see upgradeTiming).
// 8: variations. The arrangement saved as before is the current variation; the other
//    filled ones follow it.
constexpr int stateVersion = 8;

// A timing index from a save of the given version, as it is now.
int upgradeTiming (int timing, int version)
{
    // Where each timing of the version 5 and 6 lists is now.
    static constexpr std::array<int, 10> fromVersion5 { 1, 4, 5, 7, 8, 10, 11, 13, 16, 19 };
    static constexpr std::array<int, 15> fromVersion6 { 1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 18, 19 };

    if (version < 5)
        ++timing; // 1/64 went in first
    if (version < 6)
        return fromVersion5[static_cast<size_t> (std::clamp (timing, 0, 9))];
    if (version < 7)
        return fromVersion6[static_cast<size_t> (std::clamp (timing, 0, 14))];
    return timing;
}

// A loop length from a save of the given version, in today's ticks.
int upgradeLoopLength (int ticks, int version)
{
    if (version < 5)
        ticks *= 2;
    if (version < 7)
        ticks *= 2;
    return ticks;
}

const juce::ParameterID latchId { "latch", 1 };

// The Loop Length parameter counts sixteenths, up to 16 bars.
constexpr int ticksPerSixteenth = ticksPerBeat / 4;
constexpr int maxLoopSixteenths = static_cast<int> (PatternArpProcessor::maxLoopBeats * 4.0);

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

    // Which variation plays: one stepped parameter to automate.
    juce::StringArray variationNames;
    for (int i = 1; i <= maxVariations; ++i)
        variationNames.add (juce::String (i));
    auto variationParameter = std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "variation", 1 }, "Variation", variationNames, 0);
    variation = variationParameter.get();
    addParameter (variationParameter.release());

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

        // In the order the tabs show them.
        for (const auto lane : laneOrder)
        {
            const juce::String name = laneName (lane);
            pp.laneLength[static_cast<size_t> (lane)] = add (std::make_unique<juce::AudioParameterInt> (
                id (name.toLowerCase() + "length"), label (name + " Length"), 0, maxSteps, 0,
                juce::AudioParameterIntAttributes()
                    .withStringFromValueFunction ([] (int v, int) { return v == 0 ? juce::String ("Full") : juce::String (v); })
                    .withValueFromStringFunction ([] (const juce::String& text) {
                        return text.trim().equalsIgnoreCase ("full") ? 0 : text.getIntValue();
                    })));
        }

        pp.ccNumber = add (std::make_unique<juce::AudioParameterInt> (
            id ("ccnumber"), label ("CC Number"), -1, 127, -1,
            juce::AudioParameterIntAttributes()
                .withStringFromValueFunction ([] (int v, int) { return v < 0 ? juce::String ("Off") : juce::String (v); })
                .withValueFromStringFunction ([] (const juce::String& text) {
                    return text.trim().equalsIgnoreCase ("off") ? -1 : text.getIntValue();
                })));

        addParameterGroup (std::move (group));
    }

    pushToParameters (arrangement());
    *pendingVariations = variations;
    events.reserve (1024);
    startTimerHz (30);
}

PatternArpProcessor::~PatternArpProcessor()
{
    stopTimer();
}

std::array<juce::RangedAudioParameter*, 6 + numLanes + 1> PatternArpProcessor::PatternParameters::all() const
{
    return { timing,        length,        direction,     noteSource,    chanceMode,    play,     laneLength[0], laneLength[1],
             laneLength[2], laneLength[3], laneLength[4], laneLength[5], laneLength[6], ccNumber, laneLength[7] };
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
        for (int lane = 0; lane < static_cast<int> (Lane::repeat); ++lane)
            p.laneLength[static_cast<size_t> (lane)] = v[6 + lane];
        p.ccNumber = v[6 + static_cast<int> (Lane::repeat)];
        p.laneLength[static_cast<size_t> (Lane::repeat)] = v[7 + static_cast<int> (Lane::repeat)];
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
void setPlainValue (juce::RangedAudioParameter& parameter, int value, bool gesture = true)
{
    const auto normalised = parameter.convertTo0to1 (static_cast<float> (value));
    if (std::abs (parameter.getValue() - normalised) < 1.0e-6f)
        return;
    if (gesture)
        parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (normalised);
    if (gesture)
        parameter.endChangeGesture();
}
} // namespace

void PatternArpProcessor::pushToParameters (const Arrangement& a, bool gesture)
{
    for (int i = 0; i < a.numPatterns; ++i)
    {
        const auto& p = a.patterns[static_cast<size_t> (i)];
        auto& pp = patternParameters[static_cast<size_t> (i)];
        setPlainValue (*pp.timing, static_cast<int> (p.timing), gesture);
        setPlainValue (*pp.length, p.length, gesture);
        setPlainValue (*pp.direction, static_cast<int> (p.direction), gesture);
        setPlainValue (*pp.noteSource, static_cast<int> (p.noteSource), gesture);
        setPlainValue (*pp.chanceMode, static_cast<int> (p.chanceMode), gesture);
        setPlainValue (*pp.play, p.playing ? 1 : 0, gesture);
        for (int lane = 0; lane < numLanes; ++lane)
            setPlainValue (*pp.laneLength[static_cast<size_t> (lane)], p.laneLength[static_cast<size_t> (lane)], gesture);
        setPlainValue (*pp.ccNumber, p.ccNumber, gesture);
    }
    setPlainValue (*loopLength, loopSixteenths (a.loopLength), gesture);
    setPlainValue (*rotationLength, a.orderLength, gesture);
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
    auto a = arrangement();
    applyParameters (readParameters(), a);
    return a;
}

void PatternArpProcessor::setArrangement (const Arrangement& newArrangement)
{
    arrangement() = newArrangement;
    pushToParameters (arrangement());
    sendVariations();

    // Lets the host know the project has unsaved changes.
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void PatternArpProcessor::sendVariations()
{
    const juce::SpinLock::ScopedLockType lock (pendingLock);
    *pendingVariations = variations;
    hasPendingVariations = true;
}

// --- Variations -------------------------------------------------------------------------
//
// The message thread owns the variations; the audio thread plays copies of them. The host
// parameters (the pattern settings, loop and Rotation lengths) always belong to the message
// thread's current variation. Automation of the Variation parameter switches on the audio
// thread straight away; until the message thread catches up (switchVariation) and moves the
// parameters over, the audio thread plays the new variation as it was saved, without them.

void PatternArpProcessor::switchVariation (int index)
{
    if (index == variations.active || ! variations.isFilled (index))
        return;

    arrangement() = getArrangement(); // keep the one being left as it is, automation and all
    variations.active = index;
    pushToParameters (arrangement(), false); // not a person moving them: nothing to record
    sendVariations();
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void PatternArpProcessor::timerCallback()
{
    if (const int playing = playingVariation.load(); playing != lastSeenPlaying)
    {
        lastSeenPlaying = playing;
        switchVariation (playing);
    }
}

PatternArpProcessor::VariationStatus PatternArpProcessor::getVariationStatus() const
{
    VariationStatus status;
    status.filled = variations.filled;
    status.active = variations.active;
    status.waiting = waitingVariation.load();
    status.requested = variation->getIndex();
    return status;
}

void PatternArpProcessor::selectVariation (int index, SwitchTiming timing)
{
    if (index < 0 || index >= maxVariations)
        return;

    if (! variations.isFilled (index))
    {
        // An empty variation starts as a copy of the current one.
        arrangement() = getArrangement();
        variations.copy (variations.active, index);
        sendVariations();
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    }

    if (timing != SwitchTiming::immediately && transportPlaying.load())
    {
        // The audio thread times it, and moves the Variation parameter when it happens.
        clickRequestTiming.store (static_cast<int> (timing));
        clickRequest.store (index);
        return;
    }

    // The parameter first: the audio thread switches when it sees it.
    setPlainValue (*variation, index);
    clickRequest.store (index); // cancels a click still waiting
    waitingVariation.store (-1);
    switchVariation (index);
}

void PatternArpProcessor::copyVariation (int from, int to)
{
    if (from == to)
        return;
    if (from == variations.active)
        arrangement() = getArrangement();
    if (! variations.copy (from, to))
        return;
    if (to == variations.active)
        pushToParameters (arrangement());
    sendVariations();
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void PatternArpProcessor::copyVariationToEmpty (int from)
{
    if (from == variations.active)
        arrangement() = getArrangement();
    if (variations.copyToEmpty (from) == 0)
        return;
    sendVariations();
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void PatternArpProcessor::clearVariation (int index)
{
    if (index == playingVariation.load() || ! variations.clear (index))
        return;
    if (waitingVariation.load() == index)
    {
        clickRequest.store (variations.active);
        waitingVariation.store (-1);
    }
    sendVariations();
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void PatternArpProcessor::setOtherVariationsStepDefaults (int velocity, int gate)
{
    for (int i = 0; i < maxVariations; ++i)
        if (i != variations.active && variations.isFilled (i))
            variations.arrangements[static_cast<size_t> (i)].setStepDefaults (velocity, gate);
    sendVariations();
}

void PatternArpProcessor::playAudioVariation (int index, const ParameterSnapshot& values)
{
    audioActive = index;
    playingVariation.store (index);
    appliedParameters = values;
    audioPlaying = audioVariations->arrangements[static_cast<size_t> (index)];
    if (audioVariations->active == index) // the parameters belong to it
        applyParameters (values, audioPlaying);
    engine.setArrangement (audioPlaying);
    consumeNotes = audioPlaying.usesMidiIn();
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
        // Events at the same offset keep their order, so a step's CC lands just before its note.
        const auto message = e.isController() ? juce::MidiMessage::controllerEvent (midiChannel, e.controller, e.velocity)
                              : e.isNoteOn()  ? juce::MidiMessage::noteOn (midiChannel, e.note, static_cast<juce::uint8> (e.velocity))
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

    // Pick up edits from the editor if we can do so without waiting.
    bool changed = false;
    if (hasPendingVariations.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (pendingLock);
        if (lock.isLocked())
        {
            std::swap (pendingVariations, audioVariations);
            hasPendingVariations = false;
            changed = true;
        }
    }

    // Timing: follow the host's song position while it plays, otherwise run our own clock
    // at the host tempo so it also works for live playing with the transport stopped.
    double bpm = 120.0;
    bool isPlaying = false;
    juce::Optional<double> hostPpq;
    double barStart = 0.0, beatsPerBar = 4.0, beatLength = 1.0;
    if (auto* ph = getPlayHead())
    {
        if (const auto position = ph->getPosition())
        {
            bpm = position->getBpm().orFallback (120.0);
            isPlaying = position->getIsPlaying();
            hostPpq = position->getPpqPosition();
            barStart = position->getPpqPositionOfLastBarStart().orFallback (0.0);
            if (const auto signature = position->getTimeSignature(); signature.hasValue() && signature->numerator > 0 && signature->denominator > 0)
            {
                beatLength = 4.0 / signature->denominator;
                beatsPerBar = signature->numerator * beatLength;
            }
        }
    }
    const bool following = isPlaying && hostPpq.hasValue();
    transportPlaying.store (following);

    // Any automation, then the variation to play. The Variation parameter is read last: the
    // message thread moves it before the others when it switches, so if any of them already
    // belong to the new variation, this sees the switch too.
    const auto values = readParameters();
    const int requested = variation->getIndex();

    if (const int click = clickRequest.exchange (-1); click >= 0)
    {
        clickWaiting = click == audioActive ? -1 : click; // clicking the playing one cancels
        clickTiming = static_cast<SwitchTiming> (clickRequestTiming.load());
    }

    // Automation switches straight away (an empty variation keeps this one playing), and
    // overrides a click still waiting.
    const int target = audioVariations->resolve (requested, audioActive);
    if (target != audioActive)
        clickWaiting = -1;
    if (target != audioActive || changed || values != appliedParameters)
        playAudioVariation (target, values);

    // A click takes over at its bar or beat while the song plays, now otherwise. The
    // Variation parameter follows, so a host writing automation records the switch where
    // it was heard.
    const auto takeClick = [this, &values] {
        const int index = clickWaiting;
        clickWaiting = -1;
        variation->beginChangeGesture();
        variation->setValueNotifyingHost (variation->convertTo0to1 (static_cast<float> (index)));
        variation->endChangeGesture();
        playAudioVariation (index, values);
    };
    // A clicked variation that was empty is copied into first; until that copy arrives here
    // the click keeps waiting.
    const bool clickReady = clickWaiting >= 0 && audioVariations->isFilled (clickWaiting);
    if (clickReady && ! following)
        takeClick();

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
    if (following)
    {
        restartFreeRun = true;
        const double end = *hostPpq + blockBeats;
        if (const double at = clickReady ? switchPosition (*hostPpq, clickTiming, barStart, beatsPerBar, beatLength) : end; at < end)
        {
            // Old notes up to the line, the new variation from it.
            if (at > *hostPpq)
                engine.process (*hostPpq, at, events);
            takeClick();
            engine.process (at, end, events);
        }
        else
        {
            engine.process (*hostPpq, end, events);
        }
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
    waitingVariation.store (clickWaiting);

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

    // The current variation is the arrangement above; the other filled ones follow.
    state.setProperty ("variation", variations.active, nullptr);
    juce::ValueTree others ("Variations");
    for (int i = 0; i < maxVariations; ++i)
        if (i != variations.active && variations.isFilled (i))
        {
            juce::ValueTree saved ("Variation");
            saved.setProperty ("index", i, nullptr);
            saved.appendChild (toValueTree (variations.arrangements[static_cast<size_t> (i)]), nullptr);
            others.appendChild (saved, nullptr);
        }
    if (others.getNumChildren() > 0)
        state.appendChild (others, nullptr);

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

    const auto upgrade = [version] (Arrangement& a) {
        a.loopLength = upgradeLoopLength (a.loopLength, version);
        for (int i = 0; i < a.numPatterns; ++i)
        {
            auto& p = a.patterns[static_cast<size_t> (i)];
            p.timing = static_cast<Timing> (upgradeTiming (static_cast<int> (p.timing), version));
        }
    };

    // Older saves (before patterns existed) have no arrangement; keep the default one. Saves
    // from before variations have only the one, which becomes the first.
    Arrangement loaded;
    if (fromValueTree (state.getChildWithName ("Arrangement"), loaded))
    {
        upgrade (loaded);

        Variations restored;
        restored.active = juce::jlimit (0, maxVariations - 1, static_cast<int> (state.getProperty ("variation", 0)));
        restored.filled = {};
        restored.filled[static_cast<size_t> (restored.active)] = true;
        for (const auto& saved : state.getChildWithName ("Variations"))
        {
            const int i = saved.getProperty ("index", -1);
            Arrangement a;
            if (saved.hasType ("Variation") && i >= 0 && i < maxVariations && i != restored.active
                && fromValueTree (saved.getChildWithName ("Arrangement"), a))
            {
                upgrade (a);
                restored.arrangements[static_cast<size_t> (i)] = a;
                restored.filled[static_cast<size_t> (i)] = true;
            }
        }
        variations = restored;
        setPlainValue (*variation, restored.active);
        clickRequest.store (restored.active); // drops a click waiting from before
        waitingVariation.store (-1);
        setArrangement (loaded);
    }

    // The saved parameter values, which can differ from the arrangement for slots with no
    // pattern. Saves from earlier versions have fewer per slot (see oldParametersPerPattern);
    // the ones they lack come from the arrangement just loaded.
    const auto slotValues = juce::StringArray::fromTokens (state.getProperty ("slots").toString(), ",", {});
    int stride = 0;
    for (const int perSlot : { parametersPerPattern, preRepeatParametersPerPattern, preCcParametersPerPattern, oldParametersPerPattern })
        if (slotValues.size() == maxPatterns * perSlot + (perSlot == oldParametersPerPattern ? 0 : 2))
            stride = perSlot;
    const bool hasGlobals = stride != oldParametersPerPattern;
    if (stride > 0)
    {
        auto values = readParameters();
        for (int slot = 0; slot < maxPatterns; ++slot)
            for (int i = 0; i < stride; ++i)
                values[static_cast<size_t> (slot * parametersPerPattern + i)] = slotValues[slot * stride + i].getIntValue();
        if (hasGlobals)
            for (int i = 0; i < 2; ++i)
                values[static_cast<size_t> (globalsAt + i)] = slotValues[maxPatterns * stride + i].getIntValue();
        for (size_t i = 0; i < globalsAt; i += parametersPerPattern)
            values[i] = upgradeTiming (values[i], version); // each slot's timing
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
