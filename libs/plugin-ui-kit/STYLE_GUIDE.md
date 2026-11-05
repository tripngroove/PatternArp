# tripngroove plugin style guide

How tripngroove plugins look and behave. The code in `tng_ui/` puts most of this into
practice; this page is the reasoning behind it, so a new screen or a new plugin can follow the
same rules without anyone re-explaining them. It grew out of PatternArp's design passes.

Names in `code` are in the kit: colours and sizes in `tng::theme` (`theme/Theme.h`), drawing
helpers in `theme/Drawing.h`, widgets in `widgets/`.

## The overall feel

- Light and flat: white panels (`panel`) with thin black outlines (`ink`) on a pale grey window
  (`background`). No gradients, shadows, glows or bevels.
- Crisp: every bar and 1px line sits on whole pixels so nothing blurs or fringes. Use
  `theme::pixel` for edges and `theme::pixelLine` for 1px lines. Edges of stacked elements line
  up exactly; check it pixel by pixel.
- Quiet chrome, loud content: the interface is black, white and greys; colour is reserved for
  the musical things the user made.

## Colour

- **Neutrals**: `background`, `panel`, `ink` (text, outlines), `inkMuted` (secondary text,
  hints), `gridLine`, `separator` (very light grey dividers), `fill` (neutral grey for things
  that belong to nothing, like the scrollbar and pressed generic buttons), `hover` (generic
  button hover), `danger` (delete, record, playhead).
- **Accents**: each musical thing the user creates (a pattern, a track, a voice) takes the next
  colour from `accentColour (index)`, in order.
- **Two shades per accent, nothing else.** The main colour is for what is on: playing, lit,
  filled, and every editable value. `tint (main)` (main at 45% on white) is for what is off:
  paused, quiet, unused, and hover highlights. No `darker()`, no other alphas of an accent.
- Inside something that has an accent, every highlight uses that accent (hover, selection,
  focus outline). Only things that belong to no accent use the neutral `fill`.
- Things that must never be mistaken for an accent (PatternArp's Variations, for example) use
  ink and light greys only.

## Text

- One font family at a few sizes via `theme::font (size, bold)`; measure with
  `theme::textWidth`.
- Section titles use `titleSize` (18), bold.
- No all-caps labels. Write "Rotation" and "+ Add pattern", not "ROTATION".
- **Editable values are bold, in their accent colour**, with no box around them ("Step: **1/16**",
  "Chance: **Rest**", "CC: **Off**"). Use this for every new editable value. `tng::ValueBox`
  does it for a caption plus a value picked from a list.
- A value that cannot be changed right now is bold but `inkMuted`, and says why ("follows A").
- Something that opens a list ends in a small caret (`drawCaret`). Something that folds open
  has a chevron (`drawChevron`).
- MIDI note names follow Bitwig, Ableton and Logic: note 60 is C3 (`middleCOctave`).

## Spacing and shape

- Horizontal spacing is tight and identical everywhere: `padding` (8) inside a box, button or
  tab either side of its text, and `spacing` (8) between neighbouring controls and at the
  window's edges.
- Vertical spacing sticks to the 8-pixel grid (8, 16, 24, 32...).
- One corner radius everywhere: `cornerRadius` (3), including buttons and text fields.
- Things fit their text: tabs, buttons and header rows are as wide as what they say plus even
  padding, not fixed widths.
- Outlined boxes (graphs, previews) have a 1px line, then `boxPadding` of white inside it.
  Nothing drawn inside, not bars, dotted lines, dividers or handles, touches the outline.
- Light grey vertical dividers (`separator`) split items in a row, with `spacing` either side
  of the text.

## Controls

- **Pointing hand** on everything clickable: buttons (the kit's `LookAndFeel` does this),
  closed tabs, values, keys, steps. Drag handles show a resize cursor instead.
- **Buttons** (`LookAndFeel`): white with a 1px ink outline and the corner radius; `hover` on
  hover, `fill` while pressed, half strength when disabled. Buttons that only matter on a
  panel can appear only while the mouse is over it.
- **Destructive actions** ask once more on the button itself ("Delete" becomes "Sure?" for a
  few seconds) rather than opening a dialog.
- **Play/pause** (`drawPlayButton`): no outline. Playing is the accent with white pause bars;
  paused is the accent's tint with a play triangle in the accent.
- **Randomize** (`tng::DiceButton`): a die in the accent with white pips, tint while pressed.
- **Length handles** (`drawLengthHandle`, `drawLengthHandleLabel`): a 1px ink line through what
  they shorten with a grip tab on it, and a label saying what the length means ("repeats every
  3 steps"), shown live while dragging.
- **Unused areas** keep their colours under a grey veil (`drawUnusedArea`), so it is clear what
  is kept but not playing.
- Drag inside values to step through them; click to pick from a list. Leave the scroll wheel
  to scrolling the editor.

## Tabs

- One flat strip, square corners, neighbours sharing one dividing line. No raised open tab.
- Open tab: white, ink line, bold text, and it opens into what it shows (the line between them
  is broken under it). Closed tabs: grey, lighter line and text, the accent's tint on hover.
  `tabLine`, `tabFill` and `tabText` give these colours.
- Tabs fit their text with even padding. No dragging inside tabs.
- A tab can carry a value ("Chance: **Rest** ▾") that opens a list or a typed field, and a small
  grey note under its name ("2 Steps") instead of badges.
- Vertical tabs (a source picker beside a keyboard): text left-aligned, a line down their right
  side that the open tab breaks, then a white gap before what they control, with no lines
  bridging it.
- Never let an open tab run into a neighbouring control (piano keys, for example).

## Where settings live

- Per-project defaults (default velocity, gate and so on) live in a settings menu opened by
  clicking the plugin's title (it has a caret). They only affect new or reset items.
- Right-click offers bulk actions on a lane or group ("Set all steps").

## Process

- UI-only passes get their own branch and pull request; bug fixes go with the work they belong
  to.
- When changing the kit, check every plugin that uses it still looks right, since a change here
  lands in all of them.
