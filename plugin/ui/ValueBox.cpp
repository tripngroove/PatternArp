#include "ValueBox.h"

#include "Theme.h"

namespace patternarp
{

namespace
{
constexpr int captionHeight = 22;
constexpr int arrowWidth = 22;
constexpr int pixelsPerStep = 8;
} // namespace

ValueBox::ValueBox (juce::String captionText) : caption (std::move (captionText))
{
    setRepaintsOnMouseActivity (true);
}

void ValueBox::setRange (int minimum, int maximum)
{
    minValue = minimum;
    maxValue = juce::jmax (minimum, maximum);
    value = juce::jlimit (minValue, maxValue, value);
    repaint();
}

void ValueBox::setValue (int newValue)
{
    newValue = juce::jlimit (minValue, maxValue, newValue);
    if (newValue != value)
    {
        value = newValue;
        repaint();
    }
}

void ValueBox::change (int newValue)
{
    newValue = juce::jlimit (minValue, maxValue, newValue);
    if (newValue == value)
        return;

    value = newValue;
    repaint();
    if (onChange)
        onChange (value);
}

juce::Rectangle<int> ValueBox::boxArea() const
{
    return getLocalBounds().withTrimmedTop (captionHeight).reduced (1);
}

juce::Rectangle<int> ValueBox::arrowArea() const
{
    return boxArea().removeFromRight (arrowWidth);
}

void ValueBox::paint (juce::Graphics& g)
{
    g.setColour (theme::ink);
    g.setFont (theme::font (16.0f));
    g.drawText (caption, getLocalBounds().removeFromTop (captionHeight), juce::Justification::bottomLeft);

    auto box = boxArea();
    g.setColour (isMouseOver (true) ? theme::hover : theme::panel);
    g.fillRect (box);
    g.setColour (theme::ink);
    g.drawRect (box, 1);

    auto arrows = arrowArea();
    g.drawVerticalLine (arrows.getX(), static_cast<float> (box.getY()), static_cast<float> (box.getBottom()));

    auto drawTriangle = [&] (juce::Rectangle<float> area, bool up) {
        auto r = area.withSizeKeepingCentre (10.0f, 8.0f);
        juce::Path p;
        if (up)
            p.addTriangle (r.getCentreX(), r.getY(), r.getRight(), r.getBottom(), r.getX(), r.getBottom());
        else
            p.addTriangle (r.getX(), r.getY(), r.getRight(), r.getY(), r.getCentreX(), r.getBottom());
        g.fillPath (p);
    };

    auto arrowsF = arrows.toFloat();
    g.setColour (value < maxValue ? theme::ink : theme::inkMuted);
    drawTriangle (arrowsF.removeFromTop (arrowsF.getHeight() / 2.0f), true);
    g.setColour (value > minValue ? theme::ink : theme::inkMuted);
    drawTriangle (arrowsF, false);

    auto textArea = box.withTrimmedRight (arrowWidth).reduced (10, 2);
    g.setColour (theme::ink);
    if (drawValue)
    {
        drawValue (g, textArea.toFloat(), value);
    }
    else
    {
        g.setFont (theme::font (18.0f));
        g.drawText (toText (value), textArea, juce::Justification::centredLeft);
    }
}

void ValueBox::mouseDown (const juce::MouseEvent& e)
{
    draggingValue = false;
    dragStartValue = value;

    const auto arrows = arrowArea();
    if (arrows.contains (e.getPosition()))
        change (value + (e.y < arrows.getCentreY() ? 1 : -1));
}

void ValueBox::mouseDrag (const juce::MouseEvent& e)
{
    if (arrowArea().contains (e.getMouseDownPosition()))
        return;

    if (! draggingValue && std::abs (e.getDistanceFromDragStartY()) < 3)
        return;

    draggingValue = true;
    change (dragStartValue - e.getDistanceFromDragStartY() / pixelsPerStep);
}

void ValueBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    wheelAccumulator += wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);
    const auto steps = static_cast<int> (wheelAccumulator / 0.12f);
    if (steps != 0)
    {
        wheelAccumulator -= static_cast<float> (steps) * 0.12f;
        change (value + steps);
    }
}

} // namespace patternarp
