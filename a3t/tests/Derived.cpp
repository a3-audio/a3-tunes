// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Derived.h"

#include <gtest/gtest.h>

using namespace a3t;

namespace
{
    std::string tagValue (const std::vector<Tag>& tags, const char* name)
    {
        for (const auto& t : tags)
            if (t.first == name)
                return t.second.toStdString();
        return "<absent>";
    }

    Cue cue (Cue::Kind kind, double time, int slot = -1, const char* name = "")
    {
        Cue c;
        c.kind = kind;
        c.time = time;
        c.slot = slot;
        c.name = name;
        return c;
    }
}

TEST (Derived, EmptyValuesAreOmitted)
{
    A3tData d;
    d.uuid = "u";
    d.track.title = "T";
    const auto tags = tagsFor (d);
    EXPECT_EQ (tagValue (tags, "TITLE"), "T");
    for (const char* n : { "ARTIST", "ALBUM", "ALBUM_ARTIST", "GENRE", "LABEL", "REMIXED_BY", "COMPOSER",
                           "LYRICIST", "COMMENT", "ISRC", "DATE_RELEASED", "PART_NUMBER", "BPM", "INITIAL_KEY" })
        EXPECT_EQ (tagValue (tags, n), "<absent>") << n;
}

TEST (Derived, FilledValuesAreWritten)
{
    A3tData d;
    d.track.artist = "A";
    d.track.remixer = "R";
    d.track.year = 2024;
    d.track.trackNo = 3;
    const auto tags = tagsFor (d);
    EXPECT_EQ (tagValue (tags, "ARTIST"), "A");
    EXPECT_EQ (tagValue (tags, "REMIXED_BY"), "R");
    EXPECT_EQ (tagValue (tags, "DATE_RELEASED"), "2024");
    EXPECT_EQ (tagValue (tags, "PART_NUMBER"), "3");
    EXPECT_EQ (tags.front().first, "ARTIST");
}

TEST (Derived, BpmAndKeyComeFromAnalysis)
{
    A3tData d;
    d.analysis.bpm = 126.0;
    d.analysis.key = Key { "8A", "Am", 0.9 };
    EXPECT_EQ (tagValue (tagsFor (d), "BPM"), "126");
    EXPECT_EQ (tagValue (tagsFor (d), "INITIAL_KEY"), "Am");
    d.analysis.bpm = 125.5;
    EXPECT_EQ (tagValue (tagsFor (d), "BPM"), "125.5");
    d.analysis.bpm = 125.25;
    EXPECT_EQ (tagValue (tagsFor (d), "BPM"), "125.25");
    d.analysis.bpm = 125.256;
    EXPECT_EQ (tagValue (tagsFor (d), "BPM"), "125.26");
}

TEST (Derived, UuidAndVersionAreAlwaysThere)
{
    A3tData d;
    d.uuid = "abc";
    const auto tags = tagsFor (d);
    EXPECT_EQ (tagValue (tags, "A3T_UUID"), "abc");
    EXPECT_EQ (tagValue (tags, "A3T_VERSION"), "1");
    EXPECT_EQ (tags.size(), 2u);
}

TEST (Derived, HotCueTitleIsLetterColonName)
{
    A3tData d;
    d.cues = { cue (Cue::Kind::hot, 1.0, 0, "drop"), cue (Cue::Kind::hot, 2.0, 15, "end"),
               cue (Cue::Kind::hot, 3.0, 0) };
    const auto ch = chaptersFor (d, 10.0);
    ASSERT_EQ (ch.size(), 3u);
    EXPECT_EQ (ch[0].title, "A: drop");
    EXPECT_EQ (ch[1].title, "P: end");
    EXPECT_EQ (ch[2].title, "A");
}

TEST (Derived, MemoryCueTitleIsItsName)
{
    A3tData d;
    d.cues = { cue (Cue::Kind::memory, 1.0, -1, "verse"), cue (Cue::Kind::memory, 2.0) };
    const auto ch = chaptersFor (d, 10.0);
    ASSERT_EQ (ch.size(), 2u);
    EXPECT_EQ (ch[0].title, "verse");
    EXPECT_EQ (ch[1].title, "");
}

TEST (Derived, ChaptersAreSortedAndEndAtTheNextStart)
{
    A3tData d;
    d.cues = { cue (Cue::Kind::memory, 5.0, -1, "b"), cue (Cue::Kind::hot, 2.0, 1, "a") };
    const auto ch = chaptersFor (d, 10.0);
    ASSERT_EQ (ch.size(), 2u);
    EXPECT_DOUBLE_EQ (ch[0].start, 2.0);
    EXPECT_DOUBLE_EQ (ch[0].end, 5.0);
    EXPECT_EQ (ch[0].title, "B: a");
    EXPECT_DOUBLE_EQ (ch[1].start, 5.0);
}

TEST (Derived, LastChapterEndsAtTheLength)
{
    A3tData d;
    d.cues = { cue (Cue::Kind::memory, 5.0, -1, "b") };
    const auto ch = chaptersFor (d, 42.5);
    ASSERT_EQ (ch.size(), 1u);
    EXPECT_DOUBLE_EQ (ch[0].end, 42.5);
}

TEST (Derived, LoadCueIsNotAChapter)
{
    A3tData d;
    d.cues = { cue (Cue::Kind::load, 0.0), cue (Cue::Kind::memory, 5.0, -1, "b") };
    EXPECT_EQ (chaptersFor (d, 10.0).size(), 1u);
    d.cues = { cue (Cue::Kind::load, 0.0) };
    EXPECT_TRUE (chaptersFor (d, 10.0).empty());
}

TEST (Derived, ZeroLengthChapterGetsAMinimalEnd)
{
    A3tData d;
    d.cues = { cue (Cue::Kind::memory, 5.0, -1, "a"), cue (Cue::Kind::memory, 5.0, -1, "b") };
    const auto ch = chaptersFor (d, 10.0);
    ASSERT_EQ (ch.size(), 2u);
    EXPECT_DOUBLE_EQ (ch[0].end, 5.001);
    EXPECT_DOUBLE_EQ (ch[1].end, 10.0);
}

TEST (Derived, HotCueSlotOutsideTheLettersIsTitledByItsName)
{
    A3tData d;
    d.cues = { cue (Cue::Kind::hot, 1.0, 16, "drop"), cue (Cue::Kind::hot, 2.0, -1, "intro"),
               cue (Cue::Kind::hot, 3.0, 99) };
    const auto ch = chaptersFor (d, 10.0);
    ASSERT_EQ (ch.size(), 3u);
    EXPECT_EQ (ch[0].title, "drop");
    EXPECT_EQ (ch[1].title, "intro");
    EXPECT_EQ (ch[2].title, "");
}

TEST (Derived, ChapterAtOrPastTheLengthIsDropped)
{
    A3tData d;
    d.cues = { cue (Cue::Kind::memory, 2.0, -1, "in"), cue (Cue::Kind::memory, 10.0, -1, "at"),
               cue (Cue::Kind::hot, 12.0, 0, "past") };
    const auto ch = chaptersFor (d, 10.0);
    ASSERT_EQ (ch.size(), 1u);
    EXPECT_EQ (ch[0].title, "in");
    EXPECT_DOUBLE_EQ (ch[0].end, 10.0);
}

TEST (Derived, ChapterEndsNeverPassTheLength)
{
    A3tData d;
    d.cues = { cue (Cue::Kind::memory, 9.9995, -1, "a"), cue (Cue::Kind::memory, 9.9995, -1, "b") };
    const auto ch = chaptersFor (d, 10.0);
    ASSERT_EQ (ch.size(), 2u);
    for (const auto& c : ch)
    {
        EXPECT_LE (c.end, 10.0);
        EXPECT_GT (c.end, c.start);
    }
}

TEST (Derived, NegativeCueTimeStartsAtZero)
{
    A3tData d;
    d.cues = { cue (Cue::Kind::memory, -0.5, -1, "early"), cue (Cue::Kind::memory, 4.0, -1, "b") };
    const auto ch = chaptersFor (d, 10.0);
    ASSERT_EQ (ch.size(), 2u);
    EXPECT_DOUBLE_EQ (ch[0].start, 0.0);
    EXPECT_DOUBLE_EQ (ch[0].end, 4.0);
}
