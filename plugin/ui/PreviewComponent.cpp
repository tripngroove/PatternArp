#include "PreviewComponent.h"

#include "Theme.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace patternarp
{

PreviewComponent::PreviewComponent()
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void PreviewComponent::setNotes (std::vector<SequencedNote> newNotes, double newBeats, std::vector<SequencedNote> newExportNotes,
                                 double newExportBeats, const juce::String& description)
{
    notes = std::move (newNotes);
    beats = juce::jmax (0.25, newBeats);
    exportNotes = std::move (newExportNotes);
    exportBeats = juce::jmax (0.25, newExportBeats);
    setTooltip (description + "\nDrag into your DAW to get it as a MIDI clip.");
    repaint();
}

void PreviewComponent::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme::panel);
    g.fillRect (area);

    // Beat lines, with stronger bar lines.
    const float pxPerBeat = area.getWidth() / static_cast<float> (beats);
    for (int b = 1; b < static_cast<int> (std::ceil (beats)); ++b)
    {
        g.setColour (b % 4 == 0 ? theme::gridLine : theme::gridLine.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (area.getX() + pxPerBeat * static_cast<float> (b)), area.getY(), area.getBottom());
    }

    if (notes.empty())
    {
        g.setColour (theme::inkMuted);
        g.setFont (theme::font (14.0f));
        g.drawText ("No notes", area, juce::Justification::centred);
    }
    else
    {
        int low = 127, high = 0;
        for (const auto& n : notes)
        {
            low = juce::jmin (low, n.note);
            high = juce::jmax (high, n.note);
        }
        low -= 1;
        high += 1;
        const float rowHeight = juce::jmin (10.0f, area.getHeight() / static_cast<float> (high - low + 1));
        const float top = area.getCentreY() - rowHeight * static_cast<float> (high - low + 1) / 2.0f;

        for (const auto& n : notes)
        {
            const auto r = juce::Rectangle<float> (area.getX() + static_cast<float> (n.start) * pxPerBeat,
                                                   top + static_cast<float> (high - n.note) * rowHeight,
                                                   juce::jmax (2.0f, static_cast<float> (n.length) * pxPerBeat - 1.0f), rowHeight - 1.0f);
            g.setColour (theme::patternColour (n.pattern).withAlpha (0.4f + 0.6f * static_cast<float> (n.velocity) / 127.0f));
            g.fillRect (r);
        }
    }

    g.setColour (theme::ink);
    g.drawRect (area, 2.0f);
}

juce::File PreviewComponent::writeMidiFile() const
{
    constexpr int ticksPerBeat = 960;
    juce::MidiMessageSequence track;
    for (const auto& n : exportNotes)
    {
        const double on = n.start * ticksPerBeat;
        const double off = (n.start + n.length) * ticksPerBeat;
        track.addEvent (juce::MidiMessage::noteOn (1, n.note, static_cast<juce::uint8> (n.velocity)), on);
        track.addEvent (juce::MidiMessage::noteOff (1, n.note), off);
    }
    track.addEvent (juce::MidiMessage::endOfTrack(), exportBeats * ticksPerBeat);
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ticksPerBeat);
    file.addTrack (track);

    auto out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PatternArp.mid");
    out.deleteFile();
    if (juce::FileOutputStream stream (out); stream.openedOk())
        file.writeTo (stream);
    return out;
}

void PreviewComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6 || exportNotes.empty())
        return;

    dragging = true;
    const auto file = writeMidiFile();
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                [this] { dragging = false; });
}

void PreviewComponent::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
}

} // namespace patternarp
