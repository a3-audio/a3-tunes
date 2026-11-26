// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vector>

namespace a3t
{
struct SeekStats
{
    double median = 0.0, p95 = 0.0, max = 0.0;
};

// Median (mean of the two middle values for an even count), p95 by nearest rank, and the maximum.
// Empty input gives zeros.
SeekStats summarise (std::vector<double> milliseconds);
}
