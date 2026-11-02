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
constexpr int diceSize = 22;
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

// Some tabs show values that can be clicked: "Chance: Rest" opens a list, "CC: Off" a
// field to type the number in, and the lanes that can move on per note show their mode,
// "Velocity: Step", which opens a list too.
bool hasValue (Lane lane)
{
    return lane == Lane::chance || hasLaneMode (lane);
}

const char* laneModeName (LaneMode mode)
{
    return mode == LaneMode::note ? "Note" : "Step";
}


constexpr int ccFieldHeight = 28;
const char* const ccPlaceholder = "Enter MIDI CC";

juce::Font titleFont() { return theme::font (theme::titleSize, true); }
juce::Font tabFont (bool open) { return theme::font (15.0f, open); }
juce::Font stepsFont() { return theme::font (9.0f); }
constexpr int tabStepsHeight = 10; // the step count's line, tucked under the name

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
        const bool lengthsChanged = edited.laneLength != pattern.laneLength;
        pattern.steps = edited.steps;
        pattern.laneLength = edited.laneLength;
        if (lengthsChanged)
        {
            // While the length handle is dragged, so the tab's step count follows it.
            layoutTabs();
            repaint();
        }
        if (onChange)
            onChange (pattern);
    };

    keyboard.onNotesChange = [this] (const std::bitset<128>& notes) {
        // Notes switched on go through the entry, which writes them into the Notes lane in the
        // order added when the Piano Roll started out empty.
        const auto added = notes & ~pattern.enabledNotes;
        pattern.enabledNotes &= notes;
        for (int n = 0; n < 128; ++n)
            if (added.test (static_cast<size_t> (n)))
                entry.add (pattern, n);
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

    polyphonicButton.onClick = [this] { if (onMakePolyphonic) onMakePolyphonic(); };
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

    for (auto* b : { &polyphonicButton, &cloneButton, &deleteButton })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::panel);
        b->setColour (juce::TextButton::textColourOffId, theme::ink);
        addChildComponent (b);
    }
    deleteButton.setColour (juce::TextButton::textColourOffId, theme::danger);

    for (auto* c : std::initializer_list<juce::Component*> { &timing, &length, &direction, &graph, &keyboard, &dice })
        addAndMakeVisible (c);
    graph.setComponentID ("graph");
    keyboard.setComponentID ("keyboard");
    dice.setComponentID ("dice");
    dice.onClick = [this] {
        randomizeLane (pattern, graph.getLane(), static_cast<std::uint64_t> (juce::Random::getSystemRandom().nextInt64()));
        changed();
    };

    // The CC number, typed in. Return sets it (nothing turns the lane off), Escape leaves it,
    // and clicking away sets it if something was typed.
    ccField.setComponentID ("ccField");
    ccField.setFont (theme::font (15.0f));
    ccField.setInputRestrictions (3, "0123456789");
    ccField.setIndents (theme::padding, 6);
    ccField.setColour (juce::TextEditor::backgroundColourId, theme::panel);
    ccField.setColour (juce::TextEditor::textColourId, theme::ink);
    ccField.setColour (juce::TextEditor::outlineColourId, theme::ink);
    ccField.onReturnKey = [this] { closeCcField (true); };
    ccField.onEscapeKey = [this] { closeCcField (false); };
    ccField.onFocusLost = [this] { closeCcField (ccField.getText().trim().isNotEmpty()); };
    addChildComponent (ccField);

    addMouseListener (&hoverWatcher, true);
}

PatternPanel::~PatternPanel()
{
    removeMouseListener (&hoverWatcher);
    juce::Desktop::getInstance().removeGlobalMouseListener (&clickAway);
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
    dice.setColour (theme::patternColour (pattern.colour));
    for (auto* box : { &timing, &length, &direction })
        box->setAccent (theme::patternColour (pattern.colour));
    ccField.setColour (juce::TextEditor::highlightColourId, theme::tint (theme::patternColour (pattern.colour)));
    ccField.setColour (juce::TextEditor::focusedOutlineColourId, theme::patternColour (pattern.colour));
    updatePausedLook();
    layoutHeader();
    layoutTabs();

    canAddPatterns = canAdd;
    updateButtons();

    repaint();
}

void PatternPanel::setFamily (const juce::String& newParentName, bool newHasSubs)
{
    if (newParentName == parentName && newHasSubs == hasSubs)
        return;

    parentName = newParentName;
    hasSubs = newHasSubs;
    timing.setFixedText (parentName.isEmpty() ? juce::String() : "follows " + parentName);
    updateButtons();
    layoutHeader();
    repaint();
}

void PatternPanel::updateButtons()
{
    const bool sub = parentName.isNotEmpty();
    polyphonicButton.setTooltip ("Add a sub-pattern that plays a step with each note this pattern plays");
    cloneButton.setTooltip (sub ? "Add a copy of this sub-pattern under the same pattern" : "Add a copy of this pattern at the end");
    deleteButton.setTooltip (sub ? "Delete this sub-pattern" : hasSubs ? "Delete this pattern and its sub-patterns" : "Delete this pattern");
    polyphonicButton.setEnabled (canAddPatterns);
    cloneButton.setEnabled (canAddPatterns);
    updateHoverButtons();
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
    layoutHeader(); // the header and the tabs fit their values
    layoutTabs();
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

juce::String PatternPanel::tabName (Lane lane) const
{
    return hasValue (lane) ? juce::String (laneName (lane)) + ": " : juce::String (laneName (lane));
}

juce::String PatternPanel::tabValue (Lane lane) const
{
    if (lane == Lane::chance)
        return chanceModeName (static_cast<int> (pattern.chanceMode));
    if (lane == Lane::cc)
        return ccText (pattern.ccNumber);
    return {};
}

juce::String PatternPanel::tabMode (Lane lane) const
{
    // A CC lane that is off sends nothing, so how it moves on does not matter yet.
    if (! hasLaneMode (lane) || (lane == Lane::cc && pattern.ccNumber < 0))
        return {};
    return laneModeName (pattern.laneMode[static_cast<size_t> (lane)]);
}

int PatternPanel::tabDividerWidth (Lane lane) const
{
    // A light line between the CC number and its mode, spaced like the header's.
    return tabValue (lane).isNotEmpty() && tabMode (lane).isNotEmpty() ? theme::spacing * 2 + 1 : 0;
}

bool PatternPanel::tabHasCaret (Lane lane) const
{
    return lane == Lane::chance || tabMode (lane).isNotEmpty();
}

juce::String PatternPanel::tabSteps (Lane lane) const
{
    const int steps = laneLength (pattern, lane);
    if (steps >= pattern.length)
        return {};
    return juce::String (steps) + (steps == 1 ? " Step" : " Steps");
}

int PatternPanel::tabWidth (Lane lane) const
{
    // Measured bold, as the open tab is drawn.
    return theme::padding * 2 + tabContentWidth (lane, tabFont (true));
}

int PatternPanel::tabContentWidth (Lane lane, const juce::Font& nameFont) const
{
    const auto valueFont = tabFont (true);
    const int caret = tabHasCaret (lane) ? caretGap + static_cast<int> (theme::caretWidth) : 0;
    return theme::textWidth (nameFont, tabName (lane)) + theme::textWidth (valueFont, tabValue (lane))
         + tabDividerWidth (lane) + theme::textWidth (valueFont, tabMode (lane)) + caret;
}

void PatternPanel::layoutTabs()
{
    // Tabs sit on the graph, lined up with its box.
    const auto g = graph.getBounds();
    auto row = juce::Rectangle<int> (g.getX() + 1, g.getY() - tabHeight, g.getWidth() - 2, tabHeight);

    for (const auto lane : laneOrder)
    {
        const auto i = static_cast<size_t> (lane);
        const auto tab = row.removeFromLeft (tabWidth (lane));
        tabs[i] = tab;
        row.setLeft (tab.getRight() - 1); // neighbours share a dividing line

        // Clicking a value ("Rest", "Off") opens its list or field, and the mode ("Step") its
        // list. Each reaches back over the space before it, and the last to the tab's edge.
        tabValues[i] = tabModes[i] = {};
        if (! hasValue (lane))
            continue;

        auto rest = tab.withTrimmedLeft (theme::padding + theme::textWidth (tabFont (true), juce::String (laneName (lane)) + ":"));
        if (const auto value = tabValue (lane); value.isNotEmpty())
            tabValues[i] = tabMode (lane).isEmpty()
                ? rest
                : rest.removeFromLeft (theme::textWidth (tabFont (true), " " + value) + tabDividerWidth (lane) / 2);
        if (tabMode (lane).isNotEmpty())
            tabModes[i] = rest;
    }
}

void PatternPanel::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (1 + stripeWidth + theme::spacing).withTrimmedRight (theme::spacing).reduced (0, verticalMargin);

    headerRow = area.removeFromTop (headerHeight);
    layoutHeader();

    area.removeFromTop (sectionGap + tabHeight);
    // The graph draws its box 1px in from its bounds: widen it so the box lines up with the
    // play button, the keyboard and the tabs.
    graph.setBounds (area.removeFromTop (graphHeight).expanded (1, 0));
    area.removeFromTop (sectionGap);
    keyboard.setBounds (area.removeFromTop (keyboardHeight).withTrimmedRight (LaneGraph::labelGutter));

    // The dice sits on the tabs' row, its right edge on the graph's (which the keyboard's matches).
    dice.setBounds (juce::Rectangle<int> (keyboard.getRight() - diceSize, graph.getY() - tabHeight, diceSize, tabHeight).withSizeKeepingCentre (diceSize, diceSize));

    layoutTabs();
}

void PatternPanel::layoutHeader()
{
    // One row, fitted to what it says: play/pause, "Pattern A | Step: 1/16 | Length: 3 |
    // Dir: Forward", with Clone and Delete on the right.
    auto header = headerRow;
    playButton = header.removeFromLeft (headerHeight);
    header.removeFromLeft (theme::spacing);
    titleArea = header.removeFromLeft (theme::textWidth (titleFont(), "Pattern " + theme::patternName (pattern)));

    // The spacing either side of a divider counts from the text, not a value's hover backing.
    int textEnd = titleArea.getRight();
    size_t divider = 0;
    for (auto* box : { &timing, &length, &direction })
    {
        dividers[divider++] = textEnd + theme::spacing;
        box->setBounds (header.withLeft (textEnd + theme::spacing * 2 + 1).withWidth (box->getPreferredWidth()));
        textEnd = box->getRight() - ValueBox::valueInset;
    }

    header = headerRow;
    for (auto* b : { &deleteButton, &cloneButton, &polyphonicButton })
    {
        const int width = buttonWidth (b->getButtonText() == "Sure?" ? "Delete" : b->getButtonText());
        b->setBounds (header.removeFromRight (width).withSizeKeepingCentre (width, buttonHeight));
        header.removeFromRight (theme::spacing);
    }
}

void PatternPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, theme::cornerRadius);
    g.setColour (theme::patternColour (pattern.colour));
    g.fillRoundedRectangle (bounds.withWidth (static_cast<float> (stripeWidth + 4)), theme::cornerRadius);
    g.setColour (theme::ink.withAlpha (0.25f));
    g.drawRoundedRectangle (bounds, theme::cornerRadius, 1.0f);

    theme::drawPlayButton (g, playButton.toFloat(), theme::patternColour (pattern.colour), pattern.playing);

    g.setColour (theme::separator);
    for (const int x : dividers)
        g.fillRect (x, playButton.getY() + 6, 1, playButton.getHeight() - 12);

    g.setColour (pattern.playing ? theme::ink : theme::inkMuted);
    g.setFont (titleFont());
    g.drawText ("Pattern " + theme::patternName (pattern), titleArea, juce::Justification::centredLeft);

    // Lane tabs: one flat strip, neighbours sharing a dividing line. The open one is white,
    // outlined in ink and runs into the graph below (see paintOverChildren); the others are
    // grey. Closed lines are opaque so shared ones do not darken. Chance and CC end in a value.
    const auto active = graph.getLane();
    const auto closedLine = theme::panel.overlaidWith (theme::ink.withAlpha (0.3f));
    for (const bool open : { false, true })
    {
        for (int i = 0; i < numLanes; ++i)
        {
            const auto lane = static_cast<Lane> (i);
            if ((lane == active) != open)
                continue;

            // Down to the graph's top line, so the sides join it.
            const auto tab = tabs[static_cast<size_t> (i)];
            const auto shape = tab.toFloat().withBottom (static_cast<float> (graph.getY()) + 1.0f);
            g.setColour (open ? theme::ink : closedLine);
            g.fillRect (shape);
            g.setColour (open ? theme::panel : (i == hoveredTab ? theme::tint (theme::patternColour (pattern.colour)) : theme::background));
            g.fillRect (shape.reduced (1.0f, 0.0f).withTrimmedTop (1.0f));

            // The name and values centred, so the padding is even either side. Values are
            // bold in the pattern's colour, like every value that can be edited.
            const auto nameFont = tabFont (open), valueFont = tabFont (true);
            const auto name = tabName (lane), value = tabValue (lane), mode = tabMode (lane);
            auto content = tab.withSizeKeepingCentre (tabContentWidth (lane, nameFont), tab.getHeight());

            // A lane that loops over fewer steps than the pattern says how many, small, under
            // its name, without moving it or widening the tab.
            if (const auto steps = tabSteps (lane); steps.isNotEmpty())
            {
                g.setColour (theme::inkMuted);
                g.setFont (stepsFont());
                g.drawText (steps, content.withTop (tab.getBottom() - tabStepsHeight - 1).withRight (tab.getRight() - 2),
                            juce::Justification::centredLeft);
            }
            g.setColour (open ? theme::ink : theme::ink.withAlpha (0.7f));
            g.setFont (nameFont);
            g.drawText (name, content.removeFromLeft (theme::textWidth (nameFont, name)), juce::Justification::centredLeft);
            // Faded with the rest of a paused pattern's values.
            const auto colour = pattern.playing ? theme::patternColour (pattern.colour) : theme::tint (theme::patternColour (pattern.colour));
            g.setColour (colour);
            g.setFont (valueFont);
            g.drawText (value, content.removeFromLeft (theme::textWidth (valueFont, value)), juce::Justification::centredLeft);
            if (const int divider = tabDividerWidth (lane); divider > 0)
            {
                g.setColour (theme::separator);
                g.fillRect (content.getX() + theme::spacing, tab.getY() + 6, 1, tab.getHeight() - 12);
                content.removeFromLeft (divider);
                g.setColour (colour);
            }
            g.drawText (mode, content, juce::Justification::centredLeft);
            if (tabHasCaret (lane))
                theme::drawCaret (g, { static_cast<float> (content.getRight()) - theme::caretWidth / 2.0f, static_cast<float> (content.getCentreY()) + 1.0f });
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
        if (tabModes[static_cast<size_t> (tab)].contains (pos))
        {
            showModeMenu (static_cast<Lane> (tab));
        }
        else if (tabValues[static_cast<size_t> (tab)].contains (pos))
        {
            if (tab == static_cast<int> (Lane::cc))
                showCcField();
            else
                showChanceMenu();
        }
    }
}

void PatternPanel::showChanceMenu()
{
    juce::PopupMenu menu;
    const juce::Component::SafePointer<PatternPanel> safe (this);
    {
        static constexpr const char* items[] = { "Rest: silent, keeps its time", "Skip: takes no time", "Extend: holds the note before" };
        menu.addSectionHeader ("When a step's chance fails");
        for (int i = 0; i < numChanceModes; ++i)
        {
            if (pattern.isSub() && static_cast<ChanceMode> (i) == ChanceMode::skip)
                continue; // a sub-pattern has no time to skip

            menu.addItem (items[i], true, static_cast<int> (pattern.chanceMode) == i, [safe, i] {
                if (safe != nullptr)
                {
                    safe->pattern.chanceMode = static_cast<ChanceMode> (i);
                    safe->changed();
                }
            });
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
        localAreaToGlobal (tabs[static_cast<size_t> (Lane::chance)])));
}

void PatternPanel::showModeMenu (Lane lane)
{
    juce::PopupMenu menu;
    const juce::Component::SafePointer<PatternPanel> safe (this);
    static constexpr const char* items[] = { "Step: one value for the whole step", "Note: each ratchet and repeat takes the next value" };
    menu.addSectionHeader (juce::String (laneName (lane)) + " moves on per");
    for (const auto mode : { LaneMode::step, LaneMode::note })
        menu.addItem (items[static_cast<int> (mode)], true, pattern.laneMode[static_cast<size_t> (lane)] == mode, [safe, lane, mode] {
            if (safe != nullptr)
            {
                safe->pattern.laneMode[static_cast<size_t> (lane)] = mode;
                safe->changed();
            }
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
        localAreaToGlobal (tabs[static_cast<size_t> (lane)])));
}

void PatternPanel::showCcField()
{
    // Just under the CC tab, where a list would drop down, holding the number to edit.
    const auto tab = tabs[static_cast<size_t> (Lane::cc)];
    const int width = juce::jmax (tab.getWidth(), theme::textWidth (ccField.getFont(), ccPlaceholder) + theme::padding * 2 + 4);
    ccField.setBounds (tab.getX(), tab.getBottom() + 4, width, ccFieldHeight);
    ccField.setText (pattern.ccNumber >= 0 ? juce::String (pattern.ccNumber) : juce::String(), false);
    ccField.setVisible (true);
    ccField.toFront (true);
    ccField.selectAll();
    juce::Desktop::getInstance().addGlobalMouseListener (&clickAway);
}

void PatternPanel::CcField::paintOverChildren (juce::Graphics& g)
{
    juce::TextEditor::paintOverChildren (g);
    if (isEmpty())
    {
        g.setColour (theme::inkMuted);
        g.setFont (getFont());
        g.drawText (ccPlaceholder, getLocalBounds().withTrimmedLeft (getLeftIndent() + 2), juce::Justification::centredLeft);
    }
}

void PatternPanel::ClickAwayWatcher::mouseDown (const juce::MouseEvent& e)
{
    // The click that opens the field comes here too: it is on the CC tab's value.
    const auto& value = panel.tabValues[static_cast<size_t> (Lane::cc)];
    if (e.eventComponent == &panel && value.contains (e.getEventRelativeTo (&panel).getPosition()))
        return;

    auto& field = panel.ccField;
    if (e.eventComponent != &field && ! field.isParentOf (e.eventComponent))
        panel.closeCcField (field.getText().trim().isNotEmpty());
}

void PatternPanel::closeCcField (bool apply)
{
    if (! ccField.isVisible())
        return;

    ccField.setVisible (false); // first, so losing focus here does not apply it twice
    juce::Desktop::getInstance().removeGlobalMouseListener (&clickAway);
    const auto text = ccField.getText().trim();
    if (apply)
    {
        // A number sets the CC (up to 127); nothing turns the lane off.
        pattern.ccNumber = text.isEmpty() ? -1 : juce::jlimit (0, 127, text.getIntValue());
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
    // Closed tabs open their lane; values open their list or field.
    const int over = tabAt (e.getPosition());
    const bool onValue = over >= 0
                      && (tabValues[static_cast<size_t> (over)].contains (e.getPosition()) || tabModes[static_cast<size_t> (over)].contains (e.getPosition()));
    const bool closedTab = over >= 0 && over != static_cast<int> (graph.getLane());
    const bool clickable = onValue || closedTab || playButton.contains (e.getPosition());
    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);

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
    polyphonicButton.setVisible (show && parentName.isEmpty()); // one level of sub-patterns
    cloneButton.setVisible (show);
    deleteButton.setVisible (show || confirmingDelete);
}

} // namespace patternarp
