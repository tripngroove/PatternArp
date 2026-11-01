#include "ArrangementState.h"

namespace patternarp
{

namespace
{
namespace ids
{
const juce::Identifier arrangement { "Arrangement" };
const juce::Identifier pattern { "Pattern" };
const juce::Identifier loopLength { "loopLength" };
const juce::Identifier order { "order" };
const juce::Identifier orderLength { "orderLength" }; // when turns past it are remembered
const juce::Identifier quiet { "quiet" };
const juce::Identifier timing { "timing" };
const juce::Identifier length { "length" };
const juce::Identifier storedSteps { "storedSteps" };
const juce::Identifier direction { "direction" };
const juce::Identifier chanceMode { "chanceMode" };
const juce::Identifier noteSource { "noteSource" };
const juce::Identifier defaultVelocity { "defaultVelocity" };
const juce::Identifier defaultGate { "defaultGate" };
const juce::Identifier ccNumber { "ccNumber" };
const juce::Identifier seed { "seed" };
const juce::Identifier playing { "playing" };
const juce::Identifier colour { "colour" };
const juce::Identifier letter { "letter" };
const juce::Identifier notes { "notes" };
const juce::Identifier steps { "steps" };
const juce::Identifier laneLengths { "laneLengths" };
const juce::Identifier noteLanes { "noteLanes" }; // lanes in Note mode, by number
const juce::Identifier parent { "parent" };       // a sub-pattern's parent, by position
} // namespace ids

juce::String enabledNotesToString (const std::bitset<128>& notes)
{
    juce::StringArray list;
    for (int n = 0; n < 128; ++n)
        if (notes.test (static_cast<size_t> (n)))
            list.add (juce::String (n));
    return list.joinIntoString (",");
}

std::bitset<128> enabledNotesFromString (const juce::String& text)
{
    std::bitset<128> notes;
    for (const auto& token : juce::StringArray::fromTokens (text, ",", {}))
    {
        const int n = token.getIntValue();
        if (token.isNotEmpty() && n >= 0 && n < 128)
            notes.set (static_cast<size_t> (n));
    }
    return notes;
}

// Steps as "note velocity gate octave ratchet chance cc repeat" groups separated by ';'
// (saves from before the CC or Repeat lanes stop short; those steps keep their defaults).
juce::String stepsToString (const Pattern& p)
{
    juce::StringArray list;
    for (const auto& s : p.steps)
        list.add (juce::String (s.note, 4) + " " + juce::String (s.velocity) + " " + juce::String (s.gate) + " "
                  + juce::String (s.octave) + " " + juce::String (s.ratchet) + " " + juce::String (s.chance) + " " + juce::String (s.cc) + " "
                  + juce::String (s.repeat));
    return list.joinIntoString (";");
}

void stepsFromString (const juce::String& text, Pattern& p)
{
    const auto groups = juce::StringArray::fromTokens (text, ";", {});
    for (int i = 0; i < juce::jmin (groups.size(), maxSteps); ++i)
    {
        const auto values = juce::StringArray::fromTokens (groups[i], " ", {});
        if (values.size() < 6)
            continue;

        auto& s = p.steps[static_cast<size_t> (i)];
        s.note = values[0].getFloatValue();
        s.velocity = values[1].getIntValue();
        s.gate = values[2].getIntValue();
        s.octave = values[3].getIntValue();
        s.ratchet = values[4].getIntValue();
        s.chance = values[5].getIntValue();
        if (values.size() > 6)
            s.cc = values[6].getIntValue();
        if (values.size() > 7)
            s.repeat = values[7].getIntValue();
    }
}
} // namespace

juce::ValueTree toValueTree (const Arrangement& a)
{
    juce::ValueTree tree (ids::arrangement);
    tree.setProperty (ids::loopLength, a.loopLength, nullptr);

    // Every turn, including ones hidden by shortening the lane.
    const int stored = a.numStoredTurns();
    juce::StringArray order;
    for (int i = 0; i < stored; ++i)
        order.add (juce::String (a.order[static_cast<size_t> (i)]));
    tree.setProperty (ids::order, order.joinIntoString (","), nullptr);
    if (a.orderLength < stored)
        tree.setProperty (ids::orderLength, a.orderLength, nullptr);

    // Which turns are quiet, as the step numbers.
    juce::StringArray quiet;
    for (int i = 0; i < stored; ++i)
        if (a.quiet[static_cast<size_t> (i)])
            quiet.add (juce::String (i));
    if (! quiet.isEmpty())
        tree.setProperty (ids::quiet, quiet.joinIntoString (","), nullptr);

    for (int i = 0; i < a.numPatterns; ++i)
    {
        const auto& p = a.patterns[static_cast<size_t> (i)];
        juce::ValueTree child (ids::pattern);
        child.setProperty (ids::timing, static_cast<int> (p.timing), nullptr);
        child.setProperty (ids::length, p.length, nullptr);
        if (p.storedSteps > p.length)
            child.setProperty (ids::storedSteps, p.storedSteps, nullptr);
        child.setProperty (ids::direction, static_cast<int> (p.direction), nullptr);
        child.setProperty (ids::chanceMode, static_cast<int> (p.chanceMode), nullptr);
        child.setProperty (ids::noteSource, static_cast<int> (p.noteSource), nullptr);
        child.setProperty (ids::defaultVelocity, p.defaultVelocity, nullptr);
        child.setProperty (ids::defaultGate, p.defaultGate, nullptr);
        child.setProperty (ids::ccNumber, p.ccNumber, nullptr);
        child.setProperty (ids::seed, static_cast<juce::int64> (p.seed), nullptr);
        child.setProperty (ids::playing, p.playing, nullptr);
        child.setProperty (ids::colour, p.colour, nullptr);
        child.setProperty (ids::letter, p.letter, nullptr);
        child.setProperty (ids::notes, enabledNotesToString (p.enabledNotes), nullptr);
        child.setProperty (ids::steps, stepsToString (p), nullptr);

        juce::StringArray lengths;
        for (const int l : p.laneLength)
            lengths.add (juce::String (l));
        child.setProperty (ids::laneLengths, lengths.joinIntoString (","), nullptr);

        juce::StringArray noteLanes;
        for (int l = 0; l < numLanes; ++l)
            if (movesPerNote (p, static_cast<Lane> (l)))
                noteLanes.add (juce::String (l));
        if (! noteLanes.isEmpty())
            child.setProperty (ids::noteLanes, noteLanes.joinIntoString (","), nullptr);
        if (p.isSub())
            child.setProperty (ids::parent, p.parent, nullptr);
        tree.appendChild (child, nullptr);
    }
    return tree;
}

bool fromValueTree (const juce::ValueTree& tree, Arrangement& a)
{
    if (! tree.hasType (ids::arrangement))
        return false;

    Arrangement loaded;
    loaded.numPatterns = 0;
    loaded.loopLength = juce::jmax (0, static_cast<int> (tree.getProperty (ids::loopLength, 0)));

    for (const auto& child : tree)
    {
        if (! child.hasType (ids::pattern) || loaded.numPatterns >= maxPatterns)
            continue;

        Pattern p = makeDefaultPattern();
        p.timing = static_cast<Timing> (static_cast<int> (child.getProperty (ids::timing, static_cast<int> (p.timing))));
        p.length = child.getProperty (ids::length, p.length);
        p.storedSteps = child.getProperty (ids::storedSteps, 0);
        p.direction = static_cast<Direction> (static_cast<int> (child.getProperty (ids::direction, 0)));
        p.chanceMode = static_cast<ChanceMode> (static_cast<int> (child.getProperty (ids::chanceMode, 0)));
        p.noteSource = static_cast<NoteSource> (static_cast<int> (child.getProperty (ids::noteSource, 0)));
        p.defaultVelocity = child.getProperty (ids::defaultVelocity, p.defaultVelocity);
        p.defaultGate = child.getProperty (ids::defaultGate, p.defaultGate);
        p.ccNumber = child.getProperty (ids::ccNumber, -1);
        p.seed = static_cast<std::uint32_t> (static_cast<juce::int64> (child.getProperty (ids::seed, 1)));
        p.playing = child.getProperty (ids::playing, true);
        p.colour = child.getProperty (ids::colour, loaded.numPatterns); // older saves: by position
        p.letter = child.getProperty (ids::letter, loaded.numPatterns);
        p.enabledNotes = enabledNotesFromString (child.getProperty (ids::notes).toString());
        stepsFromString (child.getProperty (ids::steps).toString(), p);

        const auto lengths = juce::StringArray::fromTokens (child.getProperty (ids::laneLengths).toString(), ",", {});
        for (int l = 0; l < juce::jmin (lengths.size(), numLanes); ++l)
            p.laneLength[static_cast<size_t> (l)] = lengths[l].getIntValue();

        // Saves from before Note mode have none: every lane moves on per step, as it did.
        for (const auto& lane : juce::StringArray::fromTokens (child.getProperty (ids::noteLanes).toString(), ",", {}))
            if (const int l = lane.getIntValue(); lane.isNotEmpty() && l >= 0 && l < numLanes)
                p.laneMode[static_cast<size_t> (l)] = LaneMode::note;

        // Saves from before sub-patterns have none: every pattern takes turns.
        p.parent = child.getProperty (ids::parent, -1);
        sanitise (p);

        loaded.patterns[static_cast<size_t> (loaded.numPatterns++)] = p;
    }

    // Saves from before the Order lane take every pattern in turn.
    const auto order = juce::StringArray::fromTokens (tree.getProperty (ids::order).toString(), ",", {});
    if (order.isEmpty())
    {
        loaded.alternate();
    }
    else
    {
        loaded.storedTurns = juce::jmin (order.size(), maxOrderSteps);
        loaded.orderLength = juce::jlimit (1, loaded.storedTurns, static_cast<int> (tree.getProperty (ids::orderLength, loaded.storedTurns)));
        for (int i = 0; i < loaded.storedTurns; ++i)
            loaded.order[static_cast<size_t> (i)] = order[i].getIntValue();
        for (const auto& step : juce::StringArray::fromTokens (tree.getProperty (ids::quiet).toString(), ",", {}))
            if (const int i = step.getIntValue(); i >= 0 && i < loaded.storedTurns)
                loaded.quiet[static_cast<size_t> (i)] = true;
    }
    sanitise (loaded);

    a = loaded;
    return true;
}

} // namespace patternarp
