// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "a3t/Model.h"

#include <juce_core/juce_core.h>

#include <array>

namespace a3t
{
    // keep: the original's packets are copied and the stems keep their rate and depth.
    // Otherwise all five tracks become FLAC at sampleRate / bitDepth.
    struct ConvertTarget
    {
        bool keep = true;
        int sampleRate = 44100;
        int bitDepth = 24;
        static ConvertTarget flac44k24() { return { false, 44100, 24 }; }
    };

    struct WriteRequest
    {
        juce::File original;
        std::array<juce::File, numStems> stems;
        A3tData data;
        ConvertTarget convert;
        juce::File cover;   // optional: JPEG or PNG
        juce::File output;
    };

    struct WriteResult
    {
        bool ok = false;
        juce::String error;
    };

    // Writes <output>.tmp, verifies it, then renames it over output; on any failure output is untouched.
    WriteResult writeTrack (const WriteRequest&);
}
