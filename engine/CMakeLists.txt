add_library(patternarp_engine STATIC
    src/Euclid.cpp)

target_include_directories(patternarp_engine PUBLIC include)
target_compile_features(patternarp_engine PUBLIC cxx_std_20)

# Plugins are loaded into a host process, so the engine must be position independent.
set_target_properties(patternarp_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
