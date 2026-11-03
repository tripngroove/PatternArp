#pragma once

#include "PluginProcessor.h"
#include "ui/OrderLane.h"
#include "ui/PatternPanel.h"
#include "ui/PreviewComponent.h"
#include "ui/Theme.h"
#include "ui/VariationStrip.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace patternarp
{

class PatternArpEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PatternArpEditor (PatternArpProcessor& processor);
    ~PatternArpEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    // Brings the panels in line with the processor (automation, project load, undo...).
    void refreshFromProcessor();

    // Sends the edited arrangement to the processor, unless the variation changed under the
    // editor (automation switched it), which would put the edit in the wrong one.
    void commit();
    void syncPanels();
    void layoutContent();
    void updatePreview();
    void showSettingsMenu();
    void copyStateForDebugging();
    void setLoopLength (double beats); // 0 = natural length
    void setStepDefaults (int velocity, int gate); // for every pattern
    juce::Rectangle<int> titleArea() const;
    SwitchTiming variationTiming() const; // when a clicked variation takes over (a setting)
    void updateVariations();

    void edit (int index, const Pattern& pattern);
    void addPattern (const Pattern& pattern); // always at the end
    void addSubPattern (int parent, const Pattern& pattern); // under the parent and its other sub-patterns
    void deletePattern (int index);
    void scrollToPattern (int index);
    void animateScroll();

    PatternArpProcessor& arp;
    std::unique_ptr<juce::PropertiesFile> settings; // per-user preferences, shared by every instance
    Arrangement shown;
    int shownVariation = 0;
    std::bitset<128> shownInput;
    bool previewDirty = true;
    // The preview follows playback one pass at a time: the loop length (or the visible part).
    double previewPassBeats = 0.0;
    std::int64_t shownPass = -1;
    std::vector<int> previewInput;

    void updatePlayhead();

    theme::LookAndFeel lookAndFeel; // before the components that use it, so it outlives them
    juce::TooltipWindow tooltips { this, 500 };
    VariationStrip variationStrip;
    PreviewComponent preview;
    OrderLane order;

    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<PatternPanel> panels;
    juce::TextButton addButton { "+ Add pattern" }; // beside the Rotation lane

    // A quick scroll down to a pattern that was just added, so it is clear where it went.
    double scrollFrom = 0.0, scrollTo = 0.0, scrollStartMs = -1.0; // -1: not scrolling
    juce::VBlankAttachment scrollAnimation { this, [this] { animateScroll(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternArpEditor)
};

} // namespace patternarp
