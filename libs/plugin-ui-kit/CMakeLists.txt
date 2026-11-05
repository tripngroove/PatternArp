# tripngroove plugin UI kit, as a JUCE module.
#
# In a plugin's CMakeLists.txt, after JUCE is available:
#
#   add_subdirectory(libs/plugin-ui-kit)
#   target_link_libraries(MyPlugin PRIVATE tng::tng_ui)
#
# then #include <tng_ui/tng_ui.h>. Each plugin target compiles its own copy, like JUCE's modules.

juce_add_module(${CMAKE_CURRENT_LIST_DIR}/tng_ui ALIAS_NAMESPACE tng)
target_link_libraries(tng_ui INTERFACE juce::juce_gui_basics)
