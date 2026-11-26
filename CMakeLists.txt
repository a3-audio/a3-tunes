# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.22)
project(a3-tunes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(JUCE CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
# libswresample: the 44.1 kHz / 24 bit conversion (same FFmpeg source package).
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
find_package(GTest REQUIRED)

add_subdirectory(a3t)

add_executable(a3t-cli cli/main.cpp)
# bin/ because build/a3t is the library's directory
set_target_properties(a3t-cli PROPERTIES OUTPUT_NAME a3t
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_link_libraries(a3t-cli PRIVATE a3t)

enable_testing()
