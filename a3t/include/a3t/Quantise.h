// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

namespace a3t
{
    // Every double in a3t.json and in the events is rounded to 1e-9, on write
    // and on read, so a text round trip is exact (JUCE's double text is not).
    // Beyond ~1.8e299, x * 1e9 overflows; such a value is kept as it is (it has
    // no sub-nanosecond part anyway), since JUCE would write inf as null.
    inline double quantised (double x)
    {
        if (! std::isfinite (x))
            return x;
        const double r = std::round (x * 1e9) / 1e9;
        return std::isfinite (r) ? r : x;
    }
}
