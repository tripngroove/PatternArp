/*******************************************************************************
 The block below is read by JUCE's CMake (juce_add_module) and the Projucer.

 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 tng_ui
  vendor:             tripngroove
  version:            1.0.0
  name:               tripngroove plugin UI kit
  description:        The shared look of tripngroove plugins: colours, spacing, fonts,
                      drawing helpers, a LookAndFeel and small reusable widgets.
  dependencies:       juce_gui_basics
  minimumCppStandard: 20

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Everything lives in namespace tng ("tripngroove"); the look itself in tng::theme.
// STYLE_GUIDE.md beside this module says how it is meant to be used.

#include "theme/Theme.h"
#include "theme/Drawing.h"
#include "theme/LookAndFeel.h"

#include "widgets/DiceButton.h"
#include "widgets/ValueBox.h"
