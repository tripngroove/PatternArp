#pragma once

#include "PluginProcessor.h"
#include "ui/PatternPanel.h"
#include "ui/PreviewComponent.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace patternarp
{

class PatternArpEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PatternArpEditor (PatternArpProcessor& processor);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    // Brings the panels in line with the processor (automation, project load, undo...).
    void refreshFromProcessor();
    void syncPanels();
    void layoutContent();
    void updatePreview();
    void showSettingsMenu();
    void setLoopLength (double beats); // 0 = natural length
    juce::Rectangle<int> titleArea() const;

    void edit (int index, const Pattern& pattern);
    void insertPattern (int index, const Pattern& pattern);
    void deletePattern (int index);

    PatternArpProcessor& arp;
    std::unique_ptr<juce::PropertiesFile> settings; // per-user preferences, shared by every instance
    Arrangement shown;
    std::bitset<128> shownInput;
    bool previewDirty = true;
    double previewLoopBeats = 0.0; // how often playback repeats, 0 if never

    juce::TooltipWindow tooltips { this, 500 };
    juce::ToggleButton latchButton { "Latch" };
    juce::ButtonParameterAttachment latchAttachment;
    PreviewComponent preview;

    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<PatternPanel> panels;
    juce::TextButton addButton { "+ ADD PATTERN" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternArpEditor)
};

} // namespace patternarp
