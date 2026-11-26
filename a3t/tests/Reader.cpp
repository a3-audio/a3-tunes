// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Reader.h"
#include "a3t/Sidecar.h"
#include "a3t/Writer.h"
#include "MkvMuxer.h"
#include "TrackSource.h"
#include "TestAudio.h"
#include "TestMkv.h"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>

using namespace a3t;

namespace
{
    constexpr int rate = 44100;
    constexpr juce::int64 frames = 30000;

    WriteRequest requestFor (const juce::File& dir, int sampleRate, int channels, juce::int64 length,
                             const juce::String& outputName)
    {
        WriteRequest r;
        r.original = writeTestWav (dir, "original.wav", sampleRate, channels, 16, length, 440.0);
        for (int s = 0; s < numStems; ++s)
            r.stems[(size_t) s] = writeTestWav (dir, "stem" + juce::String (s) + ".wav", sampleRate, channels, 16,
                                                length, 220.0 + 110.0 * s);
        r.data.uuid = "11111111-2222-3333-4444-555555555555";
        r.data.revision = 3;
        r.data.track.title = "Test Title";
        r.data.track.rating = 2;
        r.output = dir.getChildFile (outputName);
        return r;
    }

    WriteRequest written (const TempDir& t, const juce::String& name = "out.a3mkv")
    {
        auto r = requestFor (t.dir, rate, 2, frames, name);
        const auto result = writeTrack (r);
        EXPECT_TRUE (result.ok) << result.error;
        return r;
    }

    // Two PCM tracks and no attachment: what any other tool's .mkv looks like.
    juce::File plainTwoTrackMkv (const TempDir& t)
    {
        const auto a = writeTestWav (t.dir, "a.wav", rate, 2, 16, 4410, 440.0);
        const auto b = writeTestWav (t.dir, "b.wav", rate, 2, 16, 4410, 550.0);
        const auto out = t.dir.getChildFile ("plain.mkv");
        std::array<std::unique_ptr<TrackSource>, 2> sources { copyPackets (a), copyPackets (b) };
        MkvMuxer muxer;
        EXPECT_TRUE (muxer.create (out));
        std::array<int, 2> streams {};
        for (size_t i = 0; i < 2; ++i)
            streams[i] = muxer.addAudio (sources[i]->parameters(), sources[i]->timeBase(), "T" + juce::String ((int) i), i == 0);
        EXPECT_TRUE (muxer.start());
        for (size_t i = 0; i < 2; ++i)
        {
            av::Packet packet (av_packet_alloc());
            while (sources[i]->next (*packet))
                EXPECT_TRUE (muxer.write (*packet, streams[i], sources[i]->timeBase()));
        }
        EXPECT_TRUE (muxer.finish());
        return out;
    }

    Sidecar sidecarFor (const juce::String& uuid, int rating)
    {
        Sidecar s;
        s.basedOnUuid = uuid;
        s.trackFields.set ("rating", rating);
        return s;
    }
}

TEST (Reader, ReadsWhatWasWritten)
{
    TempDir t;
    const auto r = written (t);
    const auto opened = openTrack (r.output);
    ASSERT_EQ (opened.status, Status::ok) << opened.error;
    auto expected = r.data;
    expected.track.audio = opened.data.track.audio;   // the writer fills the file facts
    EXPECT_TRUE (opened.data == expected);
    EXPECT_EQ (opened.data.uuid, r.data.uuid);
    EXPECT_EQ (opened.layout.audioTracks, 5);
    EXPECT_TRUE (opened.layout.isFiveTrack);
    EXPECT_EQ (opened.layout.sampleRate, rate);
    EXPECT_EQ (opened.layout.lengthFrames, frames);   // exact; the container only declares milliseconds
}

TEST (Reader, ListingNeedsNoDecoding)
{
    // Time only (ruling R6): no byte counting. Built once; 8 kHz mono keeps the five tracks small.
    static const TempDir tenMinutes;
    static const auto request = [&]
    {
        auto r = requestFor (tenMinutes.dir, 8000, 1, 8000 * 600, "long.a3mkv");
        EXPECT_TRUE (writeTrack (r).ok);
        return r;
    }();

    // Relative to one decode of the same file, so a loaded machine slows both sides alike.
    const auto millisecondsOf = [] (auto&& f)
    {
        const auto start = std::chrono::steady_clock::now();
        f();
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    };
    OpenResult opened;
    const auto listing = millisecondsOf ([&] { opened = openTrack (request.output); });
    std::optional<juce::int64> counted;
    const auto decoding = millisecondsOf ([&] { counted = countFrames (request.output, 0); });
    ASSERT_EQ (opened.status, Status::ok) << opened.error;
    EXPECT_EQ (opened.layout.lengthFrames, 8000 * 600);
    EXPECT_EQ (counted, juce::int64 (8000 * 600));
    EXPECT_LT (listing, decoding / 5.0) << "listing " << listing << " ms, decoding " << decoding << " ms";
}

TEST (Reader, FileWithoutA3tJsonIsDamagedButPlays)
{
    TempDir t;
    const auto opened = openTrack (plainTwoTrackMkv (t));
    EXPECT_EQ (opened.status, Status::damaged);
    EXPECT_EQ (opened.layout.audioTracks, 2);
    EXPECT_FALSE (opened.layout.isFiveTrack);
    EXPECT_EQ (opened.layout.sampleRate, rate);
    EXPECT_EQ (opened.layout.lengthFrames, 4410);   // no a3t.json: counted by decoding
}

TEST (Reader, UnparsableA3tJsonIsDamaged)
{
    TempDir t;
    const auto r = written (t);
    patchBytes (r.output, "\"version\": 1", "\"version\": x");
    const auto opened = openTrack (r.output);
    EXPECT_EQ (opened.status, Status::damaged);
    EXPECT_EQ (opened.layout.audioTracks, 5);
}

TEST (Reader, NewerMajorIsNewer)
{
    TempDir t;
    const auto r = written (t);
    patchBytes (r.output, "\"version\": 1", "\"version\": 2");
    const auto opened = openTrack (r.output);
    EXPECT_EQ (opened.status, Status::newer);
    EXPECT_EQ (opened.layout.audioTracks, 5);
}

TEST (Reader, NotAMkvIsNone)
{
    TempDir t;
    const auto text = t.dir.getChildFile ("notes.txt");
    text.replaceWithText ("hello");
    EXPECT_EQ (openTrack (text).status, Status::none);
    EXPECT_EQ (openTrack (t.dir.getChildFile ("missing.a3mkv")).status, Status::none);
    EXPECT_EQ (openTrack (writeTestWav (t.dir, "x.wav", rate, 2, 16, 100, 440.0)).status, Status::none);
}

TEST (Reader, AnyFileNameWorks)
{
    TempDir t;
    const auto r = written (t, "my track (v2).final.take");
    EXPECT_EQ (openTrack (r.output).status, Status::ok);
}

TEST (Reader, SidecarIsOverlaid)
{
    TempDir t;
    const auto r = written (t);
    ASSERT_TRUE (saveSidecar (sidecarFileFor (r.output), sidecarFor (r.data.uuid, 4)));
    const auto opened = openTrackWithSidecar (r.output);
    ASSERT_EQ (opened.status, Status::ok);
    EXPECT_EQ (opened.data.track.rating, 4);
    EXPECT_TRUE (opened.error.isEmpty()) << opened.error;
    EXPECT_EQ (opened.appliedSidecar, sidecarFileFor (r.output));
}

TEST (Reader, ForeignSidecarIsReportedAndNotApplied)
{
    TempDir t;
    const auto r = written (t);
    ASSERT_TRUE (saveSidecar (sidecarFileFor (r.output), sidecarFor ("99999999-0000-0000-0000-000000000000", 5)));
    const auto opened = openTrackWithSidecar (r.output);
    ASSERT_EQ (opened.status, Status::ok);
    EXPECT_EQ (opened.data.track.rating, 2);
    EXPECT_TRUE (opened.error.contains ("another track")) << opened.error;
    EXPECT_EQ (opened.appliedSidecar, juce::File());
}

TEST (Reader, RenamedFileStillFindsItsSidecarByUuid)
{
    TempDir t;
    const auto r = written (t);
    ASSERT_TRUE (saveSidecar (sidecarFileFor (r.output), sidecarFor (r.data.uuid, 5)));
    const auto renamed = t.dir.getChildFile ("renamed.a3mkv");
    ASSERT_TRUE (r.output.moveFileTo (renamed));
    const auto opened = openTrackWithSidecar (renamed);
    ASSERT_EQ (opened.status, Status::ok);
    EXPECT_EQ (opened.data.track.rating, 5);
    EXPECT_EQ (opened.appliedSidecar, sidecarFileFor (r.output));
}

TEST (Reader, SidecarIsNotAppliedToDamagedData)
{
    TempDir t;
    const auto plain = plainTwoTrackMkv (t);
    ASSERT_TRUE (saveSidecar (sidecarFileFor (plain), sidecarFor ("", 5)));
    const auto opened = openTrackWithSidecar (plain);
    EXPECT_EQ (opened.status, Status::damaged);
    EXPECT_EQ (opened.data.track.rating, 0);
}

TEST (Reader, ConvertedFileHasExactLengthToo)
{
    TempDir t;
    auto r = requestFor (t.dir, 48000, 2, 30001, "conv.a3mkv");
    r.convert = ConvertTarget::flac44k24();
    ASSERT_TRUE (writeTrack (r).ok);
    const auto opened = openTrack (r.output);
    ASSERT_EQ (opened.status, Status::ok);
    EXPECT_EQ (opened.layout.sampleRate, 44100);
    EXPECT_EQ (opened.layout.lengthFrames, av_rescale_rnd (30001, 44100, 48000, AV_ROUND_NEAR_INF));
}
