// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Audio.h"
#include "a3t/Json.h"
#include "a3t/Writer.h"
#include "AvPtr.h"
#include "WriterTesting.h"
#include "TestAudio.h"
#include "TestMkv.h"

#include <gtest/gtest.h>

using namespace a3t;

namespace
{
    constexpr std::array<double, numStems> stemFrequencies { 220.0, 330.0, 550.0, 770.0 };

    // Five WAVs of equal length; the stems are sines at different pitches so a swap would show.
    WriteRequest requestFor (const TempDir& t, int rate, int bits, juce::int64 frames, int originalChannels = 2,
                             const juce::String& outputName = "out.a3mkv")
    {
        WriteRequest r;
        r.original = writeTestWav (t.dir, "original.wav", rate, originalChannels, bits, frames, 440.0);
        for (int s = 0; s < numStems; ++s)
            r.stems[(size_t) s] = writeTestWav (t.dir, "stem" + juce::String (s) + ".wav", rate, 2, bits, frames,
                                                stemFrequencies[(size_t) s]);
        r.data.track.title = "Test Title";
        r.output = t.dir.getChildFile (outputName);
        return r;
    }

    // The 330 Hz test sine into an interleaved S32 or S16, or a planar float, stereo frame.
    void fillSine (AVFrame& frame, juce::int64 position)
    {
        for (int i = 0; i < frame.nb_samples; ++i)
        {
            const auto phase = 2.0 * juce::MathConstants<double>::pi * 330.0 * (double) (position + i) / frame.sample_rate;
            const auto value = 0.5 * std::sin (phase);
            if (frame.format == AV_SAMPLE_FMT_S32)
            {
                auto* samples = reinterpret_cast<int32_t*> (frame.data[0]);
                samples[2 * i] = samples[2 * i + 1] = (int32_t) std::lround (value * 8388607.0) * 256;
                continue;
            }
            if (frame.format == AV_SAMPLE_FMT_S16)
            {
                auto* samples = reinterpret_cast<int16_t*> (frame.data[0]);
                samples[2 * i] = samples[2 * i + 1] = (int16_t) std::lround (value * 32767.0);
                continue;
            }
            for (int c = 0; c < 2; ++c)
                reinterpret_cast<float*> (frame.data[c])[i] = (float) value;
        }
    }

    // A stereo sine encoded straight through libav with an opened encoder into `muxer`'s format.
    juce::File encodeSine (const juce::File& file, const char* muxer, AVCodecContext& encoder, juce::int64 frames)
    {
        const auto path = file.getFullPathName().toStdString();
        AVFormatContext* out = nullptr;
        if (avformat_alloc_output_context2 (&out, nullptr, muxer, path.c_str()) < 0)
            return {};
        auto* stream = avformat_new_stream (out, nullptr);
        avcodec_parameters_from_context (stream->codecpar, &encoder);
        stream->time_base = encoder.time_base;
        if (avio_open (&out->pb, path.c_str(), AVIO_FLAG_WRITE) < 0 || avformat_write_header (out, nullptr) < 0)
        {
            avio_closep (&out->pb);
            avformat_free_context (out);
            return {};
        }

        av::Frame frame { av_frame_alloc() };
        av::Packet packet { av_packet_alloc() };
        const auto drain = [&]
        {
            while (avcodec_receive_packet (&encoder, packet.get()) >= 0)
            {
                av_packet_rescale_ts (packet.get(), encoder.time_base, stream->time_base);
                av_interleaved_write_frame (out, packet.get());
            }
        };
        const auto blockSize = encoder.frame_size;
        for (juce::int64 position = 0; position < frames; position += blockSize)
        {
            av_frame_unref (frame.get());
            frame->nb_samples = (int) std::min<juce::int64> (blockSize, frames - position);
            frame->format = encoder.sample_fmt;
            frame->sample_rate = encoder.sample_rate;
            frame->pts = position;
            av_channel_layout_copy (&frame->ch_layout, &encoder.ch_layout);
            av_frame_get_buffer (frame.get(), 0);
            fillSine (*frame, position);
            avcodec_send_frame (&encoder, frame.get());
            drain();
        }
        avcodec_send_frame (&encoder, nullptr);
        drain();
        av_write_trailer (out);
        avio_closep (&out->pb);
        avformat_free_context (out);
        return file;
    }

    // A stereo 24-bit FLAC at compression level 0, which the writer's encoder (default level) never
    // produces: packets equal to these were copied, not re-encoded.
    juce::File writeTestFlac (const juce::File& dir, const juce::String& name, int rate, juce::int64 frames,
                              int blockSize)
    {
        const auto* codec = avcodec_find_encoder (AV_CODEC_ID_FLAC);
        av::Codec encoder { avcodec_alloc_context3 (codec) };
        encoder->sample_fmt = AV_SAMPLE_FMT_S32;
        encoder->bits_per_raw_sample = 24;
        encoder->sample_rate = rate;
        encoder->time_base = { 1, rate };
        encoder->frame_size = blockSize;
        encoder->compression_level = 0;
        av_channel_layout_default (&encoder->ch_layout, 2);
        if (avcodec_open2 (encoder.get(), codec, nullptr) < 0)
            return {};
        return encodeSine (dir.getChildFile (name), "flac", *encoder, frames);
    }

    // A stereo MP3 through libmp3lame, whose muxer writes the LAME header with the encoder delay and
    // padding (gapless); an invalid File when no such encoder is built in.
    juce::File writeTestMp3 (const juce::File& dir, const juce::String& name, int rate, juce::int64 frames)
    {
        const auto* codec = avcodec_find_encoder_by_name ("libmp3lame");
        if (codec == nullptr)
            return {};
        av::Codec encoder { avcodec_alloc_context3 (codec) };
        encoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
        encoder->sample_rate = rate;
        encoder->time_base = { 1, rate };
        encoder->bit_rate = 192000;
        av_channel_layout_default (&encoder->ch_layout, 2);
        if (avcodec_open2 (encoder.get(), codec, nullptr) < 0)
            return {};
        return encodeSine (dir.getChildFile (name), "mp3", *encoder, frames);
    }
}

TEST (Writer, WritesFiveAudioTracksInOrder)
{
    TempDir t;
    const auto r = requestFor (t, 44100, 24, 10000);
    const auto result = writeTrack (r);
    ASSERT_TRUE (result.ok) << result.error;
    const auto audio = audioStreamsOf (r.output);
    ASSERT_EQ (audio.size(), 5u);
    const char* names[] = { "Original", "Stem 1", "Stem 2", "Stem 3", "Stem 4" };
    for (size_t i = 0; i < 5; ++i)
    {
        EXPECT_EQ (audio[i].title, names[i]);
        EXPECT_EQ (audio[i].isDefault, i == 0) << i;
    }
}

TEST (Writer, StemsAreFlacAndOriginalIsCopied)
{
    TempDir t;
    const auto r = requestFor (t, 44100, 24, 10000);
    ASSERT_TRUE (writeTrack (r).ok);
    const auto audio = audioStreamsOf (r.output);
    ASSERT_EQ (audio.size(), 5u);
    EXPECT_EQ (audio[0].codec, probeAudio (r.original)->codecName.toStdString());
    EXPECT_EQ (audio[0].codec, "pcm_s24le");
    for (size_t i = 1; i < 5; ++i)
        EXPECT_EQ (audio[i].codec, "flac") << i;
    EXPECT_EQ (decodeAll (r.output, 0), decodeAll (r.original, 0));
}

TEST (Writer, AudioSurvivesBitExactInKeepMode)
{
    for (const int bits : { 16, 24 })
    {
        TempDir t;
        const auto r = requestFor (t, 44100, bits, 10000);
        ASSERT_TRUE (writeTrack (r).ok) << bits;
        for (int s = 0; s < numStems; ++s)
        {
            const auto written = decodeAll (r.output, s + 1);
            ASSERT_EQ (written.size(), 2u);
            EXPECT_EQ (written, decodeAll (r.stems[(size_t) s], 0)) << bits << " stem " << s;
            EXPECT_EQ (probeAudio (r.output, s + 1)->bitDepth, bits) << s;
        }
    }
}

TEST (Writer, ConvertGivesFlac44k24ForAll)
{
    TempDir t;
    auto r = requestFor (t, 48000, 16, 24000);
    r.convert = ConvertTarget::flac44k24();
    const auto result = writeTrack (r);
    ASSERT_TRUE (result.ok) << result.error;
    const auto audio = audioStreamsOf (r.output);
    ASSERT_EQ (audio.size(), 5u);
    for (int i = 0; i < 5; ++i)
    {
        const auto info = probeAudio (r.output, i);
        ASSERT_TRUE (info.has_value());
        EXPECT_EQ (info->sampleRate, 44100) << i;
        EXPECT_EQ (info->bitDepth, 24) << i;
        EXPECT_EQ (info->codecName, "flac") << i;
        EXPECT_EQ (countFrames (r.output, i), juce::int64 (22050)) << i;
    }
}

TEST (Writer, UnequalLengthsAreRefusedAndNothingIsWritten)
{
    TempDir t;
    auto r = requestFor (t, 44100, 16, 10000);
    r.stems[2] = writeTestWav (t.dir, "long.wav", 44100, 2, 16, 10007, 550.0);
    const auto result = writeTrack (r);
    EXPECT_FALSE (result.ok);
    EXPECT_TRUE (result.error.contains ("10000")) << result.error;
    EXPECT_TRUE (result.error.contains ("10007")) << result.error;
    EXPECT_FALSE (r.output.exists());
    EXPECT_TRUE (noTemporaryIn (t.dir));
}

TEST (Writer, DifferentRatesAreRefusedWhenKeeping)
{
    TempDir t;
    auto r = requestFor (t, 44100, 16, 10000);
    r.stems[1] = writeTestWav (t.dir, "fast.wav", 48000, 2, 16, 10000, 330.0);
    const auto result = writeTrack (r);
    EXPECT_FALSE (result.ok);
    EXPECT_TRUE (result.error.contains ("48000")) << result.error;
    EXPECT_FALSE (r.output.exists());
}

TEST (Writer, AFailingVerifyLeavesTheOldFileUntouched)
{
    TempDir t;
    auto r = requestFor (t, 44100, 16, 10000);
    r.output.replaceWithText ("the old file");
    const auto before = bytesOf (r.output);
    r.stems[3] = t.dir.getChildFile ("missing.wav");
    const auto result = writeTrack (r);
    EXPECT_FALSE (result.ok);
    EXPECT_FALSE (result.error.isEmpty());
    EXPECT_EQ (bytesOf (r.output), before);
    EXPECT_TRUE (noTemporaryIn (t.dir));
}

// "<output>.tmp" may be the user's own file: the writer never touches it.
TEST (Writer, AnOwnTmpBesideTheOutputIsUntouched)
{
    TempDir t;
    auto r = requestFor (t, 44100, 16, 10000, 2, "song");
    const auto own = t.dir.getChildFile ("song.tmp");
    own.replaceWithText ("the user's own file");
    ASSERT_TRUE (writeTrack (r).ok);
    EXPECT_EQ (own.loadFileAsString(), "the user's own file");
    EXPECT_TRUE (noTemporaryIn (t.dir));

    r.stems[1] = t.dir.getChildFile ("missing.wav");
    EXPECT_FALSE (writeTrack (r).ok);
    EXPECT_EQ (own.loadFileAsString(), "the user's own file");
    EXPECT_TRUE (noTemporaryIn (t.dir));
}

TEST (Writer, AFailureAtTheFirstStepLeavesNoTemporary)
{
    TempDir t;
    auto r = requestFor (t, 44100, 16, 10000);
    r.original = t.dir.getChildFile ("missing.wav");
    EXPECT_FALSE (writeTrack (r).ok);
    EXPECT_TRUE (noTemporaryIn (t.dir));
    EXPECT_FALSE (r.output.exists());
}

// The file is complete and verified, and the last step still fails: the temporary file goes too.
TEST (Writer, AFailedRenameRemovesTheTmp)
{
    TempDir t;
    auto r = requestFor (t, 44100, 16, 10000);
    r.output = t.dir.getChildFile ("taken");
    ASSERT_TRUE (r.output.createDirectory());
    ASSERT_TRUE (r.output.getChildFile ("inside").replaceWithText ("a directory that is not empty"));
    const auto result = writeTrack (r);
    EXPECT_FALSE (result.ok);
    EXPECT_TRUE (result.error.contains ("rename")) << result.error;
    EXPECT_TRUE (r.output.getChildFile ("inside").existsAsFile());
    EXPECT_TRUE (noTemporaryIn (t.dir));
}

TEST (Writer, A3tJsonIsAnAttachmentAndParses)
{
    TempDir t;
    auto r = requestFor (t, 44100, 24, 10000);
    r.data.track.artist = "Someone";
    r.data.revision = 3;
    r.data.cues = { hotCue (0, 0.1, "drop") };
    ASSERT_TRUE (r.data.uuid.isEmpty());
    ASSERT_TRUE (writeTrack (r).ok);

    const auto streams = streamsOf (r.output);
    const auto* json = attachmentNamed (streams, "a3t.json");
    ASSERT_NE (json, nullptr);
    EXPECT_EQ (json->type, AVMEDIA_TYPE_ATTACHMENT);
    EXPECT_EQ (json->mimetype, "application/json");
    const auto loaded = parse (juce::String::fromUTF8 (json->bytes.data(), (int) json->bytes.size()));
    ASSERT_EQ (loaded.status, Status::ok);

    const auto& audio = loaded.data.track.audio;
    EXPECT_EQ (loaded.data.uuid.length(), 36);
    EXPECT_EQ (audio.sampleRate, 44100);
    EXPECT_EQ (audio.bitDepth, 24);
    EXPECT_EQ (audio.channels, 2);
    EXPECT_EQ (audio.codec, "pcm_s24le");
    EXPECT_NEAR (audio.lengthSeconds, 10000.0 / 44100.0, 1e-9);

    auto expected = r.data;
    expected.uuid = loaded.data.uuid;
    EXPECT_EQ (audio.source, "original.wav");
    expected.track.audio = audio;
    EXPECT_TRUE (loaded.data == expected);
}

TEST (Writer, AGivenUuidIsKept)
{
    TempDir t;
    auto r = requestFor (t, 44100, 16, 10000);
    r.data.uuid = "01234567-89ab-cdef-0123-456789abcdef";
    ASSERT_TRUE (writeTrack (r).ok);
    const auto streams = streamsOf (r.output);
    const auto* json = attachmentNamed (streams, "a3t.json");
    ASSERT_NE (json, nullptr);
    EXPECT_EQ (parse (juce::String::fromUTF8 (json->bytes.data(), (int) json->bytes.size())).data.uuid,
               r.data.uuid);
}

TEST (Writer, TagsAndChaptersAreWritten)
{
    TempDir t;
    auto r = requestFor (t, 44100, 16, 10000);
    r.data.cues = { hotCue (0, 0.05, "in"), hotCue (1, 0.1, "drop") };
    ASSERT_TRUE (writeTrack (r).ok);
    const auto format = av::openFormat (r.output);
    ASSERT_NE (format, nullptr);
    EXPECT_EQ (metadata (format->metadata, "TITLE"), "Test Title");
    ASSERT_EQ (format->nb_chapters, 2u);
    EXPECT_EQ (metadata (format->chapters[0]->metadata, "title"), "A: in");
    EXPECT_EQ (metadata (format->chapters[1]->metadata, "title"), "B: drop");
    EXPECT_NEAR (format->chapters[0]->start * av_q2d (format->chapters[0]->time_base), 0.05, 1e-6);
}

TEST (Writer, ChaptersStayInsideTheTrack)
{
    TempDir t;
    auto r = requestFor (t, 44100, 16, 10000);   // 0.2268 s
    r.data.cues = { hotCue (0, 0.05, "in"), hotCue (1, 5.0, "past the end"), hotCue (20, 0.2265, "odd slot") };
    const auto result = writeTrack (r);
    ASSERT_TRUE (result.ok) << result.error;
    const auto format = av::openFormat (r.output);
    ASSERT_NE (format, nullptr);
    ASSERT_EQ (format->nb_chapters, 2u);
    EXPECT_EQ (metadata (format->chapters[1]->metadata, "title"), "odd slot");
    for (unsigned i = 0; i < format->nb_chapters; ++i)
        EXPECT_LE (format->chapters[i]->end * av_q2d (format->chapters[i]->time_base), 10000.0 / 44100.0 + 1e-9);
}

TEST (Writer, MonoOriginalAnd96kAreAccepted)
{
    TempDir t;
    const auto r = requestFor (t, 96000, 24, 20000, 1);
    const auto result = writeTrack (r);
    ASSERT_TRUE (result.ok) << result.error;
    const auto original = probeAudio (r.output, 0);
    ASSERT_TRUE (original.has_value());
    EXPECT_EQ (original->channels, 1);
    EXPECT_EQ (original->sampleRate, 96000);
    for (int i = 1; i < 5; ++i)
        EXPECT_EQ (probeAudio (r.output, i)->sampleRate, 96000) << i;
    EXPECT_EQ (decodeAll (r.output, 0), decodeAll (r.original, 0));
}

TEST (Writer, OddOutputNamesWork)
{
    TempDir t;
    const auto r = requestFor (t, 44100, 16, 10000, 2, juce::String::fromUTF8 ("a b.c.d \xc3\xa9"));
    const auto result = writeTrack (r);
    ASSERT_TRUE (result.ok) << result.error;
    EXPECT_TRUE (r.output.existsAsFile());
    EXPECT_EQ (audioStreamsOf (r.output).size(), 5u);
    EXPECT_TRUE (noTemporaryIn (t.dir));
}

TEST (Writer, CoverIsAnAttachment)
{
    TempDir t;
    auto r = requestFor (t, 44100, 16, 10000);
    r.cover = t.dir.getChildFile ("folder art.png");
    r.cover.replaceWithData (tinyPng, sizeof (tinyPng));
    const auto result = writeTrack (r);
    ASSERT_TRUE (result.ok) << result.error;
    const auto streams = streamsOf (r.output);
    const auto* cover = attachmentNamed (streams, "cover.png");
    ASSERT_NE (cover, nullptr);
    EXPECT_EQ (cover->mimetype, "image/png");
    EXPECT_EQ (cover->bytes, std::string ((const char*) tinyPng, sizeof (tinyPng)));
    EXPECT_EQ (audioStreamsOf (r.output).size(), 5u);
}

TEST (Writer, ACoverThatIsNoImageIsRefused)
{
    TempDir t;
    auto r = requestFor (t, 44100, 16, 10000);
    r.cover = t.dir.getChildFile ("cover.jpg");
    r.cover.replaceWithText ("not a picture");
    EXPECT_FALSE (writeTrack (r).ok);
    EXPECT_FALSE (r.output.exists());
    EXPECT_TRUE (noTemporaryIn (t.dir));
}

// Stems are what StemDeck seeks in; FLAC in Matroska must land on the exact sample.
TEST (Writer, FlacStemsSeekSampleExactly)
{
    TempDir t;
    const auto r = requestFor (t, 44100, 24, 30000);
    ASSERT_TRUE (writeTrack (r).ok);
    // 3968 is one frame before a block that starts at exactly 90 ms; 29990 lies past Matroska's
    // millisecond-rounded duration (29988 frames) but inside the audio.
    expectSampleExactSeeks (r.output, 1, 30000, { 1, 3968, 3969, 7777, 29990, 30000 });
}

TEST (Writer, AFlacStemWithMillisecondBlocksIsCopied)
{
    for (const bool convert : { false, true })
    {
        TempDir t;
        auto r = requestFor (t, 44100, 24, 20000);
        r.stems[0] = writeTestFlac (t.dir, "stem.flac", 44100, 20000, 3969);   // 90 ms blocks
        if (convert)
            r.convert = ConvertTarget::flac44k24();
        const auto result = writeTrack (r);
        ASSERT_TRUE (result.ok) << result.error;
        EXPECT_EQ (packetsOf (r.output, 1, 5), packetsOf (r.stems[0], 0, 5)) << convert;
        EXPECT_EQ (decodeAll (r.output, 1), decodeAll (r.stems[0], 0)) << convert;
    }
}

// Blocks that start between two milliseconds would make the stem unseekable to the sample: re-encoded, losslessly.
TEST (Writer, AFlacStemWithOtherBlocksIsReEncoded)
{
    TempDir t;
    auto r = requestFor (t, 44100, 24, 20000);
    r.stems[0] = writeTestFlac (t.dir, "stem.flac", 44100, 20000, 4096);
    const auto result = writeTrack (r);
    ASSERT_TRUE (result.ok) << result.error;
    EXPECT_NE (packetsOf (r.output, 1, 5), packetsOf (r.stems[0], 0, 5));
    EXPECT_EQ (decodeAll (r.output, 1), decodeAll (r.stems[0], 0));
}

// The encoder delay and padding of a gapless MP3 cannot be copied into Matroska: the copied original
// would be longer than the stems. Refused, with advice to convert; conversion trims it and works.
TEST (Writer, AGaplessMp3OriginalIsRefusedWhenKeeping)
{
    TempDir t;
    const auto mp3 = writeTestMp3 (t.dir, "original.mp3", 44100, 88200);
    if (mp3 == juce::File())
        GTEST_SKIP() << "no libmp3lame encoder in this FFmpeg build";
    const auto frames = countFrames (mp3);
    ASSERT_TRUE (frames.has_value());
    auto r = requestFor (t, 44100, 16, *frames);
    r.original = mp3;
    r.output.replaceWithText ("the old file");
    const auto before = bytesOf (r.output);

    const auto result = writeTrack (r);
    EXPECT_FALSE (result.ok);
    EXPECT_TRUE (result.error.contains ("conversion")) << result.error;
    EXPECT_TRUE (result.error.contains ("mp3")) << result.error;
    EXPECT_EQ (bytesOf (r.output), before);
    EXPECT_TRUE (noTemporaryIn (t.dir));

    r.convert = ConvertTarget::flac44k24();
    const auto converted = writeTrack (r);
    ASSERT_TRUE (converted.ok) << converted.error;
    EXPECT_EQ (countFrames (r.output, 0), frames);
}

// The same file with the up-front check off: a complete temporary file reaches the verification, is
// rejected there with the same advice, and is removed; the old target keeps its bytes.
TEST (Writer, VerificationRejectsACompleteFileAndGivesTheSameAdvice)
{
    TempDir t;
    const auto mp3 = writeTestMp3 (t.dir, "original.mp3", 44100, 88200);
    if (mp3 == juce::File())
        GTEST_SKIP() << "no libmp3lame encoder in this FFmpeg build";
    auto r = requestFor (t, 44100, 16, countFrames (mp3).value_or (0));
    r.original = mp3;
    r.output.replaceWithText ("the old file");
    const auto before = bytesOf (r.output);

    const a3t::testing::UpFrontLossyCheckOff off;
    const auto result = writeTrack (r);
    EXPECT_FALSE (result.ok);
    EXPECT_TRUE (result.error.startsWith ("verification failed")) << result.error;
    EXPECT_TRUE (result.error.contains ("conversion")) << result.error;
    EXPECT_EQ (bytesOf (r.output), before);
    EXPECT_TRUE (noTemporaryIn (t.dir));
}

TEST (Writer, AacAndMp2OriginalsAreStillKept)
{
    for (const auto* name : { "aac", "mp2" })
    {
        TempDir t;
        const auto* codec = avcodec_find_encoder_by_name (name);
        ASSERT_NE (codec, nullptr) << name;
        av::Codec encoder { avcodec_alloc_context3 (codec) };
        encoder->sample_fmt = codec->id == AV_CODEC_ID_AAC ? AV_SAMPLE_FMT_FLTP : AV_SAMPLE_FMT_S16;
        encoder->sample_rate = 44100;
        encoder->time_base = { 1, 44100 };
        encoder->bit_rate = 192000;
        av_channel_layout_default (&encoder->ch_layout, 2);
        ASSERT_GE (avcodec_open2 (encoder.get(), codec, nullptr), 0) << name;
        const auto original = encodeSine (t.dir.getChildFile (juce::String ("original.") + (codec->id == AV_CODEC_ID_AAC ? "m4a" : "mp2")),
                                          codec->id == AV_CODEC_ID_AAC ? "ipod" : "mp2", *encoder, 88200);
        ASSERT_NE (original, juce::File()) << name;
        auto r = requestFor (t, 44100, 16, countFrames (original).value_or (0));
        r.original = original;
        const auto result = writeTrack (r);
        EXPECT_TRUE (result.ok) << name << ": " << result.error;
    }
}
