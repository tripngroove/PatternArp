#include "PatternPanel.h"

#include "Theme.h"

namespace patternarp
{

namespace
{
// Vertical spacing sticks to an 8-pixel grid.
constexpr int margin = 16;
constexpr int stripeWidth = 6;
constexpr int headerHeight = 56;
constexpr int sectionGap = 16;
constexpr int tabHeight = 32;
constexpr int closedTabDrop = 4; // closed tabs sit this much lower than the open one
constexpr int graphHeight = 168;
constexpr int keyboardHeight = 80;
constexpr int defaultBoxWidth = 44;
constexpr int titleWidth = 142;
constexpr int chanceModeWidth = 60;

static_assert (PatternPanel::preferredHeight == margin + headerHeight + sectionGap + tabHeight + graphHeight + sectionGap + keyboardHeight + margin);

const char* directionName (int d)
{
    static const char* names[] = { "Forward", "Backward", "Ping-pong", "Random", "Walk" };
    return names[juce::jlimit (0, numDirections - 1, d)];
}

bool hasDefault (Lane lane)
{
    return lane == Lane::velocity || lane == Lane::gate;
}
} // namespace

PatternPanel::PatternPanel()
{
    timing.setRange (0, numTimings - 1);
    timing.toText = [] (int v) { return juce::String (timingName (static_cast<Timing> (v))); };
    timing.onChange = [this] (int v) {
        pattern.timing = static_cast<Timing> (v);
        changed();
    };

    length.setRange (1, maxSteps);
    length.onChange = [this] (int v) {
        setPatternLength (pattern, v);
        changed();
    };

    direction.setRange (0, numDirections - 1);
    direction.toText = [] (int v) { return juce::String (directionName (v)); };
    direction.onChange = [this] (int v) {
        pattern.direction = static_cast<Direction> (v);
        changed();
    };

    graph.onEdit = [this] (const Pattern& edited) {
        pattern.steps = edited.steps;
        pattern.laneLength = edited.laneLength;
        if (onChange)
            onChange (pattern);
    };

    keyboard.onNotesChange = [this] (const std::bitset<128>& notes) {
        pattern.enabledNotes = notes;
        changed();
    };
    keyboard.onSourceChange = [this] (NoteSource source) {
        pattern.noteSource = source;
        changed();
    };
    keyboard.onLearnChange = [this] (bool armed) {
        if (armed)
        {
            learner = {}; // each take writes the Notes lane from the first step
            graph.setLane (Lane::notes);
            layoutTabs();
            repaint();
        }
    };

    defaultEditor.setEditable (true, true, false);
    defaultEditor.setJustificationType (juce::Justification::centred);
    defaultEditor.setFont (theme::font (14.0f, true));
    defaultEditor.setColour (juce::Label::textColourId, theme::ink);
    defaultEditor.setColour (juce::Label::backgroundColourId, theme::panel);
    defaultEditor.setColour (juce::Label::outlineColourId, theme::ink);
    defaultEditor.setTooltip ("Default value: new and double-clicked steps use it");
    defaultEditor.onTextChange = [this] { setDefault (graph.getLane(), defaultEditor.getText().getIntValue()); };

    cloneButton.setTooltip ("Add a copy of this pattern at the end");
    deleteButton.setTooltip ("Delete this pattern");
    cloneButton.onClick = [this] { if (onClone) onClone(); };
    deleteButton.onClick = [this] {
        if (! confirmingDelete)
        {
            // Ask once more on the button itself; it goes back if not clicked again soon.
            confirmingDelete = true;
            deleteButton.setButtonText ("Sure?");
            startTimer (3000);
            return;
        }
        stopTimer();
        confirmingDelete = false;
        deleteButton.setButtonText ("Delete");
        if (onDelete)
            onDelete();
    };

    for (auto* b : { &cloneButton, &deleteButton })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::panel);
        b->setColour (juce::TextButton::textColourOffId, theme::ink);
        addChildComponent (b);
    }
    deleteButton.setColour (juce::TextButton::textColourOffId, theme::danger);

    for (auto* c : std::initializer_list<juce::Component*> { &timing, &length, &direction, &graph, &keyboard })
        addAndMakeVisible (c);
    addChildComponent (defaultEditor);

    addMouseListener (&hoverWatcher, true);
}

PatternPanel::~PatternPanel()
{
    removeMouseListener (&hoverWatcher);
}

void PatternPanel::setPattern (const Pattern& newPattern, bool canAdd)
{
    pattern = newPattern;
    setName ("Pattern " + theme::patternName (pattern));

    timing.setValue (static_cast<int> (pattern.timing));
    length.setValue (pattern.length);
    direction.setValue (static_cast<int> (pattern.direction));
    graph.setPattern (pattern);
    keyboard.setNotes (pattern.enabledNotes, pattern.noteSource);
    keyboard.setColour (theme::patternColour (pattern.colour));
    updatePausedLook();

    cloneButton.setEnabled (canAdd);

    if (hasDefault (graph.getLane()) && ! defaultEditor.isBeingEdited())
        defaultEditor.setText (juce::String (graph.getLane() == Lane::velocity ? pattern.defaultVelocity : pattern.defaultGate),
                               juce::dontSendNotification);
    repaint();
}

void PatternPanel::learnNote (int note)
{
    if (! keyboard.isLearning())
        return;

    learner.learn (pattern, note);
    length.setValue (pattern.length);
    keyboard.setNotes (pattern.enabledNotes, pattern.noteSource);
    changed();
}

void PatternPanel::updatePausedLook()
{
    // A paused pattern stays editable but is drawn faded.
    const float alpha = pattern.playing ? 1.0f : 0.45f;
    for (auto* c : std::initializer_list<juce::Component*> { &timing, &length, &direction, &graph, &keyboard })
        c->setAlpha (alpha);
}

void PatternPanel::changed()
{
    graph.setPattern (pattern);
    updatePausedLook();
    repaint();
    if (onChange)
        onChange (pattern);
}

void PatternPanel::setDefault (Lane lane, int value)
{
    // Steps still at the old default follow the new one.
    if (lane == Lane::velocity)
    {
        value = juce::jlimit (1, 127, value);
        for (auto& s : pattern.steps)
            if (s.velocity == pattern.defaultVelocity)
                s.velocity = value;
        pattern.defaultVelocity = value;
    }
    else if (lane == Lane::gate)
    {
        value = juce::jlimit (1, 100, value);
        for (auto& s : pattern.steps)
            if (s.gate == pattern.defaultGate)
                s.gate = value;
        pattern.defaultGate = value;
    }
    defaultEditor.setText (juce::String (value), juce::dontSendNotification);
    changed();
}

void PatternPanel::timerCallback()
{
    stopTimer();
    confirmingDelete = false;
    deleteButton.setButtonText ("Delete");
}

void PatternPanel::layoutTabs()
{
    // Tabs sit on the graph, lined up with its box.
    const auto g = graph.getBounds();
    auto row = juce::Rectangle<int> (g.getX() + 1, g.getY() - tabHeight, g.getWidth() - LaneGraph::labelGutter - 2, tabHeight);
    const auto f = theme::font (15.0f);

    for (int i = 0; i < numLanes; ++i)
    {
        const auto lane = static_cast<Lane> (i);
        int w = juce::GlyphArrangement::getStringWidthInt (f, laneName (lane)) + 28;
        if (hasDefault (lane))
            w += defaultBoxWidth;
        tabs[static_cast<size_t> (i)] = row.removeFromLeft (w);
        row.removeFromLeft (4);
    }

    // Rest / Skip / Extend choice for the Chance tab sits at the end of the row.
    auto choice = row.removeFromRight (chanceModeWidth * numChanceModes).reduced (0, 4).translated (0, -2);
    for (auto& button : chanceModeButtons)
        button = choice.removeFromLeft (chanceModeWidth);

    const auto lane = graph.getLane();
    if (hasDefault (lane))
    {
        const auto tab = tabs[static_cast<size_t> (lane)];
        defaultEditor.setBounds (tab.withTrimmedLeft (tab.getWidth() - defaultBoxWidth - 6).withWidth (defaultBoxWidth).reduced (0, 5).translated (0, 1));
        defaultEditor.setText (juce::String (lane == Lane::velocity ? pattern.defaultVelocity : pattern.defaultGate), juce::dontSendNotification);
        defaultEditor.setVisible (true);
    }
    else
    {
        defaultEditor.setVisible (false);
    }
}

void PatternPanel::resized()
{
    auto area = getLocalBounds().reduced (margin).withTrimmedLeft (stripeWidth);

    auto header = area.removeFromTop (headerHeight);
    playButton = { header.getX(), header.getBottom() - 33, 32, 32 }; // level with the value boxes
    header.removeFromLeft (titleWidth); // play/pause and pattern title

    auto buttons = header.removeFromRight (156).removeFromTop (26);
    deleteButton.setBounds (buttons.removeFromRight (74));
    buttons.removeFromRight (4);
    cloneButton.setBounds (buttons.removeFromRight (70));

    timing.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (12);
    length.setBounds (header.removeFromLeft (90));
    header.removeFromLeft (12);
    direction.setBounds (header.removeFromLeft (140));

    area.removeFromTop (sectionGap + tabHeight);
    graph.setBounds (area.removeFromTop (graphHeight));
    area.removeFromTop (sectionGap);
    keyboard.setBounds (area.removeFromTop (keyboardHeight).withTrimmedRight (LaneGraph::labelGutter));

    layoutTabs();
}

void PatternPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (theme::patternColour (pattern.colour));
    g.fillRoundedRectangle (bounds.withWidth (static_cast<float> (stripeWidth + 4)), 6.0f);
    g.setColour (theme::ink.withAlpha (0.25f));
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    // Play/pause: filled with the pattern colour, with pause bars, while playing; an outlined
    // play triangle while paused.
    {
        const auto r = playButton.toFloat();
        const bool hover = isMouseOver (true) && playButton.contains (getMouseXYRelative());
        g.setColour (pattern.playing ? theme::patternColour (pattern.colour) : (hover ? theme::hover : theme::panel));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::ink);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        const auto icon = r.withSizeKeepingCentre (12.0f, 12.0f);
        g.setColour (pattern.playing ? juce::Colours::white : theme::ink);
        if (pattern.playing)
        {
            g.fillRect (icon.withWidth (4.0f));
            g.fillRect (icon.withTrimmedLeft (8.0f));
        }
        else
        {
            juce::Path p;
            p.addTriangle (icon.getX() + 1.0f, icon.getY(), icon.getRight(), icon.getCentreY(), icon.getX() + 1.0f, icon.getBottom());
            g.fillPath (p);
        }
    }

    auto title = playButton.withX (playButton.getRight() + 10).withWidth (titleWidth - playButton.getWidth() - 10);
    g.setColour (pattern.playing ? theme::ink : theme::inkMuted);
    g.setFont (theme::font (17.0f, true));
    g.drawText ("PATTERN " + theme::patternName (pattern), title, juce::Justification::centredLeft);
    if (! pattern.playing)
    {
        g.setFont (theme::font (12.0f));
        g.drawText ("Paused", playButton.withWidth (titleWidth).translated (0, -24).withHeight (22), juce::Justification::bottomLeft);
    }

    // Lane tabs. The open one is white and runs into the graph below (see paintOverChildren);
    // the others sit a little lower in grey. Velocity and Gate show their default value.
    const auto active = graph.getLane();
    for (int i = 0; i < numLanes; ++i)
    {
        const auto lane = static_cast<Lane> (i);
        const bool on = lane == active;
        const auto tab = on ? tabs[static_cast<size_t> (i)] : tabs[static_cast<size_t> (i)].withTrimmedTop (closedTabDrop);

        // Down to the graph's top line, so the sides join it.
        const auto shape = tab.toFloat().withBottom (static_cast<float> (graph.getY()) + 1.0f);
        juce::Path outside, inside;
        outside.addRoundedRectangle (shape.getX(), shape.getY(), shape.getWidth(), shape.getHeight(), 5.0f, 5.0f, true, true, false, false);
        inside.addRoundedRectangle (shape.getX() + 1.0f, shape.getY() + 1.0f, shape.getWidth() - 2.0f, shape.getHeight() - 1.0f, 4.0f, 4.0f,
                                    true, true, false, false);
        g.setColour (on ? theme::ink : theme::ink.withAlpha (0.3f));
        g.fillPath (outside);
        g.setColour (on ? theme::panel : (i == hoveredTab ? theme::hover : theme::background));
        g.fillPath (inside);
        g.setFont (theme::font (15.0f, on));

        auto text = tab.reduced (12, 0);
        if (hasDefault (lane))
        {
            auto value = text.removeFromRight (defaultBoxWidth - 6);
            if (! on)
            {
                g.setColour (theme::inkMuted);
                g.drawText ("(" + juce::String (lane == Lane::velocity ? pattern.defaultVelocity : pattern.defaultGate) + ")",
                            value, juce::Justification::centredRight);
            }
        }
        g.setColour (on ? theme::ink : theme::ink.withAlpha (0.7f));
        g.drawText (laneName (lane), text, juce::Justification::centredLeft);

        // A badge on lanes that loop over fewer steps than the pattern.
        if (const int steps = laneLength (pattern, lane); steps < pattern.length)
        {
            const auto badge = juce::Rectangle<float> (static_cast<float> (tab.getRight()) - 9.0f, static_cast<float> (tab.getY()) - 7.0f, 16.0f, 14.0f);
            g.setColour (theme::fillStrong);
            g.fillRoundedRectangle (badge, 7.0f);
            g.setColour (juce::Colours::white);
            g.setFont (theme::font (11.0f, true));
            g.drawText (juce::String (steps), badge, juce::Justification::centred);
        }
    }

    if (active == Lane::chance)
    {
        static constexpr const char* labels[] = { "Rest", "Skip", "Extend" };
        g.setFont (theme::font (14.0f));
        for (int i = 0; i < numChanceModes; ++i)
        {
            const auto r = chanceModeButtons[static_cast<size_t> (i)];
            g.setColour (static_cast<int> (pattern.chanceMode) == i ? theme::fill : theme::panel);
            g.fillRect (r);
            g.setColour (theme::ink);
            g.drawRect (r, 1);
            g.drawText (labels[i], r, juce::Justification::centred);
        }
        // Labelled, if there is room after the tabs.
        const auto label = chanceModeButtons.front().translated (-90, 0).withWidth (86);
        if (label.getX() > tabs.back().getRight() + 8)
        {
            g.setColour (theme::inkMuted);
            g.setFont (theme::font (12.0f));
            g.drawText ("Failed steps:", label, juce::Justification::centredRight);
        }
    }
}

void PatternPanel::paintOverChildren (juce::Graphics& g)
{
    // Open the graph's top line under the open tab.
    const auto tab = tabs[static_cast<size_t> (graph.getLane())];
    g.setColour (theme::panel);
    g.fillRect (tab.getX() + 1, graph.getY() + 1, tab.getWidth() - 2, 1);
}

void PatternPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (playButton.contains (pos))
    {
        pattern.playing = ! pattern.playing;
        changed();
        return;
    }

    if (const int tab = tabAt (pos); tab >= 0)
    {
        graph.setLane (static_cast<Lane> (tab));
        layoutTabs();
        repaint();
        return;
    }

    if (graph.getLane() == Lane::chance)
        for (int i = 0; i < numChanceModes; ++i)
            if (chanceModeButtons[static_cast<size_t> (i)].contains (pos))
            {
                pattern.chanceMode = static_cast<ChanceMode> (i);
                changed();
            }
}

void PatternPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-clicking a tab puts every step of that lane back to its default value.
    if (const int tab = tabAt (e.getPosition()); tab >= 0)
    {
        for (auto& s : pattern.steps)
            resetStep (s, static_cast<Lane> (tab), pattern);
        changed();
    }
}

void PatternPanel::mouseMove (const juce::MouseEvent& e)
{
    if (const int tab = tabAt (e.getPosition()); tab != hoveredTab)
    {
        hoveredTab = tab;
        repaint();
    }
}

void PatternPanel::mouseExit (const juce::MouseEvent&)
{
    if (hoveredTab >= 0)
    {
        hoveredTab = -1;
        repaint();
    }
}

int PatternPanel::tabAt (juce::Point<int> p) const
{
    for (int i = 0; i < numLanes; ++i)
        if (tabs[static_cast<size_t> (i)].contains (p))
            return i;
    return -1;
}

void PatternPanel::updateHoverButtons()
{
    const bool show = isMouseOver (true);
    cloneButton.setVisible (show);
    deleteButton.setVisible (show || confirmingDelete);
}

} // namespace patternarp
