#include "OrderLane.h"

#include "Theme.h"

namespace patternarp
{

namespace
{
constexpr float gridX = static_cast<float> (OrderLane::labelWidth); // where the steps start, after the labels
constexpr float labelX = static_cast<float> (theme::padding);
constexpr float headerHeight = static_cast<float> (OrderLane::collapsedHeight); // the label stays put when opened
constexpr float gridMargin = 8.0f; // above and below the open grid, as around the closed row

// The grid is a fixed size on whole pixels: each cell has a 1px line before it, then 2px of
// white all round its step.
constexpr float stepHeight = 24.0f; // a step's box, the same open or closed
constexpr float cellPadding = 2.0f;
constexpr float stepInset = 1.0f + cellPadding; // from a cell's left or top line to its step
constexpr float pitch = static_cast<float> (OrderLane::stepPitch);
constexpr float rowHeight = static_cast<float> (OrderLane::rowPitch);
static_assert (OrderLane::rowPitch == static_cast<int> (stepInset + stepHeight + cellPadding));

constexpr float playSize = stepHeight; // an open row's play/pause button, as tall as its steps
static_assert (OrderLane::nameX == theme::padding + static_cast<int> (playSize) + theme::spacing);

juce::Font nameFont() { return theme::font (14.0f, true); }
juce::String nameOf (const Pattern& p) { return "Pattern " + theme::patternName (p); }

// A small triangle pointing right (closed) or down (open).
void drawChevron (juce::Graphics& g, juce::Point<float> centre, bool open)
{
    juce::Path p;
    if (open)
        p.addTriangle (centre.x - 4.5f, centre.y - 2.5f, centre.x + 4.5f, centre.y - 2.5f, centre.x, centre.y + 3.0f);
    else
        p.addTriangle (centre.x - 2.5f, centre.y - 4.5f, centre.x - 2.5f, centre.y + 4.5f, centre.x + 3.0f, centre.y);
    g.fillPath (p);
}
} // namespace

OrderLane::OrderLane()
{
    setName ("Rotation");
    setRepaintsOnMouseActivity (false);
}

void OrderLane::setArrangement (const Arrangement& a)
{
    const int oldRows = numRows;
    arrangement = a;
    numRows = 0;
    for (int i = 0; i < arrangement.numPatterns; ++i)
        if (! arrangement.patterns[static_cast<size_t> (i)].isSub())
            rowPatterns[static_cast<size_t> (numRows++)] = i;
    const bool rowsChanged = numRows != oldRows;
    repaint();

    if (rowsChanged && expanded && onHeightChange)
        onHeightChange();
}

int OrderLane::getPreferredHeight() const
{
    if (! expanded)
        return collapsedHeight;

    const auto rows = static_cast<float> (juce::jmax (1, numRows));
    return juce::roundToInt (headerHeight + gridMargin + rows * rowHeight + 1.0f + gridMargin); // + the grid's bottom line
}

void OrderLane::setExpanded (bool shouldExpand)
{
    if (expanded == shouldExpand)
        return;

    expanded = shouldExpand;
    drag = Drag::none;
    repaint();
    if (onHeightChange)
        onHeightChange();
}

juce::Rectangle<float> OrderLane::headerArea() const
{
    return expanded ? boxArea().withHeight (headerHeight) : boxArea();
}

float OrderLane::handleX() const
{
    return gridX + pitch * static_cast<float> (arrangement.orderLength) + 0.5f; // the middle of the grid line there
}

bool OrderLane::isOnHandle (juce::Point<float> p) const
{
    return expanded && theme::lengthHandleTab (handleX(), gridArea().getVerticalRange()).expanded (4.0f).contains (p);
}

juce::Rectangle<float> OrderLane::gridArea() const
{
    const float width = pitch * static_cast<float> (maxOrderSteps) + 1.0f; // a line after the last cell too
    if (! expanded)
        return { gridX, (headerHeight - stepHeight) / 2.0f - stepInset, width, rowHeight }; // one row, its lines not drawn

    const auto rows = static_cast<float> (juce::jmax (1, numRows));
    return { gridX, headerHeight + gridMargin, width, rows * rowHeight + 1.0f };
}

int OrderLane::stepAt (float x) const
{
    return static_cast<int> (std::floor ((x - gridX) / pitch));
}

int OrderLane::rowAt (float y) const
{
    return static_cast<int> (std::floor ((y - gridArea().getY()) / rowHeight));
}

int OrderLane::playToggleAt (juce::Point<float> p) const
{
    if (! expanded || p.x < labelX)
        return -1;
    const int row = rowAt (p.y);
    if (row < 0 || row >= numRows)
        return -1;
    const auto name = nameOf (arrangement.patterns[static_cast<size_t> (patternOfRow (row))]);
    return p.x < static_cast<float> (nameX + juce::jmin (nameWidth, theme::textWidth (nameFont(), name))) ? row : -1;
}

void OrderLane::paint (juce::Graphics& g)
{
    const auto box = boxArea();
    g.setColour (theme::panel);
    g.fillRect (box);

    // The header (the whole lane while it is closed) lights up to show it can be clicked.
    const bool hover = isMouseOver() && headerArea().contains (getMouseXYRelative().toFloat()) && drag == Drag::none;
    if (hover)
    {
        g.setColour (theme::hover);
        g.fillRect (headerArea());
    }

    g.setColour (theme::ink);
    g.setFont (theme::font (theme::titleSize, true));
    const auto label = juce::Rectangle<float> (labelX, 0.0f, gridX - labelX, expanded ? headerHeight : static_cast<float> (getHeight()));
    g.drawText ("Rotation", label, juce::Justification::centredLeft);
    const float textWidth = static_cast<float> (theme::textWidth (theme::font (theme::titleSize, true), "Rotation"));
    g.setColour (hover ? theme::ink : theme::inkMuted);
    drawChevron (g, { label.getX() + textWidth + static_cast<float> (theme::spacing) + 4.0f, label.getCentreY() }, expanded);

    if (expanded)
        paintExpanded (g);
    else
        paintCollapsed (g);

    g.setColour (theme::ink);
    g.drawRect (box, 1.0f);
}

juce::Rectangle<float> OrderLane::stepBox (int step, float rowTop) const
{
    return { gridX + pitch * static_cast<float> (step) + stepInset, rowTop + stepInset, pitch - stepInset - cellPadding, stepHeight };
}

void OrderLane::paintStep (juce::Graphics& g, juce::Rectangle<float> r, int turn) const
{
    const int owner = arrangement.order[static_cast<size_t> (turn)];
    if (owner < 0 || owner >= arrangement.numPatterns)
    {
        // A rest (from older saves): an empty outline.
        g.setColour (theme::inkMuted);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::cornerRadius, 1.0f);
        return;
    }

    // Lit: filled in the pattern's colour with a white letter. Quiet (or paused): its tint, with
    // the letter in its colour.
    const auto& p = arrangement.patterns[static_cast<size_t> (owner)];
    const auto colour = theme::patternColour (p.colour);
    g.setFont (nameFont()); // the same as the row names, so the letters line up with them
    if (arrangement.isHeard (turn))
    {
        g.setColour (colour);
        g.fillRoundedRectangle (r, theme::cornerRadius);
        g.setColour (juce::Colours::white);
    }
    else
    {
        g.setColour (theme::tint (colour));
        g.fillRoundedRectangle (r, theme::cornerRadius);
        g.setColour (colour);
    }
    g.drawText (theme::patternName (p), r, juce::Justification::centred);
}

void OrderLane::paintCollapsed (juce::Graphics& g)
{
    const auto y = gridArea().getY();
    for (int i = 0; i < arrangement.orderLength; ++i)
        paintStep (g, stepBox (i, y), i);
}

void OrderLane::paintExpanded (juce::Graphics& g)
{
    const auto grid = gridArea();

    g.setColour (theme::ink.withAlpha (0.25f));
    g.drawHorizontalLine (juce::roundToInt (headerHeight), 1.0f, boxArea().getRight() - 1.0f);

    // Steps past the end of the lane are greyed out.
    const float end = gridX + pitch * static_cast<float> (arrangement.orderLength);
    g.setColour (theme::background);
    g.fillRect (grid.withLeft (end));

    // A row per pattern, labelled, with its steps drawn just as they are when the lane is closed.
    // The button, the name and the steps share one top and height, so their text lines up.
    for (int row = 0; row < numRows; ++row)
    {
        const int pattern = patternOfRow (row);
        const auto& p = arrangement.patterns[static_cast<size_t> (pattern)];
        const float top = grid.getY() + rowHeight * static_cast<float> (row);
        const auto button = juce::Rectangle<float> (labelX, top + stepInset, playSize, playSize);
        theme::drawPlayButton (g, button, theme::patternColour (p.colour), p.playing);
        g.setColour (theme::ink.withAlpha (p.playing ? 1.0f : 0.45f));
        g.setFont (nameFont());
        g.drawFittedText (nameOf (p), button.withX (static_cast<float> (nameX)).withWidth (static_cast<float> (nameWidth)).toNearestInt(),
                          juce::Justification::centredLeft, 1, nameMinScale);

        for (int i = 0; i < arrangement.orderLength; ++i)
            if (arrangement.order[static_cast<size_t> (i)] == pattern)
                paintStep (g, stepBox (i, top), i);
    }

    // Grid lines, darker every 4 steps like the bar lines in the preview. The light ones go in
    // one list, which draws each pixel once, so they don't darken where they cross.
    const auto gridInt = grid.toNearestInt();
    juce::RectangleList<int> lines;
    for (int row = 1; row < numRows; ++row)
        lines.add (gridInt.withY (gridInt.getY() + OrderLane::rowPitch * row).withHeight (1));
    for (int i = 1; i < maxOrderSteps; ++i)
        if (i % 4 != 0)
            lines.add (gridInt.withX (gridInt.getX() + stepPitch * i).withWidth (1));
    g.setColour (theme::gridLine.withAlpha (0.35f));
    g.fillRectList (lines);
    g.setColour (theme::gridLine);
    for (int i = 4; i < maxOrderSteps; i += 4)
        g.fillRect (gridInt.withX (gridInt.getX() + stepPitch * i).withWidth (1));
    g.setColour (theme::ink);
    g.drawRect (grid, 1.0f);

    paintHandle (g);
}

void OrderLane::paintHandle (juce::Graphics& g)
{
    // Length handle: a line through the grid with a grab tab, like the lane graphs.
    const auto grid = gridArea();
    const float hx = handleX();
    theme::drawLengthHandle (g, hx, grid.getVerticalRange());

    if (drag == Drag::length || (isMouseOver() && isOnHandle (getMouseXYRelative().toFloat())))
    {
        const int steps = arrangement.orderLength;
        theme::drawLengthHandleLabel (g, juce::String (steps) + (steps == 1 ? " step" : " steps"), theme::font (12.0f), hx,
                                      grid.getVerticalRange(), grid.getRight() - 2.0f);
    }
}

void OrderLane::mouseMove (const juce::MouseEvent& e)
{
    auto shape = juce::MouseCursor::NormalCursor;
    if (isOnHandle (e.position))
        shape = juce::MouseCursor::LeftRightResizeCursor;
    else if (headerArea().contains (e.position) || playToggleAt (e.position) >= 0)
        shape = juce::MouseCursor::PointingHandCursor;
    else if (const int step = stepAt (e.position.x), row = rowAt (e.position.y);
             step >= 0 && step < arrangement.orderLength && row >= 0 && row < numRows)
        shape = juce::MouseCursor::PointingHandCursor; // a turn to give, quiet or light
    setMouseCursor (shape);
    repaint();
}

void OrderLane::mouseEnter (const juce::MouseEvent&)
{
    repaint();
}

void OrderLane::mouseExit (const juce::MouseEvent&)
{
    repaint();
}

void OrderLane::mouseDown (const juce::MouseEvent& e)
{
    lastStep = -1;
    drag = Drag::none;

    if (isOnHandle (e.position))
    {
        drag = Drag::length;
        dragStart = arrangement;
        setLengthFromX (e.position.x);
        return;
    }

    if (headerArea().contains (e.position))
    {
        setExpanded (! expanded);
        return;
    }

    if (const int row = playToggleAt (e.position); row >= 0)
    {
        const int pattern = patternOfRow (row);
        arrangement.setPlaying (pattern, ! arrangement.patterns[static_cast<size_t> (pattern)].playing);
        edited();
        return;
    }

    const int step = stepAt (e.position.x), row = rowAt (e.position.y);
    if (step < 0 || step >= arrangement.orderLength || row < 0 || row >= numRows)
        return;

    if (arrangement.order[static_cast<size_t> (step)] != patternOfRow (row))
        drag = Drag::paint;
    else
        drag = arrangement.isHeard (step) ? Drag::quiet : Drag::light;
    applyAt (e.position);
}

void OrderLane::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::length)
        setLengthFromX (e.position.x);
    else if (drag != Drag::none)
        applyAt (e.position);
}

void OrderLane::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
    repaint();
}

void OrderLane::applyAt (juce::Point<float> position)
{
    if (numRows == 0)
        return;
    const int step = juce::jlimit (0, arrangement.orderLength - 1, stepAt (position.x));
    const int pattern = patternOfRow (juce::jlimit (0, numRows - 1, rowAt (position.y)));

    // Fill in every step between the last position and this one, so fast drags leave no gaps.
    const int from = lastStep < 0 ? step : juce::jmin (lastStep, step);
    const int to = lastStep < 0 ? step : juce::jmax (lastStep, step);
    lastStep = step;

    bool changed = false;
    for (int i = from; i <= to; ++i)
    {
        const bool mine = arrangement.order[static_cast<size_t> (i)] == pattern;
        const bool heard = arrangement.isHeard (i);
        if (drag == Drag::paint && ! mine)
            arrangement.setTurn (i, pattern);
        else if (drag == Drag::quiet && mine && heard)
            arrangement.setQuiet (i, true);
        else if (drag == Drag::light && mine && ! heard)
            arrangement.setQuiet (i, false);
        else
            continue;
        changed = true;
    }

    if (changed)
        edited();
}

void OrderLane::setLengthFromX (float x)
{
    const int steps = juce::jlimit (1, maxOrderSteps, juce::roundToInt ((x - gridX) / pitch));
    auto from = dragStart;
    from.setOrderLength (steps);
    from.pauseFullyQuietPatterns(); // shortening can leave a pattern only quiet turns
    if (from == arrangement)
        return;

    arrangement = from;
    edited();
}

void OrderLane::edited()
{
    repaint();
    if (onEdit)
        onEdit (arrangement);
}

} // namespace patternarp
