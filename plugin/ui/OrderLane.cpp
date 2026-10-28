#include "OrderLane.h"

#include "Theme.h"

namespace patternarp
{

namespace
{
constexpr float gridX = static_cast<float> (OrderLane::labelWidth); // where the steps start, after the labels
constexpr float labelX = static_cast<float> (theme::padding);
constexpr float rightMargin = 12.0f;
constexpr float headerHeight = static_cast<float> (OrderLane::collapsedHeight); // the label stays put when opened
constexpr float stepHeight = 24.0f; // a step's box, the same open or closed
constexpr float rowHeight = stepHeight + 4.0f;
constexpr float gridMargin = 8.0f; // above and below the open grid, as around the closed row
constexpr float maxStepWidth = 40.0f;

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
    const bool rowsChanged = a.numPatterns != arrangement.numPatterns;
    arrangement = a;
    repaint();

    if (rowsChanged && expanded && onHeightChange)
        onHeightChange();
}

int OrderLane::getPreferredHeight() const
{
    if (! expanded)
        return collapsedHeight;

    const auto rows = static_cast<float> (juce::jmax (1, arrangement.numPatterns));
    return juce::roundToInt (headerHeight + gridMargin + rows * rowHeight + gridMargin);
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
    return gridArea().getX() + stepWidth() * static_cast<float> (arrangement.orderLength);
}

bool OrderLane::isOnHandle (juce::Point<float> p) const
{
    return expanded && theme::lengthHandleTab (handleX(), gridArea().getVerticalRange()).expanded (4.0f).contains (p);
}

float OrderLane::stepWidth() const
{
    return juce::jmin (maxStepWidth, (static_cast<float> (getWidth()) - gridX - rightMargin) / static_cast<float> (maxOrderSteps));
}

juce::Rectangle<float> OrderLane::gridArea() const
{
    const float width = stepWidth() * static_cast<float> (maxOrderSteps);
    if (! expanded)
        return { gridX, (headerHeight - stepHeight) / 2.0f, width, stepHeight };

    const auto rows = static_cast<float> (juce::jmax (1, arrangement.numPatterns));
    return { gridX, headerHeight + gridMargin, width, rows * rowHeight };
}

int OrderLane::stepAt (float x) const
{
    return static_cast<int> (std::floor ((x - gridArea().getX()) / stepWidth()));
}

int OrderLane::rowAt (float y) const
{
    return static_cast<int> (std::floor ((y - gridArea().getY()) / rowHeight));
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

juce::Rectangle<float> OrderLane::stepBox (int step, float y) const
{
    const float w = stepWidth();
    return juce::Rectangle<float> (gridArea().getX() + w * static_cast<float> (step), y, w, stepHeight).reduced (1.5f, 0.0f);
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

    // Lit: filled in the pattern's colour. Quiet (or paused): a pale outline of it.
    const auto& p = arrangement.patterns[static_cast<size_t> (owner)];
    const auto colour = theme::patternColour (p.colour);
    g.setFont (theme::font (13.0f, true));
    if (arrangement.isHeard (turn))
    {
        g.setColour (colour);
        g.fillRoundedRectangle (r, theme::cornerRadius);
        g.setColour (juce::Colours::white);
    }
    else
    {
        g.setColour (colour.withAlpha (0.12f));
        g.fillRoundedRectangle (r, theme::cornerRadius);
        g.setColour (colour.withAlpha (0.5f));
        g.drawRoundedRectangle (r.reduced (0.5f), theme::cornerRadius, 1.0f);
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
    const float w = stepWidth();

    g.setColour (theme::ink.withAlpha (0.25f));
    g.drawHorizontalLine (juce::roundToInt (headerHeight), 1.0f, boxArea().getRight() - 1.0f);

    // Steps past the end of the lane are greyed out.
    const float end = grid.getX() + w * static_cast<float> (arrangement.orderLength);
    g.setColour (theme::background);
    g.fillRect (grid.withLeft (end));

    // A row per pattern, labelled, with its steps drawn just as they are when the lane is closed.
    for (int row = 0; row < arrangement.numPatterns; ++row)
    {
        const auto& p = arrangement.patterns[static_cast<size_t> (row)];
        const float top = grid.getY() + rowHeight * static_cast<float> (row);
        const float alpha = p.playing ? 1.0f : 0.45f;

        g.setColour (theme::patternColour (p.colour).withAlpha (alpha));
        g.fillRoundedRectangle (juce::Rectangle<float> (labelX, top + (rowHeight - 10.0f) / 2.0f, 10.0f, 10.0f), theme::cornerRadius);
        g.setColour (theme::ink.withAlpha (alpha));
        g.setFont (theme::font (14.0f, true));
        g.drawText ("Pattern " + theme::patternName (p), juce::Rectangle<float> (labelX + 16.0f, top, gridX - labelX - 16.0f, rowHeight),
                    juce::Justification::centredLeft);

        for (int i = 0; i < arrangement.orderLength; ++i)
            if (arrangement.order[static_cast<size_t> (i)] == row)
                paintStep (g, stepBox (i, top + (rowHeight - stepHeight) / 2.0f), i);
    }

    // Grid lines.
    g.setColour (theme::gridLine.withAlpha (0.35f));
    for (int i = 1; i < maxOrderSteps; ++i)
        g.drawVerticalLine (juce::roundToInt (grid.getX() + w * static_cast<float> (i)), grid.getY(), grid.getBottom());
    for (int row = 1; row < arrangement.numPatterns; ++row)
        g.drawHorizontalLine (juce::roundToInt (grid.getY() + rowHeight * static_cast<float> (row)), grid.getX(), grid.getRight());
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
    else if (headerArea().contains (e.position))
        shape = juce::MouseCursor::PointingHandCursor;
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

    const int step = stepAt (e.position.x), row = rowAt (e.position.y);
    if (step < 0 || step >= arrangement.orderLength || row < 0 || row >= arrangement.numPatterns)
        return;

    if (arrangement.order[static_cast<size_t> (step)] != row)
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
    const int step = juce::jlimit (0, arrangement.orderLength - 1, stepAt (position.x));
    const int row = juce::jlimit (0, arrangement.numPatterns - 1, rowAt (position.y));

    // Fill in every step between the last position and this one, so fast drags leave no gaps.
    const int from = lastStep < 0 ? step : juce::jmin (lastStep, step);
    const int to = lastStep < 0 ? step : juce::jmax (lastStep, step);
    lastStep = step;

    bool changed = false;
    for (int i = from; i <= to; ++i)
    {
        const bool mine = arrangement.order[static_cast<size_t> (i)] == row;
        const bool heard = arrangement.isHeard (i);
        if (drag == Drag::paint && ! mine)
            arrangement.setTurn (i, row);
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
    const int steps = juce::jlimit (1, maxOrderSteps, juce::roundToInt ((x - gridArea().getX()) / stepWidth()));
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
