#include "LaneGraph.h"

#include "Theme.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace patternarp
{

const char* laneName (Lane lane)
{
    switch (lane)
    {
        case Lane::notes:    return "Notes";
        case Lane::velocity: return "Velocity";
        case Lane::gate:     return "Gate";
        case Lane::octave:   return "Octave";
        case Lane::ratchet:  return "Ratchet";
        case Lane::chance:   return "Chance";
    }
    return "";
}

LaneGraph::LaneGraph()
{
    setRepaintsOnMouseActivity (true);
}

void LaneGraph::setPattern (const Pattern& newPattern)
{
    pattern = newPattern;
    relativeNotes = pattern.noteSource == NoteSource::midiIn;
    numEnabled = 0;
    for (int n = 0; n < 128; ++n)
        if (pattern.enabledNotes.test (static_cast<size_t> (n)))
            enabled[static_cast<size_t> (numEnabled++)] = n;
    repaint();
}

void LaneGraph::setLane (Lane newLane)
{
    lane = newLane;
    repaint();
}

juce::Rectangle<float> LaneGraph::graphArea() const
{
    return getLocalBounds().toFloat().withTrimmedRight (static_cast<float> (labelGutter)).reduced (1.0f);
}

int LaneGraph::stepAt (float x) const
{
    const auto area = graphArea();
    const auto step = static_cast<int> ((x - area.getX()) / area.getWidth() * static_cast<float> (pattern.length));
    return juce::jlimit (0, pattern.length - 1, step);
}

bool LaneGraph::isBipolar() const
{
    return lane == Lane::octave || lane == Lane::chance;
}

float LaneGraph::valueToY (const Step& s) const
{
    const auto area = graphArea();
    const float top = area.getY(), h = area.getHeight(), bottom = area.getBottom(), centre = area.getCentreY();

    switch (lane)
    {
        case Lane::notes:
        {
            if (relativeNotes)
                return bottom - s.note * h;
            if (numEnabled == 0)
                return bottom;
            const auto index = juce::roundToInt (s.note * static_cast<float> (numEnabled - 1));
            return bottom - (static_cast<float> (index) + 0.5f) / static_cast<float> (numEnabled) * h;
        }
        case Lane::velocity: return bottom - static_cast<float> (s.velocity) / 127.0f * h;
        case Lane::gate:     return bottom - static_cast<float> (s.gate) / 100.0f * h;
        case Lane::ratchet:  return bottom - static_cast<float> (s.ratchet) / 8.0f * h;
        case Lane::octave:   return centre - static_cast<float> (s.octave) / 3.5f * (h / 2.0f);
        case Lane::chance:
            if (s.chance > 0)
                return centre - static_cast<float> (s.chance) / 100.0f * (h / 2.0f);
            return centre + static_cast<float> (-s.chance - 1) / 7.0f * (h / 2.0f);
    }
    juce::ignoreUnused (top);
    return bottom;
}

void LaneGraph::setFromY (Step& s, float y) const
{
    const auto area = graphArea();
    const float h = area.getHeight(), bottom = area.getBottom(), centre = area.getCentreY();
    const float fromBottom = juce::jlimit (0.0f, 1.0f, (bottom - y) / h);

    switch (lane)
    {
        case Lane::notes:
            if (relativeNotes)
            {
                s.note = fromBottom;
            }
            else if (numEnabled > 1)
            {
                const auto index = juce::jlimit (0, numEnabled - 1, static_cast<int> (fromBottom * static_cast<float> (numEnabled)));
                s.note = static_cast<float> (index) / static_cast<float> (numEnabled - 1);
            }
            else
            {
                s.note = 0.0f;
            }
            break;

        case Lane::velocity: s.velocity = juce::jlimit (1, 127, juce::roundToInt (fromBottom * 127.0f)); break;
        case Lane::gate:     s.gate = juce::jlimit (1, 100, juce::roundToInt (fromBottom * 100.0f)); break;
        case Lane::ratchet:  s.ratchet = juce::jlimit (1, 8, static_cast<int> (fromBottom * 8.0f) + 1); break;

        case Lane::octave:
            s.octave = juce::jlimit (-3, 3, juce::roundToInt ((centre - y) / (h / 2.0f) * 3.5f));
            break;

        case Lane::chance:
            if (y <= centre)
                s.chance = juce::jlimit (1, 100, juce::roundToInt ((centre - y) / (h / 2.0f) * 100.0f));
            else
                s.chance = -juce::jlimit (2, 8, juce::roundToInt (1.0f + (y - centre) / (h / 2.0f) * 7.0f));
            break;
    }
}

juce::String LaneGraph::valueText (const Step& s) const
{
    switch (lane)
    {
        case Lane::notes:
            if (relativeNotes)
                return s.note < 0.01f ? "Low" : (s.note > 0.99f ? "High" : juce::String (juce::roundToInt (s.note * 100.0f)) + "%");
            if (numEnabled == 0)
                return {};
            return juce::MidiMessage::getMidiNoteName (enabled[static_cast<size_t> (juce::roundToInt (s.note * static_cast<float> (numEnabled - 1)))],
                                                       true, true, 4);
        case Lane::velocity: return juce::String (s.velocity);
        case Lane::gate:     return juce::String (s.gate) + "%";
        case Lane::octave:   return s.octave > 0 ? "+" + juce::String (s.octave) : juce::String (s.octave);
        case Lane::ratchet:  return "x" + juce::String (s.ratchet);
        case Lane::chance:   return s.chance > 0 ? juce::String (s.chance) + "%" : "1:" + juce::String (-s.chance);
    }
    return {};
}

void LaneGraph::drawNoteGrid (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const float dots[] = { 2.0f, 4.0f };

    if (relativeNotes)
    {
        // Positions between the lowest and highest held note, whatever is being played.
        g.setFont (theme::font (14.0f));
        for (int i = 0; i <= 4; ++i)
        {
            const float y = area.getBottom() - area.getHeight() * static_cast<float> (i) / 4.0f;
            g.setColour (theme::gridLine);
            g.drawDashedLine ({ area.getX(), y, area.getRight(), y }, dots, 2, 1.0f);
        }
        g.setColour (theme::ink);
        const auto gutter = juce::Rectangle<float> (area.getRight() + 6.0f, area.getY(), static_cast<float> (labelGutter) - 6.0f, area.getHeight());
        g.drawText ("High", gutter.withHeight (16.0f), juce::Justification::centredLeft);
        g.drawText ("Low", gutter.withY (area.getBottom() - 16.0f).withHeight (16.0f), juce::Justification::centredLeft);
        return;
    }

    if (numEnabled == 0)
    {
        g.setColour (theme::inkMuted);
        g.setFont (theme::font (14.0f));
        g.drawText ("Turn on some notes on the keyboard below", area, juce::Justification::centred);
        return;
    }

    // One dotted line per enabled note, labelled in the right-hand gutter. Labels thin out
    // when there are too many to fit.
    const float rowHeight = area.getHeight() / static_cast<float> (numEnabled);
    const int labelEvery = juce::jmax (1, static_cast<int> (std::ceil (14.0f / rowHeight)));

    g.setFont (theme::font (14.0f));
    for (int i = 0; i < numEnabled; ++i)
    {
        const float y = area.getBottom() - (static_cast<float> (i) + 0.5f) * rowHeight;
        g.setColour (theme::gridLine);
        g.drawDashedLine ({ area.getX(), y, area.getRight() + static_cast<float> (labelGutter) * 0.25f, y }, dots, 2, 1.0f);

        if (i % labelEvery == 0)
        {
            g.setColour (theme::ink);
            const auto name = juce::MidiMessage::getMidiNoteName (enabled[static_cast<size_t> (i)], false, true, 4);
            g.drawText (name, juce::Rectangle<float> (area.getRight() + 8.0f, y - 8.0f, static_cast<float> (labelGutter) - 8.0f, 16.0f),
                        juce::Justification::centredLeft);
        }
    }
}

void LaneGraph::paint (juce::Graphics& g)
{
    const auto area = graphArea();
    g.setColour (theme::panel);
    g.fillRect (area);

    if (lane == Lane::notes)
        drawNoteGrid (g, area);

    const float stepWidth = area.getWidth() / static_cast<float> (pattern.length);
    const float base = isBipolar() ? area.getCentreY() : area.getBottom();

    for (int i = 0; i < pattern.length; ++i)
    {
        const auto& s = pattern.steps[static_cast<size_t> (i)];
        const float x = area.getX() + stepWidth * static_cast<float> (i);
        const float y = valueToY (s);

        auto bar = juce::Rectangle<float>::leftTopRightBottom (x, juce::jmin (y, base), x + stepWidth, juce::jmax (y, base));
        if (lane == Lane::notes && numEnabled == 0 && ! relativeNotes)
            continue;

        const bool belowLine = lane == Lane::chance && s.chance < 0;
        g.setColour (belowLine ? theme::fill.withMultipliedSaturation (0.4f) : theme::fill);
        g.fillRect (bar);
        g.setColour (theme::ink);
        g.drawRect (bar, 1.0f);

        if (stepWidth > 34.0f)
        {
            g.setColour (theme::ink.withAlpha (0.7f));
            g.setFont (theme::font (12.0f));
            auto label = juce::Rectangle<float> (x, y < base ? y - 16.0f : y, stepWidth, 16.0f);
            if (label.getY() < area.getY())
                label.setY (area.getY());
            g.drawText (valueText (s), label, juce::Justification::centred);
        }
    }

    // Step dividers and the centre line for bipolar lanes.
    const float dashes[] = { 4.0f, 3.0f };
    g.setColour (theme::ink);
    for (int i = 1; i < pattern.length; ++i)
    {
        const float x = area.getX() + stepWidth * static_cast<float> (i);
        g.drawDashedLine ({ x, area.getY(), x, area.getBottom() }, dashes, 2, 1.0f);
    }

    if (isBipolar())
    {
        g.setColour (theme::ink);
        g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
        g.setColour (theme::inkMuted);
        g.setFont (theme::font (12.0f));
        const auto gutter = juce::Rectangle<float> (area.getRight() + 6.0f, area.getY(), static_cast<float> (labelGutter) - 6.0f, area.getHeight());
        if (lane == Lane::octave)
        {
            g.drawText ("+3", gutter.withHeight (16.0f), juce::Justification::centredLeft);
            g.drawText ("0", gutter.withY (area.getCentreY() - 8.0f).withHeight (16.0f), juce::Justification::centredLeft);
            g.drawText ("-3", gutter.withY (area.getBottom() - 16.0f).withHeight (16.0f), juce::Justification::centredLeft);
        }
        else
        {
            g.drawText ("100%", gutter.withHeight (16.0f), juce::Justification::centredLeft);
            g.drawText ("1:2", gutter.withY (area.getCentreY() + 2.0f).withHeight (16.0f), juce::Justification::centredLeft);
            g.drawText ("1:8", gutter.withY (area.getBottom() - 16.0f).withHeight (16.0f), juce::Justification::centredLeft);
        }
    }

    g.setColour (theme::ink);
    g.drawRect (area, 1.0f);
}

void LaneGraph::applyAt (juce::Point<float> position)
{
    const int step = stepAt (position.x);

    // Fill in every step between the last position and this one, so fast drags draw a line.
    if (lastStep < 0)
    {
        setFromY (pattern.steps[static_cast<size_t> (step)], position.y);
    }
    else
    {
        const int from = juce::jmin (lastStep, step), to = juce::jmax (lastStep, step);
        for (int i = from; i <= to; ++i)
        {
            const float t = to == from ? 1.0f : static_cast<float> (i - lastStep) / static_cast<float> (step - lastStep);
            setFromY (pattern.steps[static_cast<size_t> (i)], lastY + (position.y - lastY) * t);
        }
    }

    lastStep = step;
    lastY = position.y;
    repaint();

    if (onEdit)
        onEdit (pattern);
}

void LaneGraph::mouseDown (const juce::MouseEvent& e)
{
    lastStep = -1;
    if (graphArea().contains (e.position))
        applyAt (e.position);
}

void LaneGraph::mouseDrag (const juce::MouseEvent& e)
{
    const auto area = graphArea();
    applyAt ({ juce::jlimit (area.getX(), area.getRight() - 1.0f, e.position.x), e.position.y });
}

void LaneGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto& s = pattern.steps[static_cast<size_t> (stepAt (e.position.x))];
    switch (lane)
    {
        case Lane::notes:    s.note = 0.0f; break;
        case Lane::velocity: s.velocity = pattern.defaultVelocity; break;
        case Lane::gate:     s.gate = pattern.defaultGate; break;
        case Lane::octave:   s.octave = 0; break;
        case Lane::ratchet:  s.ratchet = 1; break;
        case Lane::chance:   s.chance = 100; break;
    }
    repaint();
    if (onEdit)
        onEdit (pattern);
}

} // namespace patternarp
