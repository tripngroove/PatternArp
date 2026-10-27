#include "PatternPanel.h"

#include "Theme.h"

namespace patternarp
{

namespace
{
constexpr int margin = 14;
constexpr int stripeWidth = 6;
constexpr int headerHeight = 56;
constexpr int tabHeight = 32;
constexpr int graphHeight = 170 + LaneGraph::handleStrip;
constexpr int keyboardHeight = 78;
constexpr int defaultBoxWidth = 44;
constexpr int titleWidth = 142;

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

    defaultEditor.setEditable (true, true, false);
    defaultEditor.setJustificationType (juce::Justification::centred);
    defaultEditor.setFont (theme::font (14.0f, true));
    defaultEditor.setColour (juce::Label::textColourId, theme::ink);
    defaultEditor.setColour (juce::Label::backgroundColourId, theme::panel);
    defaultEditor.setColour (juce::Label::outlineColourId, theme::ink);
    defaultEditor.setTooltip ("Default value: new and double-clicked steps use it");
    defaultEditor.onTextChange = [this] { setDefault (graph.getLane(), defaultEditor.getText().getIntValue()); };

    newButton.setTooltip ("Add a new pattern at the end");
    cloneButton.setTooltip ("Add a copy of this pattern at the end");
    deleteButton.setTooltip ("Delete this pattern");
    newButton.onClick = [this] { if (onNew) onNew(); };
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

    for (auto* b : { &newButton, &cloneButton, &deleteButton })
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

    newButton.setEnabled (canAdd);
    cloneButton.setEnabled (canAdd);

    if (hasDefault (graph.getLane()) && ! defaultEditor.isBeingEdited())
        defaultEditor.setText (juce::String (graph.getLane() == Lane::velocity ? pattern.defaultVelocity : pattern.defaultGate),
                               juce::dontSendNotification);
    repaint();
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
    auto row = getLocalBounds().reduced (margin).withTrimmedLeft (stripeWidth).withTrimmedTop (headerHeight + 10).withHeight (tabHeight);
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

    // Rest / Skip choice for the Chance tab sits at the end of the row.
    auto choice = row.removeFromRight (140).reduced (0, 4);
    restButton = choice.removeFromLeft (70);
    skipButton = choice;

    const auto lane = graph.getLane();
    if (hasDefault (lane))
    {
        const auto tab = tabs[static_cast<size_t> (lane)];
        defaultEditor.setBounds (tab.withTrimmedLeft (tab.getWidth() - defaultBoxWidth - 6).withWidth (defaultBoxWidth).reduced (0, 5));
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
    playButton = header.withWidth (30).withSizeKeepingCentre (30, 30).withY (header.getY() + 22);
    header.removeFromLeft (titleWidth); // play/pause and pattern title

    auto buttons = header.removeFromRight (230).removeFromTop (26);
    deleteButton.setBounds (buttons.removeFromRight (74));
    buttons.removeFromRight (4);
    cloneButton.setBounds (buttons.removeFromRight (70));
    buttons.removeFromRight (4);
    newButton.setBounds (buttons.removeFromRight (70));

    timing.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (12);
    length.setBounds (header.removeFromLeft (90));
    header.removeFromLeft (12);
    direction.setBounds (header.removeFromLeft (140));

    area.removeFromTop (10 + tabHeight + 6);
    graph.setBounds (area.removeFromTop (graphHeight));
    area.removeFromTop (10);
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

    // Lane tabs. The active one is filled; Velocity and Gate show their default value.
    const auto active = graph.getLane();
    for (int i = 0; i < numLanes; ++i)
    {
        const auto lane = static_cast<Lane> (i);
        const auto tab = tabs[static_cast<size_t> (i)];
        const bool on = lane == active;

        g.setColour (on ? theme::fill : theme::panel);
        g.fillRect (tab);
        g.setColour (theme::ink);
        g.drawRect (tab, 1);
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
        g.setColour (theme::ink);
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
        const bool skip = pattern.chanceMode == ChanceMode::skip;
        g.setFont (theme::font (14.0f));
        for (const auto& [r, on, label] : { std::tuple { restButton, ! skip, "Rest" }, std::tuple { skipButton, skip, "Skip" } })
        {
            g.setColour (on ? theme::fill : theme::panel);
            g.fillRect (r);
            g.setColour (theme::ink);
            g.drawRect (r, 1);
            g.drawText (label, r, juce::Justification::centred);
        }
        g.setColour (theme::inkMuted);
        g.setFont (theme::font (12.0f));
        g.drawText ("Failed steps:", restButton.withX (restButton.getX() - 90).withWidth (86), juce::Justification::centredRight);
    }
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

    for (int i = 0; i < numLanes; ++i)
    {
        if (tabs[static_cast<size_t> (i)].contains (pos))
        {
            graph.setLane (static_cast<Lane> (i));
            layoutTabs();
            repaint();
            return;
        }
    }

    if (graph.getLane() == Lane::chance && (restButton.contains (pos) || skipButton.contains (pos)))
    {
        pattern.chanceMode = skipButton.contains (pos) ? ChanceMode::skip : ChanceMode::rest;
        changed();
    }
}

void PatternPanel::updateHoverButtons()
{
    const bool show = isMouseOver (true);
    newButton.setVisible (show);
    cloneButton.setVisible (show);
    deleteButton.setVisible (show || confirmingDelete);
}

} // namespace patternarp
