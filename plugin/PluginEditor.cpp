#include "PluginEditor.h"

#include "ui/Theme.h"

#include <algorithm>

namespace patternarp
{

namespace
{
constexpr int topBarHeight = 56;
constexpr int previewHeight = PreviewComponent::rulerHeight + 112;
constexpr int gap = 12;
constexpr int addButtonHeight = 44;

constexpr double maxPreviewBeats = 64.0; // 16 bars on screen
constexpr double maxExportBeats = 256.0; // 64 bars when dragged out
constexpr double nonRepeatingBeats = 16.0;

// Settings keys.
const char* const exportEveryNoteKey = "exportEveryNote";
const char* const exportWholeBarsKey = "exportWholeBars";
const char* const loopSnapKey = "loopSnap"; // "timing" or "bars"

// How many times to repeat a loop so it ends on a bar line (4/4), or 1 if that would make
// the clip longer than `maxBeats`.
int repeatsToFillBars (double loopBeats, double maxBeats)
{
    for (int repeats = 1; repeats * loopBeats <= maxBeats + 1.0e-6; ++repeats)
    {
        const double bars = repeats * loopBeats / 4.0;
        if (std::abs (bars - std::round (bars)) < 1.0e-6)
            return repeats;
    }
    return 1;
}

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "PatternArp";
    options.filenameSuffix = ".settings";
    options.folderName = "PatternArp";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}
} // namespace

PatternArpEditor::PatternArpEditor (PatternArpProcessor& p)
    : AudioProcessorEditor (p), arp (p), settings (std::make_unique<juce::PropertiesFile> (settingsOptions())), latchAttachment (p.getLatchParameter(), latchButton)
{
    latchButton.setColour (juce::ToggleButton::textColourId, theme::ink);
    latchButton.setColour (juce::ToggleButton::tickColourId, theme::ink);
    latchButton.setColour (juce::ToggleButton::tickDisabledColourId, theme::inkMuted);
    latchButton.setTooltip ("MIDI In patterns keep playing the last chord after you let go");
    addAndMakeVisible (latchButton);
    preview.onLoopChange = [this] (double beats) { setLoopLength (beats); };
    addAndMakeVisible (preview);

    addButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    addButton.setColour (juce::TextButton::textColourOffId, theme::ink);
    addButton.onClick = [this] { insertPattern (shown.numPatterns, makeDefaultPattern()); };
    content.addAndMakeVisible (addButton);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    shown = arp.getArrangement();
    syncPanels();

    setResizable (true, true);
    setResizeLimits (860, 560, 1800, 2400);
    setSize (1000, 880);

    updatePreview();
    startTimerHz (30);
}

void PatternArpEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    g.setColour (theme::ink);
    g.setFont (theme::font (22.0f, true));
    g.drawText ("PatternArp", titleArea(), juce::Justification::centredLeft);

    // A small caret says the title opens the settings menu.
    const auto caret = titleArea().toFloat().removeFromRight (16.0f).withSizeKeepingCentre (9.0f, 6.0f);
    juce::Path p;
    p.addTriangle (caret.getX(), caret.getY(), caret.getRight(), caret.getY(), caret.getCentreX(), caret.getBottom());
    g.setColour (theme::inkMuted);
    g.fillPath (p);
}

juce::Rectangle<int> PatternArpEditor::titleArea() const
{
    return { gap, 0, 130, topBarHeight };
}

void PatternArpEditor::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (titleArea().contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void PatternArpEditor::mouseDown (const juce::MouseEvent& e)
{
    if (titleArea().contains (e.getPosition()))
        showSettingsMenu();
}

void PatternArpEditor::showSettingsMenu()
{
    const bool everyNote = settings->getBoolValue (exportEveryNoteKey, true);

    juce::PopupMenu dragOut;
    dragOut.addItem ("Every possible note", true, everyNote, [this] {
        settings->setValue (exportEveryNoteKey, true);
        previewDirty = true;
    });
    dragOut.addItem ("Roll the dice (what the preview shows)", true, ! everyNote, [this] {
        settings->setValue (exportEveryNoteKey, false);
        previewDirty = true;
    });

    juce::PopupMenu menu;
    menu.addSectionHeader ("Settings");
    menu.addSubMenu ("Chance steps in dragged-out clips", dragOut);

    // DAWs such as Bitwig round a dropped MIDI clip up to a whole bar, which leaves a gap in
    // the loop unless the clip already ends on a bar line.
    const bool wholeBars = settings->getBoolValue (exportWholeBarsKey, true);
    menu.addItem ("Drag out whole bars (repeat the loop until it ends on a bar line)", true, wholeBars, [this, wholeBars] {
        settings->setValue (exportWholeBarsKey, ! wholeBars);
        previewDirty = true;
    });
    const bool snapToBars = settings->getValue (loopSnapKey, "timing") == "bars";
    juce::PopupMenu snap;
    snap.addItem ("Smallest pattern timing", true, ! snapToBars, [this] {
        settings->setValue (loopSnapKey, "timing");
        previewDirty = true;
    });
    snap.addItem ("Bars", true, snapToBars, [this] {
        settings->setValue (loopSnapKey, "bars");
        previewDirty = true;
    });
    menu.addSubMenu ("Loop handle snaps to", snap);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
        localAreaToGlobal (titleArea().withTrimmedTop (topBarHeight - 8))));
}

void PatternArpEditor::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (topBarHeight).reduced (gap, 0);
    latchButton.setBounds (bar.removeFromRight (90).withSizeKeepingCentre (90, 28));

    area.reduce (gap, 0);
    preview.setBounds (area.removeFromTop (previewHeight));
    area.removeFromTop (gap);
    viewport.setBounds (area);
    layoutContent();
}

void PatternArpEditor::layoutContent()
{
    const int width = viewport.getMaximumVisibleWidth();
    int y = 0;
    for (auto* panel : panels)
    {
        panel->setBounds (0, y, width, PatternPanel::preferredHeight);
        y += PatternPanel::preferredHeight + gap;
    }

    addButton.setBounds (0, y, width, addButtonHeight);
    addButton.setEnabled (shown.numPatterns < maxPatterns);
    content.setSize (width, y + addButtonHeight + gap);
}

void PatternArpEditor::syncPanels()
{
    while (panels.size() > shown.numPatterns)
        panels.removeLast();

    while (panels.size() < shown.numPatterns)
    {
        auto* panel = panels.add (new PatternPanel());
        const int index = panels.size() - 1;
        panel->onChange = [this, index] (const Pattern& p) { edit (index, p); };
        panel->onNew = [this, index] { insertPattern (index + 1, makeDefaultPattern()); };
        panel->onClone = [this, index] { insertPattern (index + 1, shown.patterns[static_cast<size_t> (index)]); };
        panel->onDelete = [this, index] { deletePattern (index); };
        content.addAndMakeVisible (panel);
    }

    for (int i = 0; i < panels.size(); ++i)
    {
        panels[i]->setPattern (shown.patterns[static_cast<size_t> (i)], i, shown.numPatterns < maxPatterns);
        panels[i]->setInputNotes (shownInput);
    }

    layoutContent();
    previewDirty = true;
}

void PatternArpEditor::refreshFromProcessor()
{
    if (const auto current = arp.getArrangement(); ! (current == shown))
    {
        shown = current;
        syncPanels();
    }

    if (const auto input = arp.getInputNotes(); input != shownInput)
    {
        shownInput = input;
        for (auto* panel : panels)
            panel->setInputNotes (input);
        if (shown.usesMidiIn())
            previewDirty = true;
    }
}

void PatternArpEditor::edit (int index, const Pattern& pattern)
{
    if (index >= shown.numPatterns)
        return;

    shown.patterns[static_cast<size_t> (index)] = pattern;
    arp.setArrangement (shown);
    previewDirty = true;
}

void PatternArpEditor::insertPattern (int index, const Pattern& pattern)
{
    if (shown.numPatterns >= maxPatterns)
        return;

    index = juce::jlimit (0, shown.numPatterns, index);
    for (int i = shown.numPatterns; i > index; --i)
        shown.patterns[static_cast<size_t> (i)] = shown.patterns[static_cast<size_t> (i - 1)];
    shown.patterns[static_cast<size_t> (index)] = pattern;
    ++shown.numPatterns;

    arp.setArrangement (shown);
    syncPanels();
}

void PatternArpEditor::deletePattern (int index)
{
    if (index < 0 || index >= shown.numPatterns)
        return;

    for (int i = index; i < shown.numPatterns - 1; ++i)
        shown.patterns[static_cast<size_t> (i)] = shown.patterns[static_cast<size_t> (i + 1)];
    --shown.numPatterns;

    arp.setArrangement (shown);

    // Called from the deleted panel's button, so let the click finish before removing it.
    juce::Component::SafePointer<PatternArpEditor> self (this);
    juce::MessageManager::callAsync ([self] {
        if (self != nullptr)
            self->syncPanels();
    });
}

void PatternArpEditor::updatePreview()
{
    previewDirty = false;

    // MIDI In patterns preview with what is held, or a C major chord when nothing is.
    std::vector<int> input;
    for (int n = 0; n < 128; ++n)
        if (shownInput.test (static_cast<size_t> (n)))
            input.push_back (n);
    if (input.empty() && shown.usesMidiIn())
        input = { 60, 64, 67 };

    // The preview always shows the natural loop, so the loop handle can be dragged back out.
    Arrangement natural = shown;
    natural.loopLength = 0;
    const auto naturalLoop = findLoop (natural);
    const double viewBeats = juce::jmin (naturalLoop.repeats ? naturalLoop.lengthBeats : nonRepeatingBeats, maxPreviewBeats);

    const auto loop = findLoop (shown); // the loop length set by hand, if there is one
    previewLoopBeats = loop.repeats ? loop.lengthBeats : 0.0;

    double exportBeats = loop.repeats ? juce::jmin (loop.lengthBeats, maxExportBeats) : nonRepeatingBeats;
    if (loop.repeats && settings->getBoolValue (exportWholeBarsKey, true))
        exportBeats = loop.lengthBeats * repeatsToFillBars (loop.lengthBeats, maxExportBeats);

    const bool anyPlaying = std::any_of (shown.patterns.begin(), shown.patterns.begin() + shown.numPatterns,
                                         [] (const Pattern& pt) { return pt.playing; });

    juce::String label, description;
    if (shown.numPatterns == 0)
    {
        description = "No patterns";
    }
    else if (! anyPlaying)
    {
        label = description = "All patterns paused";
    }
    else if (loop.repeats)
    {
        label = "Repeats every " + juce::String (describeLength (loop.lengthBeats));
        description = "Loop length: " + juce::String (describeLength (loop.lengthBeats));
        if (viewBeats < (naturalLoop.repeats ? naturalLoop.lengthBeats : viewBeats))
            description << " (showing the first " << describeLength (viewBeats) << ")";
    }
    else
    {
        label = "Never repeats exactly";
        description = "Random directions never repeat exactly; showing " + juce::String (describeLength (viewBeats));
    }

    if (shown.numPatterns > 0)
        description << "\nDragged-out clip: " << describeLength (exportBeats);

    // By default dragged-out clips hold every note a percent-chance step could play, so chance
    // can be set again in the DAW (MIDI files have no way to carry it). The other setting
    // exports the roll the preview shows.
    Arrangement exported = shown;
    if (settings->getBoolValue (exportEveryNoteKey, true))
        for (auto& pattern : exported.patterns)
            for (auto& step : pattern.steps)
                if (step.chance > 0)
                    step.chance = 100;

    preview.setNotes (render (natural, viewBeats, input), viewBeats, render (exported, exportBeats, input), exportBeats, description);

    // The handle snaps to bars or to the shortest step of any playing pattern.
    double snap = 4.0;
    if (settings->getValue (loopSnapKey, "timing") != "bars")
    {
        snap = 1.0;
        for (int i = 0; i < shown.numPatterns; ++i)
            if (const auto& pt = shown.patterns[static_cast<size_t> (i)]; pt.playing)
                snap = juce::jmin (snap, beatsPerStep (pt.timing));
    }

    const double handle = shown.loopLength > 0 ? juce::jmin (viewBeats, previewLoopBeats) : viewBeats;
    preview.setLoop (handle, snap, label);
}

void PatternArpEditor::setLoopLength (double beats)
{
    shown.loopLength = beats > 0.0 ? static_cast<int> (std::lround (beats * ticksPerBeat)) : 0;
    arp.setArrangement (shown);
    previewDirty = true;
}

void PatternArpEditor::timerCallback()
{
    refreshFromProcessor();
    if (previewDirty)
        updatePreview();

    // The playhead wraps with the loop.
    double position = arp.getPlayPosition();
    if (position >= 0.0 && previewLoopBeats > 0.0)
        position = std::fmod (position, previewLoopBeats);
    preview.setPlayhead (position);
}

} // namespace patternarp
