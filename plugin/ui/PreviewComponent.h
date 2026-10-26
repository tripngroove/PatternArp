#pragma once

#include "patternarp/Sequencer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace patternarp
{

// A small piano roll of what the patterns play, one colour per pattern, with a playhead.
//
// A handle at the end of the loop can be dragged left to make everything start over sooner;
// its label shows how often the arp repeats. Dragging anywhere else drops the notes into the
// DAW as a MIDI clip.
class PreviewComponent final : public juce::Component, public juce::SettableTooltipClient
{
public:
    PreviewComponent();

    // `notes` covers `beats` beats (the natural loop, or as much of it as fits); `exportNotes`
    // is what dragging out produces.
    void setNotes (std::vector<SequencedNote> notes, double beats, std::vector<SequencedNote> exportNotes, double exportBeats,
                   const juce::String& description);

    // Where the loop handle sits (`beats` from setNotes means not shortened), the grid it
    // snaps to, and the label shown when it is not shortened.
    void setLoop (double handleBeats, double snapBeats, const juce::String& naturalLabel);

    // Playback position within the preview in beats, or a negative value to hide it.
    void setPlayhead (double beat);

    // Called while the handle is dragged: the new loop length in beats, or 0 for the natural one.
    std::function<void (double)> onLoopChange;

    static constexpr int rulerHeight = 24;

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    juce::Rectangle<float> boxArea() const;
    juce::Rectangle<float> rollArea() const;
    float beatToX (double beat) const;
    bool isOverHandle (juce::Point<float> p) const;
    bool isShortened() const { return handleBeats < beats - 1.0e-6; }
    juce::String label() const;
    juce::File writeMidiFile() const;

    std::vector<SequencedNote> notes, exportNotes;
    double beats = 4.0, exportBeats = 4.0;
    double handleBeats = 4.0, snapBeats = 0.25;
    double playhead = -1.0;
    juce::String naturalLabel;

    bool draggingOut = false, draggingHandle = false;
};

} // namespace patternarp
