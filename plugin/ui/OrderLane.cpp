#include "OrderLane.h"

#include "Theme.h"

namespace patternarp
{

namespace
{
constexpr float labelWidth = 116.0f; // "ROTATION" and, when open, "Pattern A"...
constexpr float rightMargin = 12.0f;
constexpr float headerHeight = static_cast<float> (OrderLane::collapsedHeight); // the label stays put when opened
constexpr float handleStrip = 16.0f;
constexpr float stepHeight = 24.0f; // a step's box, the same open or closed
constexpr float rowHeight = stepHeight + 4.0f;
constexpr float bottomMargin = 10.0f;
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
    return juce::roundToInt (headerHeight + handleStrip + rows * rowHeight + bottomMargin);
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

juce::Rectangle<float> OrderLane::handleArea() const
{
    const auto grid = gridArea();
    return { grid.getX() - 8.0f, headerHeight, grid.getWidth() + 16.0f, handleStrip };
}

float OrderLane::stepWidth() const
{
    return juce::jmin (maxStepWidth, (static_cast<float> (getWidth()) - labelWidth - rightMargin) / static_cast<float> (maxOrderSteps));
}

juce::Rectangle<float> OrderLane::gridArea() const
{
    const float width = stepWidth() * static_cast<float> (maxOrderSteps);
    if (! expanded)
        return { labelWidth, (headerHeight - stepHeight) / 2.0f, width, stepHeight };

    const auto rows = static_cast<float> (juce::jmax (1, arrangement.numPatterns));
    return { labelWidth, headerHeight + handleStrip, width, rows * rowHeight };
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
    g.setFont (theme::font (14.0f, true));
    const auto label = juce::Rectangle<float> (12.0f, 0.0f, labelWidth - 12.0f, expanded ? headerHeight : static_cast<float> (getHeight()));
    g.drawText ("ROTATION", label, juce::Justification::centredLeft);
    const float textWidth = static_cast<float> (juce::GlyphArrangement::getStringWidthInt (theme::font (14.0f, true), "ROTATION"));
    g.setColour (hover ? theme::ink : theme::inkMuted);
    drawChevron (g, { label.getX() + textWidth + 12.0f, label.getCentreY() }, expanded);

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

void OrderLane::paintStep (juce::Graphics& g, juce::Rectangle<float> r, int owner) const
{
    if (owner < 0 || owner >= arrangement.numPatterns)
    {
        // A rest: an empty outline.
        g.setColour (theme::inkMuted);
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        return;
    }

    const auto& p = arrangement.patterns[static_cast<size_t> (owner)];
    const float alpha = p.playing ? 1.0f : 0.35f; // a paused pattern's steps are skipped
    g.setColour (theme::patternColour (p.colour).withAlpha (alpha));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (alpha));
    g.setFont (theme::font (13.0f, true));
    g.drawText (theme::patternName (p), r, juce::Justification::centred);
}

void OrderLane::paintCollapsed (juce::Graphics& g)
{
    const auto y = gridArea().getY();
    for (int i = 0; i < arrangement.orderLength; ++i)
        paintStep (g, stepBox (i, y), arrangement.order[static_cast<size_t> (i)]);
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
        g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, top + (rowHeight - 10.0f) / 2.0f, 10.0f, 10.0f), 2.0f);
        g.setColour (theme::ink.withAlpha (alpha));
        g.setFont (theme::font (14.0f, true));
        g.drawText ("Pattern " + theme::patternName (p), juce::Rectangle<float> (30.0f, top, labelWidth - 34.0f, rowHeight),
                    juce::Justification::centredLeft);

        for (int i = 0; i < arrangement.orderLength; ++i)
            if (arrangement.order[static_cast<size_t> (i)] == row)
                paintStep (g, stepBox (i, top + (rowHeight - stepHeight) / 2.0f), row);
    }

    // Grid lines.
    g.setColour (theme::gridLine.withAlpha (0.35f));
    for (int i = 1; i < maxOrderSteps; ++i)
        g.drawVerticalLine (juce::roundToInt (grid.getX() + w * static_cast<float> (i)), grid.getY(), grid.getBottom());
    for (int row = 1; row < arrangement.numPatterns; ++row)
        g.drawHorizontalLine (juce::roundToInt (grid.getY() + rowHeight * static_cast<float> (row)), grid.getX(), grid.getRight());
    g.setColour (theme::ink);
    g.drawRect (grid, 1.0f);

    paintStrip (g);
}

void OrderLane::paintStrip (juce::Graphics& g)
{
    // Length handle: a tab above the grid and a line down through it, like the lane graphs.
    const auto grid = gridArea();
    const float hx = grid.getX() + stepWidth() * static_cast<float> (arrangement.orderLength);
    const bool active = drag == Drag::length || (isMouseOver() && handleArea().contains (getMouseXYRelative().toFloat()));

    const auto tab = juce::Rectangle<float> (hx - 6.0f, headerHeight + 1.0f, 12.0f, handleStrip - 2.0f);
    theme::drawLengthHandle (g, tab, grid.getBottom());

    if (active)
    {
        const int steps = arrangement.orderLength;
        const auto text = juce::String (steps) + (steps == 1 ? " step" : " steps");
        const float w = static_cast<float> (juce::GlyphArrangement::getStringWidthInt (theme::font (12.0f), text)) + 4.0f;
        const bool right = tab.getRight() + 8.0f + w < grid.getRight();
        g.setColour (theme::ink);
        g.setFont (theme::font (12.0f));
        g.drawText (text, right ? juce::Rectangle<float> (tab.getRight() + 8.0f, headerHeight, w, handleStrip)
                                : juce::Rectangle<float> (tab.getX() - 8.0f - w, headerHeight, w, handleStrip),
                    right ? juce::Justification::centredLeft : juce::Justification::centredRight);
    }
}

void OrderLane::mouseMove (const juce::MouseEvent& e)
{
    auto shape = juce::MouseCursor::NormalCursor;
    if (headerArea().contains (e.position))
        shape = juce::MouseCursor::PointingHandCursor;
    else if (handleArea().contains (e.position))
        shape = juce::MouseCursor::LeftRightResizeCursor;
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

    if (headerArea().contains (e.position))
    {
        setExpanded (! expanded);
        return;
    }

    if (handleArea().contains (e.position))
    {
        drag = Drag::length;
        dragStart = arrangement;
        setLengthFromX (e.position.x);
        return;
    }

    const int step = stepAt (e.position.x), row = rowAt (e.position.y);
    if (step < 0 || step >= arrangement.orderLength || row < 0 || row >= arrangement.numPatterns)
        return;

    drag = arrangement.order[static_cast<size_t> (step)] == row ? Drag::clear : Drag::paint;
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
        auto& owner = arrangement.order[static_cast<size_t> (i)];
        if (drag == Drag::paint && owner != row)
        {
            owner = row;
            changed = true;
        }
        else if (drag == Drag::clear && owner == row)
        {
            owner = -1;
            changed = true;
        }
    }

    if (changed)
        edited();
}

void OrderLane::setLengthFromX (float x)
{
    const int steps = juce::jlimit (1, maxOrderSteps, juce::roundToInt ((x - gridArea().getX()) / stepWidth()));
    auto from = dragStart;
    from.setOrderLength (steps);
    if (from.orderLength == arrangement.orderLength && from.order == arrangement.order)
        return;

    arrangement.order = from.order;
    arrangement.orderLength = from.orderLength;
    edited();
}

void OrderLane::edited()
{
    repaint();
    if (onEdit)
        onEdit (arrangement);
}

} // namespace patternarp
