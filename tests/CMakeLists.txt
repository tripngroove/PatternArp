FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.9.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(engine_tests
    EuclidTests.cpp)

target_link_libraries(engine_tests PRIVATE patternarp_engine Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(engine_tests)
