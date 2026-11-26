// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Merge.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace a3t;

namespace
{
    Event shotAt (double beat, int stem = 0, double bars = 4.0)
    {
        Event e;
        e.beat = beat;
        e.stem = stem;
        e.bars = bars;
        return e;
    }

    Event posAt (double beat, int stem, double azimuth)
    {
        Event e;
        e.type = EventType::setpos;
        e.beat = beat;
        e.stem = stem;
        e.azimuth = azimuth;
        return e;
    }

    Cue hotCue (int slot, double time)
    {
        Cue c;
        c.kind = Cue::Kind::hot;
        c.slot = slot;
        c.time = time;
        return c;
    }

    A3tData laptopData()
    {
        A3tData d;
        d.uuid = "0a1b2c3d-0000-4000-8000-123456789abc";
        d.revision = 7;
        d.events = { shotAt (8.0, 1), shotAt (16.0, 1), posAt (32.0, 2, 10.0) };
        d.cues = { hotCue (3, 12.5) };
        d.track.rating = 3;
        d.track.playCount = 10;
        return d;
    }

    Sidecar sidecarFor (const A3tData& d)
    {
        Sidecar s;
        s.basedOnUuid = d.uuid;
        s.basedOnRevision = d.revision;
        return s;
    }

    const Change* find (const std::vector<Change>& changes, const juce::String& key)
    {
        for (const auto& c : changes)
            if (c.key == key)
                return &c;
        return nullptr;
    }

    std::set<juce::String> allKeys (const std::vector<Change>& changes)
    {
        std::set<juce::String> keys;
        for (const auto& c : changes)
            keys.insert (c.key);
        return keys;
    }
}

TEST (Merge, SectionAbsentFromSidecarIsNotCompared)
{
    const auto laptop = laptopData();
    EXPECT_TRUE (diff (laptop, sidecarFor (laptop)).empty());

    auto rig = sidecarFor (laptop);
    rig.cues = std::vector<Cue> { hotCue (3, 12.5) };   // equal to the laptop's
    EXPECT_TRUE (diff (laptop, rig).empty());
}

TEST (Merge, AddedRemovedChangedEventsAreKeyedByStemBeatKind)
{
    const auto laptop = laptopData();
    auto rig = sidecarFor (laptop);
    rig.events = std::vector<Event> { shotAt (8.0, 1, 2.0),          // changed: bars
                                      /* 16.0 shot lacks: removed */
                                      posAt (32.0, 2, 10.0),
                                      shotAt (40.0, 3) };            // added
    const auto changes = diff (laptop, rig);
    ASSERT_EQ (changes.size(), 3u);

    const auto* changed = find (changes, "event:1:8.000000:shot");
    ASSERT_NE (changed, nullptr);
    EXPECT_EQ (changed->kind, Change::Kind::changed);
    EXPECT_EQ (changed->section, Change::Section::event);
    EXPECT_TRUE (changed->laptop.isNotEmpty() && changed->rig.isNotEmpty());

    const auto* removed = find (changes, "event:1:16.000000:shot");
    ASSERT_NE (removed, nullptr);
    EXPECT_EQ (removed->kind, Change::Kind::removed);
    EXPECT_TRUE (removed->rig.isEmpty());

    const auto* added = find (changes, "event:3:40.000000:shot");
    ASSERT_NE (added, nullptr);
    EXPECT_EQ (added->kind, Change::Kind::added);
    EXPECT_TRUE (added->laptop.isEmpty());
}

TEST (Merge, KeysOfTheOtherSections)
{
    auto laptop = laptopData();
    laptop.cues.push_back ([] { Cue c; c.kind = Cue::Kind::memory; c.time = 31.2; return c; }());
    laptop.loops = { [] { Loop l; l.slot = 3; l.in = 4.0; l.out = 8.0; return l; }(),
                     [] { Loop l; l.in = 61.0; l.out = 65.0; return l; }() };
    laptop.phrases = { [] { Phrase p; p.start = 96.0; p.end = 128.0; return p; }() };

    auto rig = sidecarFor (laptop);
    rig.cues = std::vector<Cue> {};
    rig.loops = std::vector<Loop> {};
    rig.phrases = std::vector<Phrase> {};
    const auto keys = allKeys (diff (laptop, rig));

    EXPECT_EQ (keys, (std::set<juce::String> { "cue:hot:3", "cue:memory:31.200000", "loop:3",
                                               "loop:in:61.000000", "phrase:96.000000" }));
}

TEST (Merge, TakingNothingChangesNothingButTheRevision)
{
    const auto laptop = laptopData();
    auto rig = sidecarFor (laptop);
    rig.events = std::vector<Event> { shotAt (8.0, 1, 2.0) };
    rig.trackFields.set ("rating", 5);

    auto result = applyChanges (laptop, rig, {});
    EXPECT_EQ (result.revision, laptop.revision + 1);
    EXPECT_EQ (result.uuid, laptop.uuid);
    result.revision = laptop.revision;
    EXPECT_TRUE (result == laptop);
}

TEST (Merge, TakingAKeyTakesThatKeyOnly)
{
    const auto laptop = laptopData();
    auto rig = sidecarFor (laptop);
    rig.events = std::vector<Event> { shotAt (8.0, 1, 2.0), shotAt (16.0, 1, 8.0), posAt (32.0, 2, 10.0) };

    const auto result = applyChanges (laptop, rig, { "event:1:8.000000:shot" });
    ASSERT_EQ (result.events.size(), 3u);
    EXPECT_EQ (result.events[0].bars, 2.0);   // taken
    EXPECT_EQ (result.events[1].bars, 4.0);   // kept
}

TEST (Merge, TakingARemovalRemoves)
{
    const auto laptop = laptopData();
    auto rig = sidecarFor (laptop);
    rig.events = std::vector<Event> { shotAt (8.0, 1), posAt (32.0, 2, 10.0) };

    const auto result = applyChanges (laptop, rig, { "event:1:16.000000:shot" });
    ASSERT_EQ (result.events.size(), 2u);
    EXPECT_EQ (result.events[0].beat, 8.0);
    EXPECT_EQ (result.events[1].beat, 32.0);
}

TEST (Merge, TakingAnAddKeepsEventsSorted)
{
    const auto laptop = laptopData();
    auto rig = sidecarFor (laptop);
    auto events = laptop.events;
    events.push_back (shotAt (12.0, 0));
    rig.events = events;

    const auto result = applyChanges (laptop, rig, { "event:0:12.000000:shot" });
    ASSERT_EQ (result.events.size(), 4u);
    EXPECT_EQ (result.events[1].beat, 12.0);
}

TEST (Merge, PlayCountTakesTheLargerAutomatically)
{
    const auto laptop = laptopData();   // 10

    auto rig = sidecarFor (laptop);
    rig.trackFields.set ("playCount", 31);
    auto changes = diff (laptop, rig);
    ASSERT_EQ (changes.size(), 1u);
    EXPECT_EQ (changes[0].key, "trackField:playCount");
    EXPECT_TRUE (changes[0].automatic);
    EXPECT_EQ (applyChanges (laptop, rig, {}).track.playCount, 31);

    rig.trackFields.set ("playCount", 4);
    EXPECT_EQ (applyChanges (laptop, rig, {}).track.playCount, 10);
    EXPECT_EQ (applyChanges (laptop, rig, { "trackField:playCount" }).track.playCount, 10);
}

TEST (Merge, TrackFieldsAreChosenByName)
{
    const auto laptop = laptopData();
    auto rig = sidecarFor (laptop);
    rig.trackFields.set ("rating", 5);
    rig.trackFields.set ("comment", "tight");

    const auto changes = diff (laptop, rig);
    ASSERT_EQ (changes.size(), 2u);
    EXPECT_FALSE (find (changes, "trackField:rating")->automatic);

    const auto result = applyChanges (laptop, rig, { "trackField:rating" });
    EXPECT_EQ (result.track.rating, 5);
    EXPECT_TRUE (result.track.comment.isEmpty());
}

TEST (Merge, GridIsOneItem)
{
    auto laptop = laptopData();
    Grid g; g.firstBeat = 0.25; g.bpm = 126.0;
    laptop.analysis.grid = g;

    auto rig = sidecarFor (laptop);
    Analysis a;
    g.firstBeat = 0.5; g.bpm = 127.0; g.corrected = true;
    a.grid = g;
    rig.analysis = a;

    const auto changes = diff (laptop, rig);
    ASSERT_EQ (changes.size(), 1u);
    EXPECT_EQ (changes[0].key, "grid");
    EXPECT_EQ (changes[0].section, Change::Section::grid);
    EXPECT_EQ (changes[0].kind, Change::Kind::changed);

    const auto result = applyChanges (laptop, rig, { "grid" });
    EXPECT_EQ (result.analysis.grid->bpm, 127.0);
    EXPECT_EQ (result.analysis.grid->firstBeat, 0.5);
}

TEST (Merge, AnalysisMembersAreKeyedApart)
{
    auto laptop = laptopData();
    laptop.analysis.bpm = 126.0;
    auto rig = sidecarFor (laptop);
    Analysis a;
    a.bpm = 127.0;
    a.key = Key { "8A", "Am", 0.5 };
    rig.analysis = a;

    const auto changes = diff (laptop, rig);
    ASSERT_EQ (changes.size(), 2u);
    EXPECT_EQ (find (changes, "analysis:bpm")->kind, Change::Kind::changed);
    EXPECT_EQ (find (changes, "analysis:key")->kind, Change::Kind::added);
}

TEST (Merge, LaptopChangedSinceIsRevisionComparison)
{
    auto laptop = laptopData();
    auto rig = sidecarFor (laptop);
    EXPECT_FALSE (laptopChangedSince (laptop, rig));
    laptop.revision = 8;
    EXPECT_TRUE (laptopChangedSince (laptop, rig));
    rig.basedOnRevision = 9;
    EXPECT_FALSE (laptopChangedSince (laptop, rig));
}

TEST (Merge, ApplyThenOverlayAgreeWhenEverythingIsTaken)
{
    auto laptop = laptopData();
    laptop.loops = { [] { Loop l; l.slot = 1; l.in = 4.0; l.out = 8.0; return l; }() };
    laptop.phrases = { [] { Phrase p; p.start = 0.0; p.end = 32.0; return p; }() };
    laptop.analysis.bpm = 126.0;

    auto rig = sidecarFor (laptop);
    rig.events = std::vector<Event> { shotAt (8.0, 1, 2.0), shotAt (20.0, 3) };
    rig.cues = std::vector<Cue> { hotCue (3, 13.0), hotCue (4, 20.0) };
    rig.loops = std::vector<Loop> {};
    rig.phrases = std::vector<Phrase> { [] { Phrase p; p.start = 0.0; p.end = 16.0; return p; }() };
    Analysis a; a.bpm = 127.0; Grid g; g.bpm = 127.0; a.grid = g;
    rig.analysis = a;
    rig.trackFields.set ("rating", 5);
    rig.trackFields.set ("playCount", 40);

    auto applied = applyChanges (laptop, rig, allKeys (diff (laptop, rig)));
    EXPECT_EQ (applied.revision, laptop.revision + 1);
    applied.revision = laptop.revision;
    EXPECT_TRUE (applied == overlay (laptop, rig));
}

namespace
{
    Cue memoryCue (double time)
    {
        Cue c;
        c.kind = Cue::Kind::memory;
        c.time = time;
        return c;
    }

    Loop loopAt (int slot, double in)
    {
        Loop l;
        l.slot = slot;
        l.in = in;
        l.out = in + 4.0;
        return l;
    }

    Phrase phraseAt (double start)
    {
        Phrase p;
        p.start = start;
        p.end = start + 16.0;
        return p;
    }
}

TEST (Merge, ReorderedAndInsertedSectionsStillAgreeWithOverlay)
{
    auto laptop = laptopData();
    laptop.cues = { hotCue (1, 8.0), hotCue (2, 16.0), hotCue (3, 24.0) };
    laptop.loops = { loopAt (1, 4.0), loopAt (2, 20.0) };
    laptop.phrases = { phraseAt (0.0), phraseAt (32.0) };

    auto rig = sidecarFor (laptop);
    rig.cues = std::vector<Cue> { hotCue (3, 24.0), hotCue (1, 8.0), hotCue (5, 12.0), hotCue (2, 16.0) };
    rig.loops = std::vector<Loop> { loopAt (2, 20.0), loopAt (7, 10.0), loopAt (1, 4.0) };
    rig.phrases = std::vector<Phrase> { phraseAt (32.0), phraseAt (16.0), phraseAt (0.0) };

    auto applied = applyChanges (laptop, rig, allKeys (diff (laptop, rig)));
    applied.revision = laptop.revision;
    EXPECT_TRUE (applied == overlay (laptop, rig));
}

TEST (Merge, OverlayReturnsCanonicalOrder)
{
    const auto laptop = laptopData();
    auto rig = sidecarFor (laptop);
    rig.cues = std::vector<Cue> { hotCue (3, 24.0), memoryCue (8.0), hotCue (1, 8.0) };
    rig.loops = std::vector<Loop> { loopAt (2, 20.0), loopAt (1, 4.0) };
    rig.phrases = std::vector<Phrase> { phraseAt (32.0), phraseAt (0.0) };

    const auto out = overlay (laptop, rig);
    ASSERT_EQ (out.cues.size(), 3u);
    EXPECT_EQ (out.cues[0].kind, Cue::Kind::hot);   // time 8, hot before memory
    EXPECT_EQ (out.cues[1].kind, Cue::Kind::memory);
    EXPECT_EQ (out.cues[2].slot, 3);
    EXPECT_EQ (out.loops[0].in, 4.0);
    EXPECT_EQ (out.phrases[0].start, 0.0);
}

TEST (Merge, NoEventIsEverDropped)
{
    A3tData laptop = laptopData();
    laptop.events.clear();
    for (int i = 0; i < maxEvents; ++i)
        laptop.events.push_back (shotAt (i * 2.0));
    auto rig = sidecarFor (laptop);
    auto events = laptop.events;
    events.push_back (shotAt (1.0, 2));
    rig.events = events;

    const auto applied = applyChanges (laptop, rig, { "event:2:1.000000:shot" });
    ASSERT_EQ (applied.events.size(), 513u);
    EXPECT_TRUE (std::is_sorted (applied.events.begin(), applied.events.end(),
                                 [] (const Event& a, const Event& b) { return a.beat < b.beat; }));
    EXPECT_EQ (overlay (laptop, rig).events.size(), 513u);
}

TEST (Merge, PlayCountNotLargerIsNoDifference)
{
    const auto laptop = laptopData();   // 10
    auto rig = sidecarFor (laptop);
    rig.trackFields.set ("playCount", 4);
    EXPECT_TRUE (diff (laptop, rig).empty());
    rig.trackFields.set ("playCount", 10);
    EXPECT_TRUE (diff (laptop, rig).empty());
    rig.trackFields.set ("playCount", 11);
    EXPECT_EQ (diff (laptop, rig).size(), 1u);
}

TEST (Merge, UnknownEventSurvivesWithItsRawPayload)
{
    juce::var parsed;
    ASSERT_TRUE (juce::JSON::parse (R"({"type":"future","stem":1,"beat":8.0,"x":[1,2]})", parsed).wasOk());
    Event unknown;
    unknown.type = EventType::unknown;
    unknown.stem = 1;
    unknown.beat = 8.0;
    unknown.raw = parsed;

    auto laptop = laptopData();
    laptop.events.push_back (unknown);
    auto rig = sidecarFor (laptop);
    rig.events = std::vector<Event> { shotAt (8.0, 1) };   // the unknown one is gone from the rig

    const auto changes = diff (laptop, rig);
    const auto* removed = find (changes, "event:1:8.000000:unknown");
    ASSERT_NE (removed, nullptr);
    EXPECT_EQ (removed->kind, Change::Kind::removed);

    const auto count = [] (const A3tData& d)
    {
        return std::count_if (d.events.begin(), d.events.end(), [] (const Event& e) { return e.type == EventType::unknown; });
    };
    const auto kept = applyChanges (laptop, rig, {});
    ASSERT_EQ (count (kept), 1);
    for (const auto& e : kept.events)
        if (e.type == EventType::unknown)
            EXPECT_EQ (juce::JSON::toString (e.raw, true), juce::JSON::toString (parsed, true));
    EXPECT_EQ (count (applyChanges (laptop, rig, { "event:1:8.000000:unknown" })), 0);

    // present in the rig, not in the laptop: taken, it arrives with its payload
    auto rigWith = sidecarFor (laptop);
    rigWith.events = laptop.events;
    auto bare = laptop;
    bare.events.erase (std::remove_if (bare.events.begin(), bare.events.end(),
                                       [] (const Event& e) { return e.type == EventType::unknown; }),
                       bare.events.end());
    const auto added = applyChanges (bare, rigWith, { "event:1:8.000000:unknown" });
    ASSERT_EQ (count (added), 1);
    for (const auto& e : added.events)
        if (e.type == EventType::unknown)
            EXPECT_EQ (juce::JSON::toString (e.raw, true), juce::JSON::toString (parsed, true));
}

TEST (Merge, DuplicateKeysAreConsistentBetweenDiffAndApply)
{
    auto laptop = laptopData();
    laptop.cues = { memoryCue (5.0), memoryCue (5.0) };
    auto rig = sidecarFor (laptop);
    Cue named = memoryCue (5.0);
    named.name = "second";
    rig.cues = std::vector<Cue> { memoryCue (5.0), named };

    const auto changes = diff (laptop, rig);
    ASSERT_EQ (changes.size(), 1u);
    EXPECT_EQ (changes[0].key, "cue:memory:5.000000#2");

    auto applied = applyChanges (laptop, rig, allKeys (changes));
    applied.revision = laptop.revision;
    EXPECT_TRUE (applied == overlay (laptop, rig));
    EXPECT_EQ (applied.cues[1].name, "second");
}

TEST (Merge, TakingARemovalOfCueLoopAndPhrase)
{
    auto laptop = laptopData();
    laptop.loops = { loopAt (1, 4.0) };
    laptop.phrases = { phraseAt (0.0) };
    auto rig = sidecarFor (laptop);
    rig.cues = std::vector<Cue> {};
    rig.loops = std::vector<Loop> {};
    rig.phrases = std::vector<Phrase> {};

    const auto result = applyChanges (laptop, rig, { "cue:hot:3", "loop:1", "phrase:0.000000" });
    EXPECT_TRUE (result.cues.empty());
    EXPECT_TRUE (result.loops.empty());
    EXPECT_TRUE (result.phrases.empty());

    const auto kept = applyChanges (laptop, rig, {});
    EXPECT_EQ (kept.cues.size(), 1u);
    EXPECT_EQ (kept.loops.size(), 1u);
    EXPECT_EQ (kept.phrases.size(), 1u);
}

TEST (Merge, ChangedMemoryCueIsTakenByKey)
{
    auto laptop = laptopData();
    laptop.cues = { memoryCue (31.2) };
    auto rig = sidecarFor (laptop);
    Cue renamed = memoryCue (31.2);
    renamed.name = "break";
    rig.cues = std::vector<Cue> { renamed };

    const auto changes = diff (laptop, rig);
    ASSERT_EQ (changes.size(), 1u);
    EXPECT_EQ (changes[0].kind, Change::Kind::changed);
    EXPECT_EQ (applyChanges (laptop, rig, { "cue:memory:31.200000" }).cues[0].name, "break");
    EXPECT_TRUE (applyChanges (laptop, rig, {}).cues[0].name.isEmpty());
}
