// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

namespace a3t
{
    // Every double in a3t.json and in the events is rounded to 1e-9, on write
    // and on read, so a text round trip is exact (JUCE's double text is not).
    inline double quantised (double x)
    {
        return std::isfinite (x) ? std::round (x * 1e9) / 1e9 : x;
    }
}
