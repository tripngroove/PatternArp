#include "ValueBox.h"

#include "Theme.h"

namespace patternarp
{

namespace
{
constexpr int captionGap = 4; // between the caption and the value's hover backing
constexpr int pixelsPerStep = 8;

juce::Font captionFont() { return theme::font (15.0f); }
juce::Font valueFont() { return theme::font (15.0f, true); }
} // namespace

ValueBox::ValueBox (juce::String captionText) : caption (std::move (captionText)), accent (theme::ink)
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

void ValueBox::setAccent (juce::Colour colour)
{
    if (colour != accent)
    {
        accent = colour;
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

void ValueBox::setFixedText (const juce::String& text)
{
    if (text == fixedText)
        return;
    fixedText = text;
    setInterceptsMouseClicks (fixedText.isEmpty(), false);
    repaint();
}

juce::String ValueBox::shownText() const
{
    return fixedText.isNotEmpty() ? fixedText : toText (value);
}

int ValueBox::captionWidth() const
{
    return theme::textWidth (captionFont(), caption + ":");
}

int ValueBox::getPreferredWidth() const
{
    return captionWidth() + captionGap + valueInset * 2 + theme::textWidth (valueFont(), shownText());
}

juce::Rectangle<int> ValueBox::valueArea() const
{
    const int width = theme::textWidth (valueFont(), shownText()) + valueInset * 2;
    return getLocalBounds().withTrimmedLeft (captionWidth() + captionGap).withWidth (width).withSizeKeepingCentre (width, 24);
}

void ValueBox::paint (juce::Graphics& g)
{
    g.setColour (theme::ink);
    g.setFont (captionFont());
    g.drawText (caption + ":", getLocalBounds(), juce::Justification::centredLeft);

    const auto area = valueArea();
    if (fixedText.isNotEmpty())
    {
        // Bold like a value, but muted: it says where the value comes from.
        g.setColour (theme::inkMuted);
        g.setFont (valueFont());
        g.drawText (fixedText, area.reduced (valueInset, 0), juce::Justification::centredLeft);
        return;
    }

    if (isMouseOver() && area.contains (getMouseXYRelative()))
    {
        g.setColour (theme::tint (accent));
        g.fillRoundedRectangle (area.toFloat(), theme::cornerRadius);
    }

    g.setColour (accent);
    g.setFont (valueFont());
    g.drawText (toText (value), area.reduced (valueInset, 0), juce::Justification::centredLeft);
}

void ValueBox::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (valueArea().contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void ValueBox::mouseDown (const juce::MouseEvent&)
{
    draggingValue = false;
    dragStartValue = value;
}

void ValueBox::mouseDrag (const juce::MouseEvent& e)
{
    if (! valueArea().contains (e.getMouseDownPosition()))
        return;

    if (! draggingValue && std::abs (e.getDistanceFromDragStartY()) < 3)
        return;

    draggingValue = true;
    change (dragStartValue - e.getDistanceFromDragStartY() / pixelsPerStep);
}

void ValueBox::mouseUp (const juce::MouseEvent& e)
{
    if (! draggingValue && valueArea().contains (e.getMouseDownPosition()))
        showList();
}

void ValueBox::showList()
{
    juce::PopupMenu menu;
    for (int v = minValue; v <= maxValue; ++v)
        menu.addItem (v - minValue + 1, toText (v), true, v == value);

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (this)
                            .withTargetScreenArea (localAreaToGlobal (valueArea()))
                            .withItemThatMustBeVisible (value - minValue + 1),
                        [safe = juce::Component::SafePointer<ValueBox> (this)] (int chosen) {
                            if (safe != nullptr && chosen > 0)
                                safe->change (safe->minValue + chosen - 1);
                        });
}

} // namespace patternarp
