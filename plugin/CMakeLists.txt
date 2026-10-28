# PatternArp ships as two plugins built from the same source:
#
#   PatternArp          VST3 + CLAP. Loads as an instrument that outputs MIDI, which is how
#                       VST3 hosts accept note generators. In Bitwig it is a note effect.
#   PatternArp MIDI FX  AU (macOS only). A true MIDI effect for Logic's MIDI FX slot.

set(PATTERNARP_SOURCES
    ArrangementState.cpp
    ArrangementState.h
    PluginEditor.cpp
    PluginEditor.h
    PluginProcessor.cpp
    PluginProcessor.h
    ui/LaneGraph.cpp
    ui/LaneGraph.h
    ui/NoteKeyboard.cpp
    ui/NoteKeyboard.h
    ui/OrderLane.cpp
    ui/OrderLane.h
    ui/PatternPanel.cpp
    ui/PatternPanel.h
    ui/PreviewComponent.cpp
    ui/PreviewComponent.h
    ui/Theme.h
    ui/ValueBox.cpp
    ui/ValueBox.h)

function(patternarp_configure_target target)
    target_sources(${target} PRIVATE ${PATTERNARP_SOURCES})

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target}
        PRIVATE
            patternarp_engine
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

juce_add_plugin(PatternArp
    PRODUCT_NAME "PatternArp"
    COMPANY_NAME "tripngroove"
    BUNDLE_ID "com.tripngroove.patternarp"
    PLUGIN_MANUFACTURER_CODE Trpg
    PLUGIN_CODE Patr
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Generator
    COPY_PLUGIN_AFTER_BUILD FALSE)

patternarp_configure_target(PatternArp)

clap_juce_extensions_plugin(TARGET PatternArp
    CLAP_ID "com.tripngroove.patternarp"
    CLAP_FEATURES note-effect utility)

if(APPLE)
    juce_add_plugin(PatternArpMidiFX
        PRODUCT_NAME "PatternArp MIDI FX"
        COMPANY_NAME "tripngroove"
        BUNDLE_ID "com.tripngroove.patternarp.midifx"
        PLUGIN_MANUFACTURER_CODE Trpg
        PLUGIN_CODE Patm
        FORMATS AU
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
        COPY_PLUGIN_AFTER_BUILD FALSE)

    patternarp_configure_target(PatternArpMidiFX)
endif()
