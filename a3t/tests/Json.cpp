// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Json.h"

#include <gtest/gtest.h>

#include <random>

using namespace a3t;

namespace
{
    A3tData fullSample()
    {
        A3tData d;
        d.uuid = "0a1b2c3d-0000-4000-8000-123456789abc";
        d.revision = 7;

        auto& t = d.track;
        t.title = "Title"; t.artist = "Artist"; t.album = "Album"; t.albumArtist = "AA";
        t.genre = "Techno"; t.label = "Label"; t.remixer = "Rem"; t.composer = "Comp";
        t.lyricist = "Lyr"; t.comment = "note"; t.isrc = "DE1234567890"; t.colour = "Rose";
        t.dateAdded = "2026-11-26T10:00:00Z"; t.sourceService = "tidal"; t.sourceId = "42";
        t.year = 2024; t.trackNo = 3; t.discNo = 1; t.rating = 4; t.playCount = 9;
        t.gainDb = -1.5;
        t.myTags.add ("peak"); t.myTags.add ("late");
        t.audio.sampleRate = 44100; t.audio.bitDepth = 24; t.audio.channels = 2;
        t.audio.bitrate = 1411200; t.audio.codec = "flac"; t.audio.lengthSeconds = 301.25;
        auto* extra = new juce::DynamicObject();
        extra->setProperty ("trackFuture", 5);
        t.trackExtra = juce::var (extra);

        Key k; k.camelot = "8A"; k.musical = "Am"; k.confidence = 0.82;
        d.analysis.bpm = 126.0;
        d.analysis.key = k;
        Grid g; g.firstBeat = 0.412; g.bpm = 126.0; g.beatsPerBar = 4; g.gridVersion = 2; g.corrected = true;
        d.analysis.grid = g;
        Loudness l; l.lufs = -9.1; l.peakDb = -0.3;
        d.analysis.loudness = l;

        d.stems[0] = { "Drums", "#ff0000" };
        d.stems[3] = { "Vocals", "#00ff00" };

        Cue hot; hot.kind = Cue::Kind::hot; hot.slot = 3; hot.time = 31.2; hot.colour = "#e6a000"; hot.name = "drop";
        Cue mem; mem.kind = Cue::Kind::memory; mem.time = 0.4; mem.colour = "#3b82f6";
        Cue load; load.kind = Cue::Kind::load; load.time = 0.0;
        d.cues = { hot, mem, load };

        Loop lp; lp.slot = 2; lp.in = 61.0; lp.out = 68.7; lp.colour = "#112233"; lp.name = "lp"; lp.active = true;
        Loop memLoop; memLoop.in = 10.0; memLoop.out = 12.0;
        d.loops = { lp, memLoop };

        Phrase p; p.kind = Phrase::Kind::chorus; p.mood = Phrase::Mood::high;
        p.start = 0.4; p.end = 30.1; p.label = "hook";
        d.phrases = { p };

        Event shot; shot.beat = 16.0; shot.shape = Shape::rise; shot.bars = 2.0; shot.reversed = true;
        shot.speed = 2.0; shot.seed = 123; shot.origin = Origin::prepared;
        Event sp; sp.type = EventType::setpos; sp.stem = 2; sp.beat = 32.0; sp.azimuth = -45.0; sp.elevation = 20.0;
        Event rs; rs.type = EventType::resetpos; rs.stem = 2; rs.beat = 64.0;
        d.events = { shot, sp, rs };

        auto* top = new juce::DynamicObject();
        top->setProperty ("futureThing", 1);
        d.extra = juce::var (top);
        return d;
    }

    juce::var parsed (const juce::String& s) { return juce::JSON::parse (s); }
}

TEST (Json, RoundTripKeepsEverything)
{
    const auto d = fullSample();
    const auto r = parse (serialise (d));
    EXPECT_EQ (r.status, Status::ok);
    EXPECT_TRUE (r.data == d);
}

TEST (Json, GarbageIsDamaged)
{
    const auto r = parse ("{{");
    EXPECT_EQ (r.status, Status::damaged);
    EXPECT_TRUE (r.data == A3tData{});
}

TEST (Json, NotAnObjectIsDamaged)
{
    EXPECT_EQ (parse ("[]").status, Status::damaged);
}

TEST (Json, NewerMajorIsNewerAndEmpty)
{
    const auto r = parse (R"({"version":2,"uuid":"x"})");
    EXPECT_EQ (r.status, Status::newer);
    EXPECT_TRUE (r.data == A3tData{});
}

TEST (Json, MissingVersionIsDamaged)
{
    EXPECT_EQ (parse (R"({"uuid":"x"})").status, Status::damaged);
    EXPECT_EQ (parse (R"({"version":"1"})").status, Status::damaged);
}

TEST (Json, UnknownKeysSurviveTheRoundTrip)
{
    const auto r = parse (R"({"version":1,"futureThing":{"a":1},"track":{"title":"T","trackFuture":[1,2]}})");
    ASSERT_EQ (r.status, Status::ok);
    const auto out = parsed (serialise (r.data));
    EXPECT_EQ (static_cast<int> (out["futureThing"]["a"]), 1);
    EXPECT_EQ (out["track"]["trackFuture"].size(), 2);
    EXPECT_EQ (out["track"]["title"].toString(), "T");
}

TEST (Json, BadFieldsAreSkippedNotFatal)
{
    const auto r = parse (R"({"version":1,
        "track":{"rating":9},
        "cues":[{"kind":"hot","slot":16,"time":1.0},
                {"kind":"memory","time":"soon"},
                {"kind":"hot","slot":2,"time":3.5},
                {"kind":"memory","time":4.0}]})");
    ASSERT_EQ (r.status, Status::ok);
    EXPECT_EQ (r.data.track.rating, 5);
    ASSERT_EQ (r.data.cues.size(), 2u);
    EXPECT_EQ (r.data.cues[0].slot, 2);
    EXPECT_EQ (r.data.cues[1].time, 4.0);
}

TEST (Json, SerialisedKeysAreStable)
{
    const auto d = fullSample();
    EXPECT_EQ (serialise (d), serialise (d));
    EXPECT_EQ (serialise (d), serialise (parse (serialise (d)).data));
}

TEST (Json, EventsBeyondTheCapAreDroppedOnLoad)
{
    juce::String s = R"({"version":1,"events":[)";
    for (int i = 0; i < maxEvents + 5; ++i)
        s << (i ? "," : "") << R"({"type":"resetpos","stem":0,"beat":)" << i << "}";
    s << "]}";
    const auto r = parse (s);
    ASSERT_EQ (r.status, Status::ok);
    EXPECT_EQ (static_cast<int> (r.data.events.size()), maxEvents);
}

TEST (Json, TrackHelpersRoundTripAndKeepExtras)
{
    const auto d = fullSample();
    const auto back = trackFromVar (trackToVar (d.track));
    TrackInfo expected = d.track;
    // compare through A3tData == to reuse the field-wise comparison
    A3tData a, b;
    a.track = expected; b.track = back;
    EXPECT_TRUE (a == b);
}

TEST (Json, RandomDoublesRoundTripExactly)
{
    std::mt19937_64 rng (12345);
    std::uniform_real_distribution<double> dist (0.0, 36000.0);
    A3tData d;
    for (int i = 0; i < 1000; ++i)
    {
        Cue c; c.time = dist (rng);
        d.cues.push_back (c);
    }
    const auto first = serialise (d);
    const auto a = parse (first).data;
    const auto b = parse (serialise (a)).data;
    EXPECT_TRUE (a == b);
    EXPECT_EQ (serialise (a), first);
}

TEST (Json, SumOfFloatsSurvivesAsQuantised)
{
    A3tData d;
    Cue c; c.time = 0.1 + 0.2;
    d.cues.push_back (c);
    const auto r = parse (serialise (d)).data;
    ASSERT_EQ (r.cues.size(), 1u);
    EXPECT_EQ (r.cues[0].time, quantised (0.3));
}

TEST (Json, HugeNumbersClampInsteadOfOverflowing)
{
    auto r = parse (R"({"version":1,"track":{"rating":1e300,"year":1e300,"audio":{"bitrate":1e300}}})");
    ASSERT_EQ (r.status, Status::ok);
    EXPECT_EQ (r.data.track.rating, 5);
    r = parse (R"({"version":1,"track":{"rating":-1e300}})");
    EXPECT_EQ (r.data.track.rating, 0);
}

TEST (Json, VersionIsComparedWide)
{
    EXPECT_EQ (parse (R"({"version":4294967297})").status, Status::newer);
    EXPECT_EQ (parse (R"({"version":0})").status, Status::damaged);
}

TEST (Json, DeepNestingIsDamagedNotACrash)
{
    EXPECT_EQ (parse (juce::String::repeatedString ("[", 100000)).status, Status::damaged);
    EXPECT_EQ (parse (juce::String::repeatedString (R"({"a":)", 100000)).status, Status::damaged);
    EXPECT_EQ (parse (R"({"version":1,"x":"[[[[\"[[[","y":[[1]]})").status, Status::ok);
}

TEST (Json, LoopSlot16AndUnknownCueKindAreSkipped)
{
    const auto r = parse (R"({"version":1,
        "loops":[{"slot":16,"in":1,"out":2},{"slot":15,"in":3,"out":4}],
        "cues":[{"kind":"banana","time":1},{"kind":"load","time":2}]})");
    ASSERT_EQ (r.status, Status::ok);
    ASSERT_EQ (r.data.loops.size(), 1u);
    EXPECT_EQ (r.data.loops[0].slot, 15);
    ASSERT_EQ (r.data.cues.size(), 1u);
    EXPECT_EQ (r.data.cues[0].kind, Cue::Kind::load);
}

TEST (Json, RandomEventsRoundTripExactly)
{
    std::mt19937_64 rng (777);
    std::uniform_real_distribution<double> beat (0.0, 2000.0), az (-180.0, 180.0), el (-90.0, 90.0);
    A3tData d;
    for (int i = 0; i < 1000; ++i)
    {
        Event e; e.type = EventType::setpos; e.stem = i % 4;
        e.beat = beat (rng); e.azimuth = az (rng); e.elevation = el (rng);
        d.events.push_back (e);
    }
    const auto first = serialise (d);
    const auto a = parse (first).data;
    const auto b = parse (serialise (a)).data;
    EXPECT_TRUE (a == b);
    EXPECT_EQ (serialise (a), serialise (b));
}
