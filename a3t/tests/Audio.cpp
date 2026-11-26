// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Audio.h"
#include "TestAudio.h"

#include <gtest/gtest.h>

using namespace a3t;

namespace
{
    // The whole stream, channel 0, read in odd-sized blocks.
    std::vector<float> readAll (TrackDecoder& d)
    {
        std::vector<float> all;
        std::vector<std::vector<float>> buf ((size_t) d.channels(), std::vector<float> (777));
        std::vector<float*> planes;
        for (auto& b : buf)
            planes.push_back (b.data());
        for (int n; (n = d.read (planes.data(), 777)) > 0;)
            all.insert (all.end(), buf[0].begin(), buf[0].begin() + n);
        return all;
    }
}

TEST (Audio, ProbeReadsRateChannelsLengthDepth)
{
    TempDir t;
    const auto info = probeAudio (writeTestWav (t.dir, "a.wav", 44100, 2, 24, 10000, 440.0));
    ASSERT_TRUE (info.has_value());
    EXPECT_EQ (info->sampleRate, 44100);
    EXPECT_EQ (info->channels, 2);
    EXPECT_EQ (info->bitDepth, 24);
    EXPECT_EQ (info->lengthFrames, 10000);
    EXPECT_FALSE (info->codecName.isEmpty());
}

TEST (Audio, ProbeOfGarbageIsNullopt)
{
    TempDir t;
    const auto f = t.dir.getChildFile ("junk.wav");
    f.replaceWithText ("this is not audio at all, not even close");
    EXPECT_FALSE (probeAudio (f).has_value());
    EXPECT_FALSE (probeAudio (t.dir.getChildFile ("missing.wav")).has_value());
}

TEST (Audio, ProbeOfMonoAndOf96k)
{
    TempDir t;
    const auto mono = probeAudio (writeTestWav (t.dir, "m.wav", 48000, 1, 16, 4800, 100.0));
    ASSERT_TRUE (mono.has_value());
    EXPECT_EQ (mono->channels, 1);
    EXPECT_EQ (mono->bitDepth, 16);
    const auto hi = probeAudio (writeTestWav (t.dir, "h.wav", 96000, 2, 32, 9600, 100.0));
    ASSERT_TRUE (hi.has_value());
    EXPECT_EQ (hi->sampleRate, 96000);
    EXPECT_EQ (hi->bitDepth, 32);
    EXPECT_EQ (hi->lengthFrames, 9600);
}

TEST (Audio, ProbeOfASecondAudioTrackIsNulloptWhenThereIsNone)
{
    TempDir t;
    EXPECT_FALSE (probeAudio (writeTestWav (t.dir, "a.wav", 44100, 2, 16, 100, 440.0), 1).has_value());
}

TEST (Audio, DecoderReadsTheSineBack)
{
    TempDir t;
    auto d = TrackDecoder::open (writeTestWav (t.dir, "s.wav", 44100, 2, 24, 10000, 1000.0), 0);
    ASSERT_NE (d, nullptr);
    EXPECT_EQ (d->sampleRate(), 44100);
    EXPECT_EQ (d->channels(), 2);
    EXPECT_EQ (d->lengthFrames(), 10000);
    float left[100], right[100];
    float* planes[] = { left, right };
    ASSERT_EQ (d->read (planes, 100), 100);
    for (int n = 0; n < 100; ++n)
    {
        const auto expected = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / 44100.0);
        EXPECT_NEAR (left[n], expected, 1e-3) << n;
        EXPECT_NEAR (right[n], expected, 1e-3) << n;
    }
}

TEST (Audio, DecoderSeekIsSampleExact)
{
    TempDir t;
    const auto f = writeTestWav (t.dir, "s.wav", 44100, 2, 16, 10000, 1000.0);
    auto straight = TrackDecoder::open (f, 0);
    ASSERT_NE (straight, nullptr);
    const auto all = readAll (*straight);
    ASSERT_EQ (all.size(), 10000u);

    auto d = TrackDecoder::open (f, 0);
    ASSERT_NE (d, nullptr);
    float a[8], b[8];
    float* planes[] = { a, b };
    ASSERT_TRUE (d->seek (5000));
    ASSERT_EQ (d->read (planes, 8), 8);
    for (int n = 0; n < 8; ++n)
        EXPECT_EQ (a[n], all[(size_t) (5000 + n)]);
    ASSERT_TRUE (d->seek (0));
    ASSERT_EQ (d->read (planes, 8), 8);
    EXPECT_EQ (a[3], all[3]);
}

TEST (Audio, DecoderReadsToTheEndThenReturnsZero)
{
    TempDir t;
    auto d = TrackDecoder::open (writeTestWav (t.dir, "s.wav", 44100, 1, 16, 5000, 1000.0), 0);
    ASSERT_NE (d, nullptr);
    EXPECT_EQ (readAll (*d).size(), 5000u);
    float x[16];
    float* planes[] = { x };
    EXPECT_EQ (d->read (planes, 16), 0);
    EXPECT_EQ (d->read (planes, 16), 0);
}

TEST (Audio, OpenOfAMissingFileIsNull)
{
    TempDir t;
    EXPECT_EQ (TrackDecoder::open (t.dir.getChildFile ("nope.wav"), 0), nullptr);
    EXPECT_EQ (TrackDecoder::open (writeTestWav (t.dir, "a.wav", 44100, 1, 16, 10, 1.0), 3), nullptr);
}

TEST (Audio, BufferStaysBoundedOverAFullRead)
{
    TempDir t;
    const auto f = writeTestWav (t.dir, "long.wav", 44100, 2, 16, 44100 * 30, 300.0);
    auto d = TrackDecoder::open (f, 0);
    ASSERT_NE (d, nullptr);
    std::vector<std::vector<float>> buf (2, std::vector<float> (777));
    float* planes[] = { buf[0].data(), buf[1].data() };
    int peak = 0;
    juce::int64 total = 0;
    for (int n; (n = d->read (planes, 777)) > 0;)
    {
        total += n;
        peak = std::max (peak, d->bufferedFramesForTest());
    }
    EXPECT_EQ (total, 44100 * 30);
    EXPECT_LT (peak, 8192 + 4 * 777 + 8192);
    auto straight = TrackDecoder::open (f, 0);
    EXPECT_EQ (readAll (*straight).size(), (size_t) total);
}

TEST (Audio, CountFramesIsExactForEveryDepthAndLayout)
{
    TempDir t;
    for (int bits : { 16, 24, 32 })
        for (int ch : { 1, 2 })
        {
            const auto f = writeTestWav (t.dir, "c.wav", 44100, ch, bits, 12345, 440.0);
            const auto n = countFrames (f);
            ASSERT_TRUE (n.has_value());
            EXPECT_EQ (*n, 12345) << bits << "/" << ch;
        }
    const auto junk = t.dir.getChildFile ("junk.wav");
    junk.replaceWithText ("not audio");
    EXPECT_FALSE (countFrames (junk).has_value());
}

TEST (Audio, SeekPastTheEndFailsAndSeekToTheEndReadsZero)
{
    TempDir t;
    auto d = TrackDecoder::open (writeTestWav (t.dir, "s.wav", 44100, 1, 16, 5000, 100.0), 0);
    ASSERT_NE (d, nullptr);
    EXPECT_FALSE (d->seek (5001));
    float x[16];
    float* planes[] = { x };
    ASSERT_TRUE (d->seek (5000));
    EXPECT_EQ (d->read (planes, 16), 0);
}

TEST (Audio, ReadLargerThanTheFileReturnsTheWholeFile)
{
    TempDir t;
    auto d = TrackDecoder::open (writeTestWav (t.dir, "s.wav", 44100, 1, 16, 300, 100.0), 0);
    ASSERT_NE (d, nullptr);
    std::vector<float> big (10000);
    float* planes[] = { big.data() };
    EXPECT_EQ (d->read (planes, 10000), 300);
    EXPECT_EQ (d->read (planes, 10000), 0);
}

TEST (Audio, SeekIsExactIn24BitOnBothChannels)
{
    TempDir t;
    const auto f = writeTestWav (t.dir, "s.wav", 48000, 2, 24, 20000, 777.0);
    auto straight = TrackDecoder::open (f, 0);
    std::vector<std::vector<float>> ref (2, std::vector<float> (20000));
    float* refPlanes[] = { ref[0].data(), ref[1].data() };
    ASSERT_EQ (straight->read (refPlanes, 20000), 20000);

    auto d = TrackDecoder::open (f, 0);
    float a[16], b[16];
    float* planes[] = { a, b };
    for (juce::int64 at : { 7777, 1, 19990 })
    {
        ASSERT_TRUE (d->seek (at));
        ASSERT_EQ (d->read (planes, 10), 10);
        for (int n = 0; n < 10; ++n)
        {
            EXPECT_EQ (a[n], ref[0][(size_t) (at + n)]);
            EXPECT_EQ (b[n], ref[1][(size_t) (at + n)]);
        }
    }
}
