#include "VariationStrip.h"

#include "Theme.h"

namespace patternarp
{

VariationStrip::VariationStrip()
{
    state.filled[0] = true;
}

void VariationStrip::setState (const State& newState)
{
    if (newState == state)
        return;
    state = newState;
    repaint();
}

void VariationStrip::setPulse (bool on)
{
    if (on == pulse)
        return;
    pulse = on;
    if (state.waiting >= 0)
        repaint (circleBounds (state.waiting));
}

juce::Rectangle<int> VariationStrip::circleBounds (int index) const
{
    const int y = (getHeight() - diameter) / 2;
    return { ring + index * (diameter + gap), y, diameter, diameter };
}

int VariationStrip::indexAt (juce::Point<int> position) const
{
    for (int i = 0; i < maxVariations; ++i)
        if (circleBounds (i).toFloat().getCentre().getDistanceFrom (position.toFloat()) <= diameter / 2.0f)
            return i;
    return -1;
}

void VariationStrip::paint (juce::Graphics& g)
{
    for (int i = 0; i < maxVariations; ++i)
    {
        const auto r = circleBounds (i).toFloat();
        const bool filled = state.filled[static_cast<size_t> (i)];
        const bool playing = i == state.active || (i == state.waiting && pulse);

        if (! filled)
        {
            // Empty: white with a light line, a grey number, and a plus under the mouse.
            if (i == state.requested && i != state.active)
            {
                g.setColour (theme::gridLine);
                g.drawEllipse (r.expanded (1.0f), 2.0f);
            }
            g.setColour (theme::panel);
            g.fillEllipse (r);
            g.setColour (theme::separator);
            g.drawEllipse (r.reduced (0.5f), 1.0f);

            if (i == hovered)
            {
                // A crisp plus: 10px arms, 2px thick, on whole pixels.
                const auto c = circleBounds (i).getCentre();
                g.setColour (theme::ink);
                g.fillRect (c.x - 5, c.y - 1, 10, 2);
                g.fillRect (c.x - 1, c.y - 5, 2, 10);
            }
            else
            {
                g.setColour (theme::inkMuted);
                g.setFont (theme::font (14.0f));
                g.drawText (juce::String (i + 1), r, juce::Justification::centred);
            }
            continue;
        }

        g.setColour (playing ? theme::variationOn : theme::variationOff);
        g.fillEllipse (r);
        g.setColour (playing ? juce::Colours::white : theme::ink);
        g.setFont (theme::font (14.0f, true));
        g.drawText (juce::String (i + 1), r, juce::Justification::centred);
    }
}

void VariationStrip::setHovered (int index)
{
    if (index == hovered)
        return;
    hovered = index;
    setMouseCursor (index >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void VariationStrip::mouseMove (const juce::MouseEvent& e)
{
    setHovered (indexAt (e.getPosition()));
}

void VariationStrip::mouseExit (const juce::MouseEvent&)
{
    setHovered (-1);
}

void VariationStrip::mouseDown (const juce::MouseEvent& e)
{
    const int index = indexAt (e.getPosition());
    if (index < 0)
        return;

    if (e.mods.isPopupMenu())
        showMenu (index);
    else if (onSelect)
        onSelect (index);
}

void VariationStrip::showMenu (int index)
{
    const auto name = [] (int i) { return "Variation " + juce::String (i + 1); };
    const bool filled = state.filled[static_cast<size_t> (index)];
    juce::PopupMenu menu;

    if (filled)
    {
        // Copy to any other; the hint says whether that replaces something.
        menu.addSectionHeader ("Copy " + name (index) + " to");
        bool anyEmpty = false;
        for (int i = 0; i < maxVariations; ++i)
        {
            if (i == index)
                continue;
            const bool targetFilled = state.filled[static_cast<size_t> (i)];
            anyEmpty = anyEmpty || ! targetFilled;
            juce::PopupMenu::Item item (name (i));
            item.shortcutKeyDescription = targetFilled ? "replace" : "empty";
            item.action = [this, index, i] {
                if (onCopy)
                    onCopy (index, i);
            };
            menu.addItem (std::move (item));
        }
        menu.addSeparator();
        menu.addItem ("Copy to all empty", anyEmpty, false, [this, index] {
            if (onCopyToEmpty)
                onCopyToEmpty (index);
        });

        // The one playing can't be cleared.
        juce::PopupMenu::Item clear ("Clear");
        clear.isEnabled = index != state.active;
        if (! clear.isEnabled)
            clear.shortcutKeyDescription = "playing";
        clear.action = [this, index] {
            if (onClear)
                onClear (index);
        };
        menu.addItem (std::move (clear));
    }
    else
    {
        // Fill this one from another without switching to it.
        menu.addSectionHeader ("Fill " + name (index) + " from");
        for (int i = 0; i < maxVariations; ++i)
            if (state.filled[static_cast<size_t> (i)])
                menu.addItem (name (i), [this, i, index] {
                    if (onCopy)
                        onCopy (i, index);
                });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (circleBounds (index))));
}

} // namespace patternarp
