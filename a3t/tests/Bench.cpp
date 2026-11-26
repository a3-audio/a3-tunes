// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Bench.h"

#include <gtest/gtest.h>

using a3t::summarise;

TEST (Bench, MedianOfAnOddCount)
{
    const auto s = summarise ({ 9.0, 1.0, 5.0 });
    EXPECT_DOUBLE_EQ (s.median, 5.0);
    EXPECT_DOUBLE_EQ (s.max, 9.0);
}

TEST (Bench, MedianOfAnEvenCountIsTheMeanOfTheMiddlePair)
{
    EXPECT_DOUBLE_EQ (summarise ({ 4.0, 1.0, 3.0, 2.0 }).median, 2.5);
}

TEST (Bench, P95OfOneToHundredIsNinetyFive)
{
    std::vector<double> v;
    for (int i = 100; i >= 1; --i)
        v.push_back (i);
    const auto s = summarise (v);
    EXPECT_DOUBLE_EQ (s.p95, 95.0);
    EXPECT_DOUBLE_EQ (s.max, 100.0);
}

TEST (Bench, ASingleValueIsEverything)
{
    const auto s = summarise ({ 7.0 });
    EXPECT_DOUBLE_EQ (s.median, 7.0);
    EXPECT_DOUBLE_EQ (s.p95, 7.0);
    EXPECT_DOUBLE_EQ (s.max, 7.0);
}

TEST (Bench, EmptyInputGivesZeros)
{
    const auto s = summarise ({});
    EXPECT_DOUBLE_EQ (s.median, 0.0);
    EXPECT_DOUBLE_EQ (s.p95, 0.0);
    EXPECT_DOUBLE_EQ (s.max, 0.0);
}

// The 95th percentile is the value at rank ceil(0.95 n), also when 0.95 n is not whole.
TEST (Bench, P95OfANonMultipleCountIsTheCeilingRank)
{
    const auto oneTo = [] (int n)
    {
        std::vector<double> v;
        for (int i = n; i >= 1; --i)
            v.push_back (i);
        return summarise (v);
    };
    EXPECT_DOUBLE_EQ (oneTo (10).p95, 10.0);
    EXPECT_DOUBLE_EQ (oneTo (21).p95, 20.0);
}
