# tripngroove plugin UI kit

The shared look of tripngroove's audio plugins, so they all look and behave alike and a style
fix lands in every plugin at once. It is a [JUCE](https://juce.com) module named `tng_ui`
(tng = tripngroove).

- [STYLE_GUIDE.md](STYLE_GUIDE.md): the visual rules and why. Read this first.
- `tng_ui/theme/Theme.h`: colours, the accent palette, fonts, spacing, corner radius.
- `tng_ui/theme/Drawing.h`: whole-pixel helpers, carets, chevrons, tab colours, length handles,
  play buttons, unused-area veils.
- `tng_ui/theme/LookAndFeel.h`: buttons, text fields and scrollbars in the kit's style.
- `tng_ui/widgets/`: small reusable controls (`ValueBox`, `DiceButton`).

Only things any plugin could use belong here. Anything tied to one plugin's data stays in that
plugin and draws with the kit's colours and helpers.

## Using it in a plugin

Add it as a git submodule:

```sh
git submodule add https://github.com/tripngroove/plugin-ui-kit.git libs/plugin-ui-kit
```

In CMake, after JUCE is available:

```cmake
add_subdirectory(libs/plugin-ui-kit)
target_link_libraries(MyPlugin PRIVATE tng::tng_ui)
```

In code:

```cpp
#include <tng_ui/tng_ui.h>

// in the editor
tng::theme::LookAndFeel lookAndFeel; // and setLookAndFeel (&lookAndFeel) in the constructor
g.fillAll (tng::theme::background);
```

In CI, check out submodules (`actions/checkout` with `submodules: true`).

## Updating

Change the kit here, then in each plugin move the submodule to the new commit
(`git -C libs/plugin-ui-kit pull` and commit the change). Plugins only pick up kit changes
when they do this, so one plugin can't be broken by a kit change it hasn't taken yet.
