#pragma once

#include "PluginProcessor.h"
#include "ui/OrderLane.h"
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
    void copyStateForDebugging();
    void setLoopLength (double beats); // 0 = natural length
    juce::Rectangle<int> titleArea() const;

    void edit (int index, const Pattern& pattern);
    void addPattern (const Pattern& pattern); // always at the end
    void deletePattern (int index);
    void scrollToPattern (int index);
    void animateScroll();

    PatternArpProcessor& arp;
    std::unique_ptr<juce::PropertiesFile> settings; // per-user preferences, shared by every instance
    Arrangement shown;
    std::bitset<128> shownInput;
    bool previewDirty = true;
    // The preview follows playback one pass at a time: the loop length (or the visible part).
    double previewPassBeats = 0.0;
    std::int64_t shownPass = -1;
    std::vector<int> previewInput;

    void updatePlayhead();

    juce::TooltipWindow tooltips { this, 500 };
    PreviewComponent preview;
    OrderLane order;

    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<PatternPanel> panels;
    juce::TextButton addButton { "+ ADD PATTERN" }; // beside the Rotation lane

    // A quick scroll down to a pattern that was just added, so it is clear where it went.
    double scrollFrom = 0.0, scrollTo = 0.0, scrollStartMs = -1.0; // -1: not scrolling
    juce::VBlankAttachment scrollAnimation { this, [this] { animateScroll(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternArpEditor)
};

} // namespace patternarp
