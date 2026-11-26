// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Bench.h"

#include <algorithm>
#include <cmath>

namespace a3t
{
SeekStats summarise (std::vector<double> milliseconds)
{
    if (milliseconds.empty())
        return {};
    std::sort (milliseconds.begin(), milliseconds.end());
    const auto n = milliseconds.size();
    const auto median = n % 2 == 1 ? milliseconds[n / 2] : (milliseconds[n / 2 - 1] + milliseconds[n / 2]) / 2.0;
    const auto rank = (size_t) std::ceil (0.95 * (double) n);
    return { median, milliseconds[rank - 1], milliseconds.back() };
}
}
