// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Json.h"
#include "a3t/Reader.h"
#include "a3t/Writer.h"
#include "MkvMuxer.h"
#include "TrackSource.h"
#include "TestAudio.h"
#include "TestMkv.h"

#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

using namespace a3t;

namespace
{
    constexpr int rate = 44100;
    constexpr juce::int64 frames = 30000;
    const juce::String uuid = "11111111-2222-3333-4444-555555555555";

    WriteRequest requestFor (const TempDir& t, int sampleRate = rate)
    {
        WriteRequest r;
        r.original = writeTestWav (t.dir, "original.wav", sampleRate, 2, 24, frames, 440.0);
        for (int s = 0; s < numStems; ++s)
            r.stems[(size_t) s] = writeTestWav (t.dir, "stem" + juce::String (s) + ".wav", sampleRate, 2, 24, frames,
                                                220.0 + 110.0 * s);
        r.data.uuid = uuid;
        r.data.revision = 3;
        r.data.track.title = "Test Title";
        r.data.track.rating = 2;
        r.data.cues = { hotCue (0, 0.05, "in") };
        r.cover = t.dir.getChildFile ("cover art.png");
        r.cover.replaceWithData (tinyPng, sizeof (tinyPng));
        r.output = t.dir.getChildFile ("song.a3mkv");
        return r;
    }

    WriteRequest written (const TempDir& t)
    {
        auto r = requestFor (t);
        const auto result = writeTrack (r);
        EXPECT_TRUE (result.ok) << result.error;
        return r;
    }

    A3tData dataOf (const juce::File& file)
    {
        const auto opened = openTrack (file);
        EXPECT_EQ (opened.status, Status::ok);
        return opened.data;
    }

    std::vector<Channels> allTracks (const juce::File& file)
    {
        std::vector<Channels> tracks;
        for (int i = 0; i < 1 + numStems; ++i)
            tracks.push_back (decodeAll (file, i));
        return tracks;
    }

    std::vector<std::vector<std::string>> allPackets (const juce::File& file)
    {
        std::vector<std::vector<std::string>> tracks;
        for (int i = 0; i < 1 + numStems; ++i)
            tracks.push_back (packetsOf (file, i, 1000));
        return tracks;
    }

    juce::File handBuilt (const TempDir& t, juce::int64 stemExtra)
    {
        return handBuiltFile (t.dir, rate, frames, stemExtra, uuid);
    }

    // Write permission off for as long as it lives, so the TempDir can still clean up after a failure.
    struct ReadOnly
    {
        explicit ReadOnly (const juce::File& d) : dir (d) { ::chmod (dir.getFullPathName().toRawUTF8(), 0555); }
        ~ReadOnly() { ::chmod (dir.getFullPathName().toRawUTF8(), 0755); }
        juce::File dir;
    };

    void expectRefusedAndUntouched (const juce::File& file, const A3tData& data, const char* why)
    {
        const auto before = bytesOf (file);
        const auto result = rewriteMetadata (file, data);
        EXPECT_FALSE (result.ok) << why;
        EXPECT_FALSE (result.error.isEmpty()) << why;
        EXPECT_EQ (bytesOf (file), before) << why;
        EXPECT_TRUE (noTemporaryIn (file.getParentDirectory())) << why;
    }
}

TEST (Rewrite, RewriteChangesOnlyTheMetadata)
{
    TempDir t;
    const auto r = written (t);
    const auto samples = allTracks (r.output);
    const auto packets = allPackets (r.output);
    const auto oldData = dataOf (r.output);

    auto changed = oldData;
    changed.revision = 4;
    changed.track.rating = 5;
    changed.cues.push_back (hotCue (1, 0.1, "drop"));
    const auto result = rewriteMetadata (r.output, changed);
    ASSERT_TRUE (result.ok) << result.error;

    EXPECT_TRUE (dataOf (r.output) == changed);
    EXPECT_EQ (allTracks (r.output), samples);
    EXPECT_EQ (allPackets (r.output), packets);
    const auto audio = audioStreamsOf (r.output);
    ASSERT_EQ (audio.size(), 5u);
    const char* names[] = { "Original", "Stem 1", "Stem 2", "Stem 3", "Stem 4" };
    for (size_t i = 0; i < audio.size(); ++i)
    {
        EXPECT_EQ (audio[i].title, names[i]);
        EXPECT_EQ (audio[i].isDefault, i == 0) << i;
    }
    const auto streams = streamsOf (r.output);
    const auto* cover = attachmentNamed (streams, "cover.png");
    ASSERT_NE (cover, nullptr);
    EXPECT_EQ (cover->mimetype, "image/png");
    EXPECT_EQ (cover->bytes, std::string ((const char*) tinyPng, sizeof (tinyPng)));
    EXPECT_TRUE (noTemporaryIn (t.dir));
}

// The audio facts belong to the file: whatever the caller hands in, a3t.json keeps what the file is.
TEST (Rewrite, TheAudioFactsComeFromTheFile)
{
    TempDir t;
    const auto r = written (t);
    const auto oldData = dataOf (r.output);
    auto changed = oldData;
    changed.track.audio = {};
    changed.track.audio.lengthSeconds = 99.0;
    ASSERT_TRUE (rewriteMetadata (r.output, changed).ok);
    const auto opened = openTrack (r.output);
    auto expected = changed;
    expected.track.audio = oldData.track.audio;
    EXPECT_TRUE (opened.data == expected);
    EXPECT_EQ (opened.layout.lengthFrames, frames);
}

// A converted file's a3t.json describes the source before conversion; a rewrite keeps that record.
TEST (Rewrite, AConvertedFileKeepsTheSourceFacts)
{
    TempDir t;
    auto r = requestFor (t, 48000);
    r.convert = ConvertTarget::flac44k24();
    ASSERT_TRUE (writeTrack (r).ok);
    const auto oldData = dataOf (r.output);
    ASSERT_EQ (oldData.track.audio.sampleRate, 48000);
    auto changed = oldData;
    changed.track.rating = 1;
    const auto result = rewriteMetadata (r.output, changed);
    ASSERT_TRUE (result.ok) << result.error;
    const auto opened = openTrack (r.output);
    EXPECT_TRUE (opened.data == changed);
    EXPECT_EQ (opened.layout.lengthFrames, av_rescale_rnd (frames, 44100, 48000, AV_ROUND_NEAR_INF));
}

TEST (Rewrite, RewriteRefusesAnotherTracksData)
{
    TempDir t;
    const auto r = written (t);
    auto other = dataOf (r.output);
    other.uuid = "99999999-0000-0000-0000-000000000000";
    expectRefusedAndUntouched (r.output, other, "another uuid");
    other.uuid = {};
    expectRefusedAndUntouched (r.output, other, "no uuid");
}

// A rewrite never repairs: a file whose a3t.json is unreadable or newer stays exactly as it is.
TEST (Rewrite, RewriteOfADamagedOrNewerFileIsRefused)
{
    TempDir t;
    const auto r = written (t);
    const auto data = dataOf (r.output);
    patchBytes (r.output, "\"version\": 1", "\"version\": x");
    ASSERT_EQ (openTrack (r.output).status, Status::damaged);
    expectRefusedAndUntouched (r.output, data, "damaged");

    patchBytes (r.output, "\"version\": x", "\"version\": 2");
    ASSERT_EQ (openTrack (r.output).status, Status::newer);
    expectRefusedAndUntouched (r.output, data, "newer");

    const auto notAnA3mkv = writeTestWav (t.dir, "plain.wav", rate, 2, 16, 100, 440.0);
    expectRefusedAndUntouched (notAnA3mkv, data, "none");
}

TEST (Rewrite, RewriteLeavesNoTemporaryAndKeepsTheOldFileOnFailure)
{
    TempDir t;
    // Stem 2 is longer than the rest: the copy is complete, and the verification rejects it.
    const auto uneven = handBuilt (t, 7);
    const auto data = dataOf (uneven);
    const auto before = bytesOf (uneven);
    const auto result = rewriteMetadata (uneven, data);
    EXPECT_FALSE (result.ok);
    EXPECT_TRUE (result.error.startsWith ("verification failed")) << result.error;
    EXPECT_EQ (bytesOf (uneven), before);
    EXPECT_TRUE (noTemporaryIn (t.dir));

    // No temporary can be created at all.
    if (::geteuid() == 0)
        GTEST_SKIP() << "root writes into a read-only directory";
    const auto r = written (t);
    const ReadOnly readOnly (t.dir);
    expectRefusedAndUntouched (r.output, dataOf (r.output), "read-only directory");
}

TEST (Rewrite, UnknownAttachmentsAreKeptAndMissingFactsMeasured)
{
    TempDir t;
    const auto hand = handBuilt (t, 0);
    auto data = dataOf (hand);
    data.track.rating = 3;
    const auto result = rewriteMetadata (hand, data);
    ASSERT_TRUE (result.ok) << result.error;
    const auto streams = streamsOf (hand);
    const auto* notes = attachmentNamed (streams, "notes.txt");
    ASSERT_NE (notes, nullptr);
    EXPECT_EQ (notes->mimetype, "text/plain");
    EXPECT_EQ (notes->bytes, "kept as is");
    const auto opened = openTrack (hand);
    EXPECT_EQ (opened.data.track.rating, 3);
    EXPECT_EQ (opened.data.track.audio.sampleRate, rate);
    EXPECT_EQ (opened.data.track.audio.channels, 2);
    EXPECT_NEAR (opened.data.track.audio.lengthSeconds, (double) frames / rate, 1e-9);
}

TEST (Rewrite, RewriteKeepsSeekExactness)
{
    TempDir t;
    const auto r = written (t);
    auto changed = dataOf (r.output);
    changed.track.rating = 4;
    ASSERT_TRUE (rewriteMetadata (r.output, changed).ok);
    for (int stem = 1; stem <= numStems; ++stem)
        expectSampleExactSeeks (r.output, stem, frames, { 1, 3968, 3969, 7777, 29990, 30000 });
}

TEST (Rewrite, RewriteRegeneratesTagsAndChapters)
{
    TempDir t;
    const auto r = written (t);
    auto changed = dataOf (r.output);
    changed.track.title = "New Title";
    changed.cues.push_back (hotCue (1, 0.1, "drop"));
    changed.cues.push_back (hotCue (2, 5.0, "past the end"));
    ASSERT_TRUE (rewriteMetadata (r.output, changed).ok);

    const auto format = av::openFormat (r.output);
    ASSERT_NE (format, nullptr);
    EXPECT_EQ (metadata (format->metadata, "TITLE"), "New Title");
    ASSERT_EQ (format->nb_chapters, 2u);
    EXPECT_EQ (metadata (format->chapters[0]->metadata, "title"), "A: in");
    EXPECT_EQ (metadata (format->chapters[1]->metadata, "title"), "B: drop");
    EXPECT_NEAR (format->chapters[1]->start * av_q2d (format->chapters[1]->time_base), 0.1, 1e-6);
    EXPECT_LE (format->chapters[1]->end * av_q2d (format->chapters[1]->time_base), (double) frames / rate + 1e-9);
}

TEST (Rewrite, MoreEventsThanTheCapLeaveTheFileUntouched)
{
    TempDir t;
    const auto r = written (t);
    auto data = dataOf (r.output);
    for (int i = 0; i <= maxEvents; ++i)
    {
        Event e;
        e.type = EventType::resetpos;
        e.beat = i;
        data.events.push_back (e);
    }
    expectRefusedAndUntouched (r.output, data, "513 events");
}

TEST (Rewrite, GridSegmentsSurviveARewrite)
{
    TempDir t;
    const auto r = written (t);
    auto data = parse (R"({"version":1,"analysis":{"grid":{"bpm":120,"segments":[{"start":0,"bpm":120}]}}})").data;
    const auto own = dataOf (r.output);
    data.uuid = own.uuid;
    data.revision = 4;
    const auto result = rewriteMetadata (r.output, data);
    ASSERT_TRUE (result.ok) << result.error;
    auto again = dataOf (r.output);
    again.revision = 5;
    ASSERT_TRUE (rewriteMetadata (r.output, again).ok);
    const auto text = serialise (dataOf (r.output));
    EXPECT_TRUE (juce::JSON::parse (text)["analysis"]["grid"]["segments"].isArray()) << text;
}

TEST (Rewrite, RewriteThroughASymlinkUpdatesTheRealFile)
{
    TempDir t;
    const auto r = written (t);
    const auto linkDir = t.dir.getChildFile ("links");
    ASSERT_TRUE (linkDir.createDirectory());
    const auto link = linkDir.getChildFile ("song.a3mkv");
    ASSERT_EQ (::symlink (r.output.getFullPathName().toRawUTF8(), link.getFullPathName().toRawUTF8()), 0);

    auto data = dataOf (link);
    data.revision = 9;
    const auto result = rewriteMetadata (link, data);
    ASSERT_TRUE (result.ok) << result.error;
    EXPECT_TRUE (link.isSymbolicLink());
    EXPECT_EQ (dataOf (r.output).revision, 9);
    EXPECT_TRUE (noTemporaryIn (t.dir));
    EXPECT_TRUE (noTemporaryIn (linkDir));
}

TEST (Rewrite, RewriteKeepsTheFileMode)
{
    TempDir t;
    const auto r = written (t);
    ASSERT_EQ (::chmod (r.output.getFullPathName().toRawUTF8(), 0640), 0);
    auto data = dataOf (r.output);
    data.revision = 9;
    ASSERT_TRUE (rewriteMetadata (r.output, data).ok);
    struct stat st {};
    ASSERT_EQ (::stat (r.output.getFullPathName().toRawUTF8(), &st), 0);
    EXPECT_EQ (st.st_mode & 07777, 0640u);
}
