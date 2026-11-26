#!/bin/bash
# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
# test.sh — build the unit-test runner, then run it; prints when it was built,
# so a green claim can be checked (ctest runs, it does not build).
#   ./test.sh [gtest args]
# JUCE is found via CMAKE_PREFIX_PATH (default ~/local/juce); JOBS sets build parallelism.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${A3T_BUILD:-$HERE/build}"
RUNNER="$BUILD/a3t/a3t-tests"

cmake -S "$HERE" -B "$BUILD" \
    -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-$HOME/local/juce}"
cmake --build "$BUILD" --target a3t-tests -j"${JOBS:-2}"
echo "=== a3t-tests built $(date -r "$RUNNER" '+%F %T') ==="
"$RUNNER" "$@"
