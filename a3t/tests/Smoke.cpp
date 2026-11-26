// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

extern "C" {
#include <libavformat/avformat.h>
}

TEST (Smoke, JuceStringAndFfmpegLink)
{
    EXPECT_EQ (juce::String ("a3t").length(), 3);
    EXPECT_GT (avformat_version(), 0u);
}
