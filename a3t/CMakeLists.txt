# SPDX-FileCopyrightText: 2026 Raphael Eismann
# JUCE's module code is compiled once, here: a juce:: target hands its module
# .cpp files to every target that links it publicly, so each consumer would
# carry its own copy. Distro JUCE builds also leave zlib to the system.
find_package(ZLIB REQUIRED)
add_library(a3t-juce STATIC)
target_link_libraries(a3t-juce PRIVATE juce::juce_core ZLIB::ZLIB)
target_compile_definitions(a3t-juce PUBLIC
    JUCE_STANDALONE_APPLICATION=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_compile_definitions(a3t-juce INTERFACE $<TARGET_PROPERTY:a3t-juce,COMPILE_DEFINITIONS>)
target_include_directories(a3t-juce INTERFACE $<TARGET_PROPERTY:a3t-juce,INCLUDE_DIRECTORIES>)
target_link_libraries(a3t-juce INTERFACE ZLIB::ZLIB)

add_library(a3t STATIC src/Model.cpp src/Events.cpp src/Json.cpp src/Sidecar.cpp src/Merge.cpp src/Derived.cpp src/Audio.cpp src/MkvMuxer.cpp src/TrackSource.cpp src/Writer.cpp)
target_include_directories(a3t PUBLIC include)
target_link_libraries(a3t PUBLIC a3t-juce PkgConfig::LIBAV)

add_executable(a3t-tests tests/Smoke.cpp tests/Events.cpp tests/Model.cpp tests/Json.cpp tests/Sidecar.cpp tests/Merge.cpp tests/Derived.cpp tests/Audio.cpp tests/Writer.cpp)
target_link_libraries(a3t-tests PRIVATE a3t GTest::gtest_main)
# The writer tests inspect the container through the internal libav helpers.
target_include_directories(a3t-tests PRIVATE src)
# The CLI tests (Task 10) run the built binary; building the tests builds it.
add_dependencies(a3t-tests a3t-cli)
target_compile_definitions(a3t-tests PRIVATE A3T_CLI_PATH="$<TARGET_FILE:a3t-cli>")
add_test(NAME a3t-tests COMMAND a3t-tests)
