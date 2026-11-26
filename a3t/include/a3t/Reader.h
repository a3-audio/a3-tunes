// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "a3t/Json.h"

namespace a3t
{
    // 1 original + 4 stems is the five-track layout; any other file keeps what it has.
    struct TrackLayout
    {
        int audioTracks = 0;
        int sampleRate = 0;           // of the first audio track
        juce::int64 lengthFrames = 0; // of the first audio track, exact
        bool isFiveTrack = false;
    };

    struct OpenResult
    {
        Status status = Status::none;
        A3tData data;
        TrackLayout layout;
        juce::String error;
        juce::File appliedSidecar;   // openTrackWithSidecar: the sidecar overlaid on data; none if no file
    };

    // ok: a3t.json read. damaged: the file has audio but a3t.json is missing or unparsable (the layout
    // is filled, so the original still plays). newer: a3t.json's major is above formatVersion.
    // none: not a readable Matroska file with audio. Never throws; the file name is never parsed.
    // The length comes from a3t.json, so a normal file is not decoded; only a file without that
    // number is decoded once to count its frames.
    OpenResult openTrack (const juce::File&);

    // data = overlay(file data, sidecar) when the sidecar beside the file (or, for a renamed file,
    // the one in its folder that names its uuid) belongs to it; otherwise data is unchanged and
    // `error` says why. Damaged and newer data never take a sidecar.
    OpenResult openTrackWithSidecar (const juce::File&);
}
