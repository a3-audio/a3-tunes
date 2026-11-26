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

    // Writes a unique temporary beside output, verifies it, then renames it over output; on any failure
    // output is untouched and the temporary is gone.
    WriteResult writeTrack (const WriteRequest&);

    // Replaces the a3t.json attachment (and the tags and chapters derived from it) of an existing .a3mkv.
    // All five audio tracks and a cover attachment are copied as packets, bit for bit; the file is written to a
    // unique temporary beside it, verified as in writeTrack, and renamed over the original.
    // Refused, with the file untouched, unless it opens as status ok with five tracks and data.uuid is its uuid.
    // data.track.audio is the file's (taken from it, not from data); data.revision is stored as given.
    WriteResult rewriteMetadata (const juce::File& a3mkv, const A3tData& data);
}
