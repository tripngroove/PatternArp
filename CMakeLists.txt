cmake_minimum_required(VERSION 3.28)

project(PatternArp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Mac builds are universal (Apple Silicon + Intel) and run on macOS 11+.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

option(PATTERNARP_BUILD_PLUGIN "Build the plugin (needs JUCE)" ON)
option(PATTERNARP_BUILD_TESTS "Build the engine tests" ON)

include(FetchContent)

# The musical engine: plain C++, no JUCE, so it can be tested on its own.
add_subdirectory(engine)

if(PATTERNARP_BUILD_PLUGIN)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    # Unofficial CLAP support for JUCE (JUCE has no native CLAP yet).
    FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d
        GIT_SUBMODULES_RECURSE TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(clap-juce-extensions)

    add_subdirectory(plugin)
endif()

if(PATTERNARP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
