#include "PreviewComponent.h"

#include "Theme.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <numeric>
#include <tuple>

namespace patternarp
{

namespace
{
constexpr float padding = 10.0f; // keeps the first/last and highest/lowest notes off the frame
constexpr float handleGrab = 8.0f;
constexpr float tabWidth = 12.0f;
} // namespace

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
    setTooltip (description + "\nDrag the notes into your DAW to get them as a MIDI clip. Drag the handle to shorten the loop.");
    repaint();
}

void PreviewComponent::setLoop (double newHandleBeats, double newSnapBeats, const juce::String& newNaturalLabel)
{
    if (! draggingHandle)
        handleBeats = juce::jlimit (0.0, beats, newHandleBeats);
    snapBeats = juce::jmax (1.0 / ticksPerBeat, newSnapBeats);
    naturalLabel = newNaturalLabel;
    repaint();
}

void PreviewComponent::setPass (std::vector<SequencedNote> current, std::vector<SequencedNote> next, double newPassBeats)
{
    currentPass = std::move (current);
    nextPass = std::move (next);
    passBeats = newPassBeats;
    repaint();
}

std::vector<SequencedNote> PreviewComponent::visibleNotes() const
{
    if (passBeats <= 0.0)
        return notes;

    std::vector<SequencedNote> shown;
    for (const auto& n : nextPass)
        if (n.start < playhead)
            shown.push_back (n);
    for (const auto& n : currentPass)
        if (n.start >= playhead)
            shown.push_back (n);
    for (const auto& n : notes) // past a shortened loop: what would have played
        if (n.start >= passBeats - 1.0e-9)
            shown.push_back (n);
    return shown;
}

void PreviewComponent::setPlayhead (double beat)
{
    if (std::abs (beat - playhead) > 1.0e-4)
    {
        playhead = beat;
        repaint();
    }
}

juce::Rectangle<float> PreviewComponent::boxArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (static_cast<float> (rulerHeight)).reduced (1.0f);
}

juce::Rectangle<float> PreviewComponent::rollArea() const
{
    return boxArea().reduced (padding, padding * 0.8f);
}

float PreviewComponent::beatToX (double beat) const
{
    const auto roll = rollArea();
    return roll.getX() + static_cast<float> (beat / beats) * roll.getWidth();
}

juce::String PreviewComponent::label() const
{
    return isShortened() ? "Repeats every " + juce::String (describeLength (handleBeats)) : naturalLabel;
}

bool PreviewComponent::isOverHandle (juce::Point<float> p) const
{
    return std::abs (p.x - beatToX (handleBeats)) < handleGrab;
}

void PreviewComponent::paint (juce::Graphics& g)
{
    const auto box = boxArea();
    const auto roll = rollArea();
    g.setColour (theme::panel);
    g.fillRect (box);

    // Beat lines, with stronger bar lines.
    const float pxPerBeat = roll.getWidth() / static_cast<float> (beats);
    for (int b = 0; b <= static_cast<int> (std::floor (beats + 1.0e-6)); ++b)
    {
        g.setColour (b % 4 == 0 ? theme::gridLine : theme::gridLine.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (roll.getX() + pxPerBeat * static_cast<float> (b)), box.getY(), box.getBottom());
    }

    const auto drawn = visibleNotes();
    if (drawn.empty())
    {
        g.setColour (theme::inkMuted);
        g.setFont (theme::font (14.0f));
        g.drawText ("No notes", roll, juce::Justification::centred);
    }
    else
    {
        // One row per pitch that is used (like a folded piano roll), so notes stay chunky
        // even when they are far apart.
        std::array<int, 128> rowOf {};
        int rows = 0;
        for (const auto* list : { &notes, &currentPass, &nextPass }) // stable rows as notes reroll
            for (const auto& n : *list)
                rowOf[static_cast<size_t> (n.note)] = 1;
        for (int pitch = 0; pitch < 128; ++pitch)
            if (rowOf[static_cast<size_t> (pitch)] != 0)
                rowOf[static_cast<size_t> (pitch)] = rows++;

        const float rowHeight = juce::jmin (18.0f, roll.getHeight() / static_cast<float> (rows));
        const float bottom = roll.getCentreY() + rowHeight * static_cast<float> (rows) / 2.0f;

        // Walk each row in time order so back-to-back notes (ratchets, mostly) keep a 1px gap
        // and still read as separate hits when zoomed far out.
        std::vector<size_t> order (drawn.size());
        std::iota (order.begin(), order.end(), size_t { 0 });
        std::sort (order.begin(), order.end(), [&drawn] (size_t a, size_t b)
                   { return std::tie (drawn[a].note, drawn[a].start) < std::tie (drawn[b].note, drawn[b].start); });

        for (size_t k = 0; k < order.size(); ++k)
        {
            const auto& n = drawn[order[k]];
            const float x = roll.getX() + static_cast<float> (n.start) * pxPerBeat;
            float width = juce::jmax (3.0f, static_cast<float> (n.length) * pxPerBeat - 1.0f);
            if (k + 1 < order.size() && drawn[order[k + 1]].note == n.note)
                width = juce::jmin (width, roll.getX() + static_cast<float> (drawn[order[k + 1]].start) * pxPerBeat - x - 1.0f);

            const auto row = static_cast<float> (rowOf[static_cast<size_t> (n.note)]);
            const auto r = juce::Rectangle<float> (x, bottom - (row + 1.0f) * rowHeight, juce::jmax (1.0f, width), juce::jmax (2.0f, rowHeight - 2.0f));
            const auto colour = theme::patternColour (colourOf[static_cast<size_t> (juce::jlimit (0, maxPatterns - 1, n.pattern))]);

            // Slivers get a solid fill on whole pixels: an outline or blur would swallow them.
            if (r.getWidth() < 4.0f)
            {
                g.setColour (colour.darker (0.2f));
                g.fillRect (r.withX (std::round (x)).withWidth (juce::jmax (1.0f, std::round (r.getWidth()))));
                continue;
            }

            g.setColour (colour.withAlpha (0.45f + 0.55f * static_cast<float> (n.velocity) / 127.0f));
            g.fillRoundedRectangle (r, 1.5f);
            g.setColour (colour.darker (0.4f));
            g.drawRoundedRectangle (r, 1.5f, 0.75f);
        }
    }

    // Past a shortened loop the notes never play: fade them out.
    const float hx = beatToX (handleBeats);
    if (isShortened())
    {
        g.setColour (theme::background.withAlpha (0.75f));
        g.fillRect (box.withLeft (hx));
    }

    if (playhead >= 0.0 && playhead <= beats)
    {
        g.setColour (theme::danger);
        g.fillRect (juce::Rectangle<float> (beatToX (playhead) - 0.5f, box.getY(), 1.0f, box.getHeight()));
    }

    g.setColour (theme::ink);
    g.drawRect (box, 1.0f);

    // Loop handle: a line through the roll with a grab tab and the repeat label in the ruler.
    const auto tab = juce::Rectangle<float> (hx - tabWidth / 2.0f, 2.0f, tabWidth, static_cast<float> (rulerHeight) - 4.0f);
    theme::drawLengthHandle (g, tab, box.getBottom());

    const auto text = label();
    const auto font = theme::font (15.0f, true);
    const float textWidth = static_cast<float> (juce::GlyphArrangement::getStringWidthInt (font, text)) + 4.0f;
    const bool fitsRight = tab.getRight() + 8.0f + textWidth < static_cast<float> (getWidth());
    const auto textArea = fitsRight ? juce::Rectangle<float> (tab.getRight() + 8.0f, 0.0f, textWidth, static_cast<float> (rulerHeight))
                                    : juce::Rectangle<float> (tab.getX() - 8.0f - textWidth, 0.0f, textWidth, static_cast<float> (rulerHeight));
    g.setFont (font);
    g.setColour (theme::ink);
    g.drawText (text, textArea, fitsRight ? juce::Justification::centredLeft : juce::Justification::centredRight);
}

void PreviewComponent::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (isOverHandle (e.position) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void PreviewComponent::mouseDown (const juce::MouseEvent& e)
{
    draggingHandle = isOverHandle (e.position);
    draggingOut = false;
}

void PreviewComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingHandle)
    {
        // Snap to the grid; near the right end the loop goes back to its natural length.
        const auto roll = rollArea();
        const double raw = (e.position.x - roll.getX()) / roll.getWidth() * beats;
        double snapped = juce::jmax (snapBeats, std::round (raw / snapBeats) * snapBeats);
        if (snapped >= beats - 1.0e-6)
            snapped = beats;

        if (std::abs (snapped - handleBeats) > 1.0e-9)
        {
            handleBeats = snapped;
            repaint();
            if (onLoopChange)
                onLoopChange (isShortened() ? handleBeats : 0.0);
        }
        return;
    }

    if (draggingOut || e.getDistanceFromDragStart() < 6 || exportNotes.empty())
        return;

    draggingOut = true;
    const auto file = writeMidiFile();
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                [this] { draggingOut = false; });
}

void PreviewComponent::mouseUp (const juce::MouseEvent&)
{
    draggingOut = false;
    draggingHandle = false;
}

juce::File PreviewComponent::writeMidiFile() const
{
    constexpr int ticksPerQuarter = 960;
    juce::MidiMessageSequence track;
    for (const auto& n : exportNotes)
    {
        const double on = n.start * ticksPerQuarter;
        const double off = (n.start + n.length) * ticksPerQuarter;
        track.addEvent (juce::MidiMessage::noteOn (1, n.note, static_cast<juce::uint8> (n.velocity)), on);
        track.addEvent (juce::MidiMessage::noteOff (1, n.note), off);
    }
    track.addEvent (juce::MidiMessage::endOfTrack(), exportBeats * ticksPerQuarter);
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ticksPerQuarter);
    file.addTrack (track);

    auto out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PatternArp.mid");
    out.deleteFile();
    if (juce::FileOutputStream stream (out); stream.openedOk())
        file.writeTo (stream);
    return out;
}

} // namespace patternarp
