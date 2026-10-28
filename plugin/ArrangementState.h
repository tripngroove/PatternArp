#pragma once

#include "patternarp/Pattern.h"

#include <juce_data_structures/juce_data_structures.h>

namespace patternarp
{

// Saving and loading the arrangement as part of the plugin state.
juce::ValueTree toValueTree (const Arrangement& arrangement);

// Returns false (and leaves `arrangement` alone) if the tree is not a saved arrangement.
bool fromValueTree (const juce::ValueTree& tree, Arrangement& arrangement);

} // namespace patternarp
