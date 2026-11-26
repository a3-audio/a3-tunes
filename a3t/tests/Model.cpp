// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Model.h"

#include <gtest/gtest.h>

using namespace a3t;

namespace
{
    A3tData sample()
    {
        A3tData d;
        d.uuid = "u";
        d.track.title = "Title";
        d.cues.push_back (Cue{});
        d.loops.push_back (Loop{});
        d.phrases.push_back (Phrase{});
        d.events.push_back (Event{});
        return d;
    }

    template <typename Flip>
    void expectDiffers (Flip flip)
    {
        A3tData a = sample(), b = sample();
        ASSERT_TRUE (a == b);
        flip (b);
        EXPECT_FALSE (a == b);
    }
}

TEST (Model, DefaultsAreEmptyAndStemLabelsAreFour)
{
    A3tData d;
    EXPECT_EQ (d.stems.size(), 4u);
    EXPECT_TRUE (d.events.empty());
}

TEST (Model, NewUuidHasTheRightShapeAndDiffers)
{
    auto u = newUuid();
    EXPECT_EQ (u.length(), 36);
    for (int i : { 8, 13, 18, 23 })
        EXPECT_EQ (u[i], '-');
    EXPECT_EQ (u, u.toLowerCase());
    EXPECT_NE (newUuid(), newUuid());
}

TEST (Model, EqualityLooksAtEveryField)
{
    expectDiffers ([] (A3tData& d) { d.track.title = "Other"; });
    expectDiffers ([] (A3tData& d) { d.cues[0].time = 1.0; });
    expectDiffers ([] (A3tData& d) { d.stems[2].label = "Bass"; });
    expectDiffers ([] (A3tData& d) { d.events[0].beat = 3.0; });
}

TEST (Model, EqualityCoversTheRest)
{
    expectDiffers ([] (A3tData& d) { d.uuid = "v"; });
    expectDiffers ([] (A3tData& d) { d.revision = 1; });
    expectDiffers ([] (A3tData& d) { d.track.artist = "x"; });
    expectDiffers ([] (A3tData& d) { d.track.year = 1999; });
    expectDiffers ([] (A3tData& d) { d.track.gainDb = -3.0; });
    expectDiffers ([] (A3tData& d) { d.track.myTags.add ("a"); });
    expectDiffers ([] (A3tData& d) { d.track.audio.sampleRate = 48000; });
    expectDiffers ([] (A3tData& d) { d.track.audio.bitrate = 320000; });
    expectDiffers ([] (A3tData& d) { d.track.audio.source = "s"; });
    expectDiffers ([] (A3tData& d) { d.track.trackExtra = juce::JSON::parse ("{\"a\":1}"); });
    expectDiffers ([] (A3tData& d) { d.extra = juce::JSON::parse ("{\"b\":2}"); });
    expectDiffers ([] (A3tData& d) { d.analysis.bpm = 120.0; });
    expectDiffers ([] (A3tData& d) { d.analysis.key = Key{}; });
    expectDiffers ([] (A3tData& d) { d.analysis.grid = Grid{}; });
    expectDiffers ([] (A3tData& d) { d.analysis.loudness = Loudness{}; });
    expectDiffers ([] (A3tData& d) { d.stems[0].colour = "red"; });
    expectDiffers ([] (A3tData& d) { d.cues[0].kind = Cue::Kind::hot; });
    expectDiffers ([] (A3tData& d) { d.cues[0].name = "n"; });
    expectDiffers ([] (A3tData& d) { d.cues.push_back (Cue{}); });
    expectDiffers ([] (A3tData& d) { d.loops[0].out = 2.0; });
    expectDiffers ([] (A3tData& d) { d.loops[0].active = true; });
    expectDiffers ([] (A3tData& d) { d.phrases[0].mood = Phrase::Mood::high; });
    expectDiffers ([] (A3tData& d) { d.phrases[0].label = "l"; });
    expectDiffers ([] (A3tData& d) { d.events.clear(); });

    A3tData a, b;
    a.analysis.key = Key{ "8A", "Am", 0.5 };
    b.analysis.key = Key{ "8A", "Am", 0.6 };
    EXPECT_FALSE (a == b);
    a.analysis.grid = Grid{ 0.1, 120.0, 4, 2, false };
    b.analysis.grid = Grid{ 0.1, 120.0, 4, 2, true };
    b.analysis.key = a.analysis.key;
    EXPECT_FALSE (a == b);
}
