// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "a3t/Events.h"

#include <juce_core/juce_core.h>

#include <array>
#include <optional>
#include <vector>

// The in-memory form of a3t.json: what a track file carries besides its audio.
namespace a3t
{
    constexpr int numStems = 4;
    constexpr int formatVersion = 1;   // a3t.json
    constexpr int sidecarVersion = 2;

    struct Key
    {
        juce::String camelot, musical;
        double confidence = 0.0;
    };

    struct Grid
    {
        double firstBeat = 0.0, bpm = 0.0;
        int beatsPerBar = 4;
        int gridVersion = 2;
        bool corrected = false;
    };

    struct Loudness
    {
        double lufs = 0.0, peakDb = 0.0;
    };

    struct Analysis
    {
        std::optional<double> bpm;
        std::optional<Key> key;
        std::optional<Grid> grid;
        std::optional<Loudness> loudness;
    };

    struct AudioInfo
    {
        int sampleRate = 0, bitDepth = 0, channels = 0;
        juce::int64 bitrate = 0;
        juce::String codec, source;
        double lengthSeconds = 0.0;
    };

    struct TrackInfo
    {
        juce::String title, artist, album, albumArtist, genre, label, remixer, composer, lyricist,
            comment, isrc, colour, dateAdded, sourceService, sourceId;
        int year = 0, trackNo = 0, discNo = 0, rating = 0, playCount = 0;
        double gainDb = 0.0;
        juce::StringArray myTags;
        AudioInfo audio;
        juce::var trackExtra;   // unknown keys of "track", kept on rewrite
    };

    struct Cue
    {
        enum class Kind { hot, memory, load } kind = Kind::memory;
        int slot = -1;
        double time = 0.0;
        juce::String colour, name;
    };

    struct Loop
    {
        int slot = -1;
        double in = 0.0, out = 0.0;
        juce::String colour, name;
        bool active = false;
    };

    struct Phrase
    {
        enum class Kind { intro, up, down, chorus, bridge, outro } kind = Kind::intro;
        enum class Mood { high, mid, low } mood = Mood::mid;
        double start = 0.0, end = 0.0;
        juce::String label;
    };

    struct StemLabel
    {
        juce::String label, colour;
    };

    struct A3tData
    {
        juce::String uuid;
        int revision = 0;
        TrackInfo track;
        Analysis analysis;
        std::array<StemLabel, numStems> stems;
        std::vector<Cue> cues;
        std::vector<Loop> loops;
        std::vector<Phrase> phrases;
        std::vector<Event> events;
        juce::var extra;   // unknown top-level keys
    };

    // The writer records the original's length as seconds at its own rate, which round-trips to the exact
    // frame count; this is that count at sampleRate, rescaled as the writer does. 0 without a record.
    juce::int64 lengthFramesAt (const AudioInfo&, int sampleRate);

    juce::String newUuid();   // random, lower-case, 8-4-4-4-12
    // The one order every section is kept in: cues by (time, hot < memory < load, slot),
    // loops by (in, slot), phrases by start, events as EventList sorts them. Stable.
    void canonicalOrder (A3tData&);
    bool operator== (const A3tData&, const A3tData&);   // field-wise; used by tests and the merge
}
