#include "PatternPanel.h"

#include "Theme.h"

namespace patternarp
{

namespace
{
// Vertical spacing sticks to an 8-pixel grid; horizontal spacing is theme::padding and
// theme::spacing.
constexpr int verticalMargin = 16;
constexpr int stripeWidth = 10; // the pattern's colour down the left edge
constexpr int headerHeight = 32;
constexpr int sectionGap = 16;
constexpr int tabHeight = 32;
constexpr int closedTabDrop = 4; // closed tabs sit this much lower than the open one
constexpr int graphHeight = 168;
constexpr int keyboardHeight = 80;
constexpr int buttonHeight = 28;
constexpr int caretGap = 4; // between a tab's value and its caret

static_assert (PatternPanel::preferredHeight
               == verticalMargin + headerHeight + sectionGap + tabHeight + graphHeight + sectionGap + keyboardHeight + verticalMargin);

const char* directionName (int d)
{
    static const char* names[] = { "Forward", "Backward", "Ping-pong", "Random", "Walk" };
    return names[juce::jlimit (0, numDirections - 1, d)];
}

const char* chanceModeName (int mode)
{
    static const char* names[] = { "Rest", "Skip", "Extend" };
    return names[juce::jlimit (0, numChanceModes - 1, mode)];
}

juce::String ccText (int ccNumber)
{
    return ccNumber < 0 ? juce::String ("Off") : juce::String (ccNumber);
}

// The Chance and CC tabs end in a dropdown: "Chance: Rest", "CC: Off".
bool hasMenu (Lane lane)
{
    return lane == Lane::chance || lane == Lane::cc;
}

juce::Font titleFont() { return theme::font (16.0f, true); }
juce::Font tabFont (bool open) { return theme::font (15.0f, open); }

int buttonWidth (const juce::String& text)
{
    return theme::textWidth (theme::font (15.0f), text) + theme::padding * 2;
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
            repaint();
        }
    };

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
    graph.setComponentID ("graph");
    keyboard.setComponentID ("keyboard");

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
    layoutTabs();

    cloneButton.setEnabled (canAdd);

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
    layoutTabs(); // "Chance: Rest" and "CC: Off" change width with their value
    repaint();
    if (onChange)
        onChange (pattern);
}

void PatternPanel::timerCallback()
{
    stopTimer();
    confirmingDelete = false;
    deleteButton.setButtonText ("Delete");
}

juce::String PatternPanel::tabText (Lane lane) const
{
    if (lane == Lane::chance)
        return juce::String (laneName (lane)) + ": " + chanceModeName (static_cast<int> (pattern.chanceMode));
    if (lane == Lane::cc)
        return juce::String (laneName (lane)) + ": " + ccText (pattern.ccNumber);
    return laneName (lane);
}

int PatternPanel::tabWidth (Lane lane) const
{
    // Measured bold, as the open tab is drawn.
    const int caret = hasMenu (lane) ? caretGap + static_cast<int> (theme::caretWidth) : 0;
    return theme::padding * 2 + theme::textWidth (tabFont (true), tabText (lane)) + caret;
}

void PatternPanel::layoutTabs()
{
    // Tabs sit on the graph, lined up with its box.
    const auto g = graph.getBounds();
    auto row = juce::Rectangle<int> (g.getX() + 1, g.getY() - tabHeight, g.getWidth() - 2, tabHeight);

    for (int i = 0; i < numLanes; ++i)
    {
        const auto lane = static_cast<Lane> (i);
        const auto tab = row.removeFromLeft (tabWidth (lane));
        tabs[static_cast<size_t> (i)] = tab;
        row.removeFromLeft (theme::spacing / 2);

        // Clicking the value part ("Rest", "Off") opens its list.
        tabValues[static_cast<size_t> (i)] = hasMenu (lane)
            ? tab.withTrimmedLeft (theme::padding + theme::textWidth (tabFont (true), juce::String (laneName (lane)) + ":"))
            : juce::Rectangle<int>();
    }
}

void PatternPanel::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (1 + stripeWidth + theme::spacing).withTrimmedRight (theme::spacing).reduced (0, verticalMargin);

    // One row: play/pause, the title, then Step, Length and Dir; Clone and Delete on the right.
    auto header = area.removeFromTop (headerHeight);
    playButton = header.removeFromLeft (headerHeight);
    header.removeFromLeft (theme::spacing);
    titleArea = header.removeFromLeft (theme::textWidth (titleFont(), "Pattern W")); // the same for every letter, so the boxes line up
    for (auto* box : { &timing, &length, &direction })
    {
        header.removeFromLeft (theme::spacing);
        box->setBounds (header.removeFromLeft (box->getPreferredWidth()));
    }

    deleteButton.setBounds (header.removeFromRight (buttonWidth ("Delete")).withSizeKeepingCentre (buttonWidth ("Delete"), buttonHeight));
    header.removeFromRight (theme::spacing);
    cloneButton.setBounds (header.removeFromRight (buttonWidth ("Clone")).withSizeKeepingCentre (buttonWidth ("Clone"), buttonHeight));

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

    g.setColour (pattern.playing ? theme::ink : theme::inkMuted);
    g.setFont (titleFont());
    g.drawText ("Pattern " + theme::patternName (pattern), titleArea, juce::Justification::centredLeft);

    // Lane tabs. The open one is white and runs into the graph below (see paintOverChildren);
    // the others sit a little lower in grey. Chance and CC end in a dropdown.
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

        // The name (and value) centred, so the padding is even either side.
        const auto f = tabFont (on);
        const auto text = tabText (lane);
        const int contentWidth = theme::textWidth (f, text) + (hasMenu (lane) ? caretGap + static_cast<int> (theme::caretWidth) : 0);
        auto content = tab.withSizeKeepingCentre (contentWidth, tab.getHeight());
        g.setColour (on ? theme::ink : theme::ink.withAlpha (0.7f));
        g.setFont (f);
        g.drawText (text, content, juce::Justification::centredLeft);
        if (hasMenu (lane))
            theme::drawCaret (g, { static_cast<float> (content.getRight()) - theme::caretWidth / 2.0f, static_cast<float> (content.getCentreY()) + 1.0f });

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
        repaint();
        if (tabValues[static_cast<size_t> (tab)].contains (pos))
            showTabMenu (static_cast<Lane> (tab));
    }
}

void PatternPanel::showTabMenu (Lane lane)
{
    juce::PopupMenu menu;
    const juce::Component::SafePointer<PatternPanel> safe (this);

    if (lane == Lane::chance)
    {
        static constexpr const char* items[] = { "Rest: silent, keeps its time", "Skip: takes no time", "Extend: holds the note before" };
        menu.addSectionHeader ("When a step's chance fails");
        for (int i = 0; i < numChanceModes; ++i)
            menu.addItem (items[i], true, static_cast<int> (pattern.chanceMode) == i, [safe, i] {
                if (safe != nullptr)
                {
                    safe->pattern.chanceMode = static_cast<ChanceMode> (i);
                    safe->changed();
                }
            });
    }
    else
    {
        // Off, then every CC number in columns of 16.
        auto choose = [safe] (int number) {
            if (safe != nullptr)
            {
                safe->pattern.ccNumber = number;
                safe->changed();
            }
        };
        menu.addItem ("Off", true, pattern.ccNumber < 0, [choose] { choose (-1); });
        for (int n = 0; n < 128; ++n)
        {
            if (n > 0 && n % 16 == 0)
                menu.addColumnBreak();
            menu.addItem (juce::String (n), true, pattern.ccNumber == n, [choose, n] { choose (n); });
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
        localAreaToGlobal (tabs[static_cast<size_t> (lane)])));
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
