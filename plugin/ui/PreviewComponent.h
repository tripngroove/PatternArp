#pragma once

#include "patternarp/Sequencer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace patternarp
{

// A small piano roll of what the patterns play, one colour per pattern. Hovering shows how
// long the loop is; dragging it out drops the notes into the DAW as a MIDI clip.
class PreviewComponent final : public juce::Component, public juce::SettableTooltipClient
{
public:
    PreviewComponent();

    // `notes` covers `beats` beats; `exportNotes` is what dragging out produces.
    void setNotes (std::vector<SequencedNote> notes, double beats, std::vector<SequencedNote> exportNotes, double exportBeats,
                   const juce::String& description);

    void paint (juce::Graphics& g) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    juce::File writeMidiFile() const;

    std::vector<SequencedNote> notes, exportNotes;
    double beats = 4.0, exportBeats = 4.0;
    bool dragging = false;
};

} // namespace patternarp
