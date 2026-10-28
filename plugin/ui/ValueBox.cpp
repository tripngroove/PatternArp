#include "ValueBox.h"

#include "Theme.h"

namespace patternarp
{

namespace
{
constexpr int captionGap = 4; // between the caption and its box
constexpr int arrowWidth = 18;
constexpr int pixelsPerStep = 8;

juce::Font captionFont() { return theme::font (15.0f); }
juce::Font valueFont() { return theme::font (16.0f); }
} // namespace

ValueBox::ValueBox (juce::String captionText) : caption (std::move (captionText))
{
    setName (caption);
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

int ValueBox::getPreferredWidth() const
{
    int widest = 0;
    for (int v = minValue; v <= maxValue; ++v)
        widest = juce::jmax (widest, theme::textWidth (valueFont(), toText (v)));
    return theme::textWidth (captionFont(), caption + ":") + captionGap + theme::padding + widest + arrowWidth + 2;
}

juce::Rectangle<int> ValueBox::boxArea() const
{
    return getLocalBounds().withTrimmedLeft (theme::textWidth (captionFont(), caption + ":") + captionGap).reduced (1);
}

juce::Rectangle<int> ValueBox::arrowArea() const
{
    return boxArea().removeFromRight (arrowWidth);
}

void ValueBox::paint (juce::Graphics& g)
{
    g.setColour (theme::ink);
    g.setFont (captionFont());
    g.drawText (caption + ":", getLocalBounds(), juce::Justification::centredLeft);

    const auto box = boxArea();
    g.setColour (isMouseOver (true) ? theme::hover : theme::panel);
    g.fillRect (box);
    g.setColour (theme::ink);
    g.drawRect (box, 1);

    // Two small chevrons, stacked: up above the middle, down below it.
    const auto arrows = arrowArea().toFloat();
    auto drawChevron = [&] (float centreY, bool up) {
        const float x = arrows.getCentreX(), dy = up ? -2.0f : 2.0f;
        juce::Path p;
        p.startNewSubPath (x - 4.0f, centreY - dy);
        p.lineTo (x, centreY + dy);
        p.lineTo (x + 4.0f, centreY - dy);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    g.setColour (value < maxValue ? theme::ink : theme::inkMuted.withAlpha (0.5f));
    drawChevron (arrows.getCentreY() - 4.5f, true);
    g.setColour (value > minValue ? theme::ink : theme::inkMuted.withAlpha (0.5f));
    drawChevron (arrows.getCentreY() + 4.5f, false);

    g.setColour (theme::ink);
    g.setFont (valueFont());
    g.drawText (toText (value), box.withTrimmedLeft (theme::padding).withTrimmedRight (arrowWidth), juce::Justification::centredLeft);
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
    if (! boxArea().contains (e.getMouseDownPosition()) || arrowArea().contains (e.getMouseDownPosition()))
        return;

    if (! draggingValue && std::abs (e.getDistanceFromDragStartY()) < 3)
        return;

    draggingValue = true;
    change (dragStartValue - e.getDistanceFromDragStartY() / pixelsPerStep);
}

void ValueBox::mouseUp (const juce::MouseEvent& e)
{
    if (! draggingValue && boxArea().contains (e.getMouseDownPosition()) && ! arrowArea().contains (e.getMouseDownPosition()))
        showList();
}

void ValueBox::showList()
{
    juce::PopupMenu menu;
    for (int v = minValue; v <= maxValue; ++v)
        menu.addItem (v - minValue + 1, toText (v), true, v == value);

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (this)
                            .withTargetScreenArea (localAreaToGlobal (boxArea()))
                            .withMinimumWidth (boxArea().getWidth())
                            .withItemThatMustBeVisible (value - minValue + 1),
                        [safe = juce::Component::SafePointer<ValueBox> (this)] (int chosen) {
                            if (safe != nullptr && chosen > 0)
                                safe->change (safe->minValue + chosen - 1);
                        });
}

} // namespace patternarp
