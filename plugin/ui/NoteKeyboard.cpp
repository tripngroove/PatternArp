#include "NoteKeyboard.h"

#include "Theme.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace patternarp
{

namespace
{
constexpr int switchWidth = 78;
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

juce::Rectangle<int> NoteKeyboard::switchArea() const
{
    return getLocalBounds().removeFromLeft (switchWidth).withTrimmedRight (8);
}

juce::Rectangle<int> NoteKeyboard::keysArea() const
{
    return getLocalBounds().withTrimmedLeft (switchWidth);
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
    // Select | MIDI In switch.
    auto sw = switchArea().toFloat();
    const auto half = sw.getHeight() / 2.0f;
    const juce::Rectangle<float> buttons[] = { sw.withHeight (half), sw.withTrimmedTop (half) };
    const char* labels[] = { "Select", "MIDI In" };
    for (int i = 0; i < 2; ++i)
    {
        const bool on = static_cast<int> (source) == i;
        g.setColour (on ? colour : theme::panel);
        g.fillRect (buttons[i]);
        g.setColour (on ? juce::Colours::white : theme::ink);
        g.setFont (theme::font (14.0f, on));
        g.drawText (labels[i], buttons[i], juce::Justification::centred);
    }
    g.setColour (theme::ink);
    g.drawRect (sw, 1.0f);
    g.drawHorizontalLine (juce::roundToInt (sw.getCentreY()), sw.getX(), sw.getRight());

    // Keys.
    const auto area = keysArea();
    g.saveState();
    g.reduceClipRegion (area);
    g.setColour (theme::panel);
    g.fillRect (area);

    const bool midiIn = source == NoteSource::midiIn;
    const auto& lit = midiIn ? input : enabled;
    const auto litColour = midiIn ? colour : theme::fill;

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
                c = pass == 1 ? litColour.darker (0.25f) : litColour;
            g.setColour (c);
            g.fillRect (r);
            g.setColour (theme::ink);
            g.drawRect (r, 1.0f);

            if (n % 12 == 0)
            {
                g.setColour (theme::inkMuted);
                g.setFont (theme::font (11.0f));
                g.drawText (juce::MidiMessage::getMidiNoteName (n, false, true, theme::middleCOctave), r.withTrimmedTop (r.getHeight() - 16.0f),
                            juce::Justification::centred);
            }
        }
    }
    g.restoreState();

    if (midiIn)
    {
        g.setColour (theme::inkMuted);
        g.setFont (theme::font (12.0f));
        g.drawText ("Plays the notes you hold", area.reduced (6, 2).removeFromTop (14), juce::Justification::topRight);
    }

    g.setColour (theme::ink);
    g.drawRect (area, 1);
}

void NoteKeyboard::mouseDown (const juce::MouseEvent& e)
{
    paintingKeys = false;

    if (switchArea().contains (e.getPosition()))
    {
        const auto newSource = e.position.y < static_cast<float> (switchArea().getCentreY()) ? NoteSource::select : NoteSource::midiIn;
        if (newSource != source)
        {
            source = newSource;
            repaint();
            if (onSourceChange)
                onSourceChange (source);
        }
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
