#include "NoteKeyboard.h"

#include "Theme.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace patternarp
{

namespace
{
constexpr int learnHeight = 26;
constexpr int learnGap = 4;      // between the tabs and the Learn button
constexpr float dotSize = 8.0f; // the Learn button's record dot
constexpr int dotGap = 6;
const char* const sourceNames[] = { "Piano Roll", "MIDI Thru" };
constexpr int totalWhiteKeys = (NoteKeyboard::highestNote - NoteKeyboard::lowestNote) / 12 * 7 + 1;

bool isBlack (int note)
{
    const int pc = note % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

// Position of a key in white keys from the lowest C. Black keys sit on the line between two whites.
float whitePosition (int note)
{
    static const float offsets[] = { 0.0f, 1.0f, 1.0f, 2.0f, 2.0f, 3.0f, 4.0f, 4.0f, 5.0f, 5.0f, 6.0f, 6.0f };
    const int rel = note - NoteKeyboard::lowestNote;
    return static_cast<float> (rel / 12 * 7) + offsets[rel % 12];
}
} // namespace

NoteKeyboard::NoteKeyboard()
{
    setRepaintsOnMouseActivity (true);
}

void NoteKeyboard::setNotes (const std::bitset<128>& newEnabled, NoteSource newSource)
{
    if (newEnabled != enabled || newSource != source)
    {
        enabled = newEnabled;
        source = newSource;
        repaint();
    }

    // Learning adds to the Piano Roll notes, so it stops if the source changes (automation).
    if (source != NoteSource::select)
        setLearning (false);
}

void NoteKeyboard::setLearning (bool shouldLearn)
{
    if (shouldLearn == learning)
        return;

    learning = shouldLearn;
    if (learning)
        startTimerHz (30);
    else
        stopTimer();
    repaint();
    if (onLearnChange)
        onLearnChange (learning);
}

void NoteKeyboard::setInputNotes (const std::bitset<128>& notes)
{
    if (notes != input)
    {
        input = notes;
        if (source == NoteSource::midiIn)
            repaint();
    }
}

int NoteKeyboard::tabsWidth() const
{
    // The Learn button stops `spacing` short of the tabs' right line.
    const auto plain = theme::font (14.0f), bold = theme::font (14.0f, true);
    const int learnText = juce::jmax (theme::textWidth (plain, "MIDI Learn"), theme::textWidth (bold, "Play Notes"));
    const int learn = theme::padding * 2 + static_cast<int> (dotSize) + dotGap + learnText + theme::spacing;
    const int tabs = theme::padding * 2 + juce::jmax (theme::textWidth (bold, sourceNames[0]), theme::textWidth (bold, sourceNames[1]));
    return juce::jmax (learn, tabs);
}

juce::Rectangle<int> NoteKeyboard::sourceTab (NoteSource s) const
{
    const auto area = getLocalBounds().withWidth (tabsWidth()).withTrimmedBottom (learnHeight + learnGap);
    const int split = area.getCentreY();
    return s == NoteSource::select ? area.withBottom (split + 1) : area.withTop (split);
}

int NoteKeyboard::sourceTabAt (juce::Point<int> p) const
{
    for (const auto s : { NoteSource::select, NoteSource::midiIn })
        if (sourceTab (s).contains (p))
            return static_cast<int> (s);
    return -1;
}

juce::Rectangle<int> NoteKeyboard::learnArea() const
{
    return getLocalBounds().withWidth (tabsWidth() - theme::spacing).removeFromBottom (learnHeight);
}

juce::Rectangle<int> NoteKeyboard::keysArea() const
{
    // White space between the tabs' right line and the keys, for the open tab to run into.
    return getLocalBounds().withTrimmedLeft (tabsWidth() + theme::spacing);
}

float NoteKeyboard::whiteKeyWidth() const
{
    return static_cast<float> (keysArea().getWidth()) / static_cast<float> (totalWhiteKeys);
}

juce::Rectangle<float> NoteKeyboard::keyBounds (int note) const
{
    const auto area = keysArea().toFloat();
    const float w = whiteKeyWidth();
    const float x = area.getX() + whitePosition (note) * w;
    if (isBlack (note))
        return { x - w * 0.3f, area.getY(), w * 0.6f, area.getHeight() * 0.6f };
    return { x, area.getY(), w, area.getHeight() };
}

int NoteKeyboard::noteAt (juce::Point<float> p) const
{
    for (int pass = 0; pass < 2; ++pass) // black keys are on top, so test them first
        for (int n = lowestNote; n <= highestNote; ++n)
            if (isBlack (n) == (pass == 0) && keyBounds (n).contains (p))
                return n;
    return -1;
}

void NoteKeyboard::paint (juce::Graphics& g)
{
    // Keys.
    const auto area = keysArea();
    g.saveState();
    g.reduceClipRegion (area);
    g.setColour (theme::panel);
    g.fillRect (area);

    const bool midiIn = source == NoteSource::midiIn;
    const auto& lit = midiIn ? input : enabled;

    for (int pass = 0; pass < 2; ++pass)
    {
        for (int n = lowestNote; n <= highestNote; ++n)
        {
            if (isBlack (n) != (pass == 1))
                continue;

            const auto r = keyBounds (n);
            const bool on = lit.test (static_cast<size_t> (n));
            juce::Colour c = pass == 1 ? theme::ink : theme::panel;
            if (on)
                c = pass == 1 ? colour.darker (0.25f) : colour;
            g.setColour (c);
            g.fillRect (r);
            g.setColour (theme::ink);
            g.drawRect (r, 1.0f);

            if (n % 12 == 0)
            {
                g.setColour (on ? theme::panel : theme::inkMuted); // readable on a lit key
                g.setFont (theme::font (11.0f));
                g.drawText (juce::MidiMessage::getMidiNoteName (n, false, true, theme::middleCOctave), r.withTrimmedTop (r.getHeight() - 16.0f),
                            juce::Justification::centred);
            }
        }
    }
    g.restoreState();

    if (midiIn)
    {
        // On a pill, so the keys underneath do not get in the way.
        const auto f = theme::font (12.0f);
        const juce::String text = "Plays the notes you hold";
        const auto w = static_cast<float> (juce::GlyphArrangement::getStringWidthInt (f, text)) + 10.0f;
        const auto pill = juce::Rectangle<float> (static_cast<float> (area.getRight()) - w - 4.0f, static_cast<float> (area.getY()) + 4.0f, w, 16.0f);
        g.setColour (theme::panel.withAlpha (0.9f));
        g.fillRoundedRectangle (pill, theme::cornerRadius);
        g.setColour (theme::ink);
        g.setFont (f);
        g.drawText (text, pill, juce::Justification::centred);
    }

    g.setColour (theme::ink);
    g.drawRect (area, 1);

    // Piano Roll and MIDI Thru tabs, coloured like the lane tabs: the open one is white and
    // opens through the line down their right side (as lane tabs do through the graph's top
    // line), the other grey. Closed lines are opaque so shared ones do not darken.
    const int tabsLine = tabsWidth() - 1;
    g.setColour (theme::ink);
    g.fillRect (tabsLine, 0, 1, getHeight());
    const auto closedLine = theme::panel.overlaidWith (theme::ink.withAlpha (0.3f));
    for (const bool open : { false, true })
        for (const auto s : { NoteSource::select, NoteSource::midiIn })
        {
            if ((s == source) != open)
                continue;

            const auto tab = sourceTab (s).toFloat();
            const int i = static_cast<int> (s);
            // Its right side is that line: kept for the closed tab, opened for the open one.
            g.setColour (open ? theme::ink : closedLine);
            g.fillRect (tab.withTrimmedRight (1.0f));
            g.setColour (open ? theme::panel : (i == hoveredTab ? theme::tint (colour) : theme::background));
            g.fillRect (open ? tab.withTrimmedLeft (1.0f).reduced (0.0f, 1.0f) : tab.reduced (1.0f));
            g.setColour (open ? theme::ink : theme::ink.withAlpha (0.7f));
            g.setFont (theme::font (14.0f, open));
            g.drawText (sourceNames[i], tab.withTrimmedLeft (static_cast<float> (theme::padding)), juce::Justification::centredLeft);
        }

    // MIDI Learn, with a record dot: pulses red while armed, like a record-armed track, and
    // offers "Done" on hover.
    {
        const auto r = learnArea().toFloat();
        const bool hover = isMouseOver() && learnArea().contains (getMouseXYRelative());
        juce::String text = "MIDI Learn";
        if (learning)
        {
            const double phase = juce::Time::getMillisecondCounterHiRes() / 1000.0 * juce::MathConstants<double>::twoPi;
            const auto pulse = static_cast<float> (0.5 + 0.5 * std::cos (phase));
            g.setColour (theme::danger.withAlpha (0.45f + 0.55f * pulse));
            g.fillRoundedRectangle (r, theme::cornerRadius);
            text = hover ? "Done" : "Play Notes";
        }
        else
        {
            g.setColour (hover ? theme::tint (colour) : theme::panel);
            g.fillRoundedRectangle (r, theme::cornerRadius);
            g.setColour (theme::ink);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::cornerRadius, 1.0f);
        }

        const auto f = theme::font (14.0f, learning);
        const float textWidth = static_cast<float> (theme::textWidth (f, text));
        auto content = r.withTrimmedLeft (static_cast<float> (theme::padding)).withWidth (dotSize + static_cast<float> (dotGap) + textWidth);
        g.setColour (learning ? juce::Colours::white : theme::danger);
        g.fillEllipse (content.removeFromLeft (dotSize).withSizeKeepingCentre (dotSize, dotSize));
        content.removeFromLeft (static_cast<float> (dotGap));
        g.setColour (learning ? juce::Colours::white : theme::ink);
        g.setFont (f);
        g.drawText (text, content, juce::Justification::centredLeft);
    }
}

void NoteKeyboard::mouseMove (const juce::MouseEvent& e)
{
    repaint (learnArea()); // the Learn button's hover look
    if (const int tab = sourceTabAt (e.getPosition()); tab != hoveredTab)
    {
        hoveredTab = tab;
        repaint (sourceTab (NoteSource::select).getUnion (sourceTab (NoteSource::midiIn)));
    }
}

void NoteKeyboard::mouseExit (const juce::MouseEvent&)
{
    hoveredTab = -1;
}

void NoteKeyboard::mouseDown (const juce::MouseEvent& e)
{
    paintingKeys = false;

    if (learnArea().contains (e.getPosition()))
    {
        // Learning adds to the Piano Roll notes, so arming it switches to Piano Roll.
        if (! learning && source != NoteSource::select)
        {
            source = NoteSource::select;
            if (onSourceChange)
                onSourceChange (source);
        }
        setLearning (! learning);
        return;
    }

    if (const int tab = sourceTabAt (e.getPosition()); tab >= 0)
    {
        const auto newSource = static_cast<NoteSource> (tab);
        if (newSource != source)
        {
            source = newSource;
            repaint();
            if (onSourceChange)
                onSourceChange (source);
        }
        if (source != NoteSource::select)
            setLearning (false);
        return;
    }

    if (source != NoteSource::select)
        return;

    const int n = noteAt (e.position);
    if (n < 0)
        return;

    paintingKeys = true;
    paintValue = ! enabled.test (static_cast<size_t> (n));
    lastPainted = -1;
    mouseDrag (e);
}

void NoteKeyboard::mouseDrag (const juce::MouseEvent& e)
{
    if (! paintingKeys)
        return;

    const int n = noteAt (e.position);
    if (n < 0 || n == lastPainted)
        return;

    lastPainted = n;
    if (enabled.test (static_cast<size_t> (n)) != paintValue)
    {
        enabled.set (static_cast<size_t> (n), paintValue);
        repaint();
        if (onNotesChange)
            onNotesChange (enabled);
    }
}

void NoteKeyboard::mouseUp (const juce::MouseEvent&)
{
    paintingKeys = false;
}

} // namespace patternarp
