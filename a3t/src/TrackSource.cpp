// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrackSource.h"

#include "a3t/Audio.h"

extern "C"
{
#include <libavutil/audio_fifo.h>
#include <libavutil/intreadwrite.h>
}

#include <algorithm>
#include <cstring>
#include <numeric>
#include <vector>

namespace a3t
{
namespace
{
    struct FifoFree       { void operator() (AVAudioFifo* f) const       { av_audio_fifo_free (f); } };
    struct ParametersFree { void operator() (AVCodecParameters* p) const { avcodec_parameters_free (&p); } };
    using Fifo = std::unique_ptr<AVAudioFifo, FifoFree>;
    using Parameters = std::unique_ptr<AVCodecParameters, ParametersFree>;

    // FLAC's streamable subset allows blocks up to 4608 samples at 48 kHz and below.
    constexpr int preferredBlock = 4096;
    constexpr int largestFlacBlock = 65535;
    constexpr int decodeBlock = 4096;

    class PacketCopy final : public TrackSource
    {
    public:
        bool init (const juce::File& file, int audioTrack)
        {
            format = av::openFormat (file);
            if (format == nullptr || packet == nullptr)
                return false;
            index = av::audioStreamIndex (*format, audioTrack);
            if (index < 0)
                return false;
            for (unsigned i = 0; i < format->nb_streams; ++i)
                format->streams[i]->discard = (int) i == index ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
            const auto& stream = *format->streams[index];
            start = stream.start_time == AV_NOPTS_VALUE ? 0 : stream.start_time;
            return true;
        }

        const AVCodecParameters& parameters() const override { return *format->streams[index]->codecpar; }
        AVRational timeBase() const override { return format->streams[index]->time_base; }

        bool next (AVPacket& out) override
        {
            int result = 0;
            while ((result = av_read_frame (format.get(), packet.get())) >= 0)
            {
                if (packet->stream_index != index)
                {
                    av_packet_unref (packet.get());
                    continue;
                }
                av_packet_move_ref (&out, packet.get());
                stamp (out);
                return true;
            }
            broken = result != AVERROR_EOF;
            return false;
        }

    private:
        // Every track of the file starts at 0; a packet without a timestamp follows the one before.
        void stamp (AVPacket& p)
        {
            p.pts = p.pts == AV_NOPTS_VALUE ? end : p.pts - start;
            p.dts = p.pts;
            end = p.pts + p.duration;
            // Every audio packet starts a decodable unit; Matroska indexes only key packets.
            p.flags |= AV_PKT_FLAG_KEY;
        }

        av::Format format;
        av::Packet packet { av_packet_alloc() };
        int index = -1;
        juce::int64 start = 0, end = 0;
    };

    class FlacEncode final : public TrackSource
    {
    public:
        bool init (const juce::File& file, int rate, int bitDepth, juce::int64 frames)
        {
            decoder = TrackDecoder::open (file, 0);
            if (decoder == nullptr || frame == nullptr || (bitDepth != 16 && bitDepth != 24))
                return false;
            target = frames;
            channels = decoder->channels();
            sampleFormat = bitDepth == 16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S32;
            av_channel_layout_default (&layout, channels);
            return openEncoder (rate, bitDepth) && openResampler() && openFifo();
        }

        ~FlacEncode() override { av_channel_layout_uninit (&layout); }

        const AVCodecParameters& parameters() const override { return *codecParameters; }
        AVRational timeBase() const override { return encoder->time_base; }

        bool next (AVPacket& out) override
        {
            for (;;)
            {
                const auto result = avcodec_receive_packet (encoder.get(), &out);
                if (result >= 0)
                {
                    out.flags |= AV_PKT_FLAG_KEY;
                    return true;
                }
                if (result == AVERROR_EOF)
                    return false;
                if (result != AVERROR (EAGAIN) || ! feedEncoder())
                {
                    broken = true;
                    return false;
                }
            }
        }

    private:
        bool openEncoder (int rate, int bitDepth)
        {
            const auto* codec = avcodec_find_encoder (AV_CODEC_ID_FLAC);
            encoder.reset (codec != nullptr ? avcodec_alloc_context3 (codec) : nullptr);
            codecParameters.reset (avcodec_parameters_alloc());
            if (encoder == nullptr || codecParameters == nullptr)
                return false;
            encoder->sample_fmt = sampleFormat;
            encoder->bits_per_raw_sample = bitDepth;
            encoder->sample_rate = rate;
            encoder->time_base = { 1, rate };
            encoder->frame_size = millisecondBlockSize (rate);   // 0 leaves the choice to the encoder
            if (av_channel_layout_copy (&encoder->ch_layout, &layout) < 0
                || avcodec_open2 (encoder.get(), codec, nullptr) < 0)
                return false;
            return avcodec_parameters_from_context (codecParameters.get(), encoder.get()) >= 0;
        }

        // From the decoder's planar float to the encoder's integers: exact when the depth is kept,
        // since float holds every 16- and 24-bit value. Same layout in and out, so nothing is remixed.
        bool openResampler()
        {
            SwrContext* raw = nullptr;
            const auto result = swr_alloc_set_opts2 (&raw, &layout, sampleFormat, encoder->sample_rate, &layout,
                                                     AV_SAMPLE_FMT_FLTP, decoder->sampleRate(), 0, nullptr);
            swr.reset (raw);
            return result >= 0 && swr_init (swr.get()) >= 0;
        }

        bool openFifo()
        {
            fifo.reset (av_audio_fifo_alloc (sampleFormat, channels, decodeBlock * 4));
            return fifo != nullptr;
        }

        int blockSize() const { return encoder->frame_size > 0 ? encoder->frame_size : preferredBlock; }

        bool feedEncoder()
        {
            while (! inputDone && av_audio_fifo_size (fifo.get()) < blockSize())
                if (! pullInput())
                    return false;
            const auto available = av_audio_fifo_size (fifo.get());
            if (available == 0)
                return avcodec_send_frame (encoder.get(), nullptr) >= 0;
            return sendFrame (std::min (available, blockSize()));
        }

        bool sendFrame (int count)
        {
            av_frame_unref (frame.get());
            frame->nb_samples = count;
            frame->format = sampleFormat;
            frame->sample_rate = encoder->sample_rate;
            frame->pts = sent;
            if (av_channel_layout_copy (&frame->ch_layout, &layout) < 0 || av_frame_get_buffer (frame.get(), 0) < 0
                || av_audio_fifo_read (fifo.get(), (void**) frame->data, count) != count)
                return false;
            sent += count;
            return avcodec_send_frame (encoder.get(), frame.get()) >= 0;
        }

        bool pullInput()
        {
            std::vector<std::vector<float>> block ((size_t) channels, std::vector<float> (decodeBlock));
            std::vector<float*> planes;
            for (auto& b : block)
                planes.push_back (b.data());
            const auto n = decoder->read (planes.data(), decodeBlock);
            if (decoder->failed())
                return false;
            if (! convert ((const uint8_t**) planes.data(), n))
                return false;
            if (n > 0)
                return true;
            inputDone = true;
            return convert (nullptr, 0) && padToTarget();   // the resampler's tail, then silence to the length
        }

        bool convert (const uint8_t** in, int count)
        {
            const auto capacity = std::max (swr_get_out_samples (swr.get(), count), 1);
            uint8_t** out = nullptr;
            if (av_samples_alloc_array_and_samples (&out, nullptr, channels, capacity, sampleFormat, 0) < 0)
                return false;
            const auto got = swr_convert (swr.get(), out, capacity, in, count);
            const auto ok = got >= 0 && store ((void**) out, got);
            av_freep (&out[0]);
            av_freep (&out);
            return ok;
        }

        // Only up to the target length; whatever the resampler gives beyond it is dropped.
        bool store (void** data, int count)
        {
            const auto wanted = (int) std::min<juce::int64> (count, target - queued);
            if (wanted <= 0)
                return true;
            if (av_audio_fifo_write (fifo.get(), data, wanted) != wanted)
                return false;
            queued += wanted;
            return true;
        }

        bool padToTarget()
        {
            while (queued < target)
            {
                const auto count = (int) std::min<juce::int64> (target - queued, decodeBlock);
                uint8_t** silence = nullptr;
                if (av_samples_alloc_array_and_samples (&silence, nullptr, channels, count, sampleFormat, 0) < 0)
                    return false;
                av_samples_set_silence (silence, 0, count, channels, sampleFormat);
                const auto ok = store ((void**) silence, count);
                av_freep (&silence[0]);
                av_freep (&silence);
                if (! ok)
                    return false;
            }
            return true;
        }

        std::unique_ptr<TrackDecoder> decoder;
        av::Codec encoder;
        Parameters codecParameters;
        av::Swr swr;
        Fifo fifo;
        av::Frame frame { av_frame_alloc() };
        AVChannelLayout layout {};
        AVSampleFormat sampleFormat = AV_SAMPLE_FMT_S32;
        int channels = 0;
        juce::int64 target = 0, queued = 0, sent = 0;
        bool inputDone = false;
    };

    // Min and max block size from FLAC's STREAMINFO, which libav keeps bare or behind "fLaC" and a block header.
    std::pair<int, int> blockSizesOf (const AVCodecParameters& p)
    {
        const auto* bytes = p.extradata;
        auto size = p.extradata_size;
        if (size >= 8 && std::memcmp (bytes, "fLaC", 4) == 0)
        {
            bytes += 8;
            size -= 8;
        }
        if (size < 4)
            return { 0, 0 };
        return { (bytes[0] << 8) | bytes[1], (bytes[2] << 8) | bytes[3] };
    }
}

std::unique_ptr<TrackSource> copyPackets (const juce::File& file, int audioTrack)
{
    auto source = std::make_unique<PacketCopy>();
    return source->init (file, audioTrack) ? std::move (source) : nullptr;
}

std::unique_ptr<TrackSource> encodeFlac (const juce::File& file, int sampleRate, int bitDepth, juce::int64 frames)
{
    auto source = std::make_unique<FlacEncode>();
    return source->init (file, sampleRate, bitDepth, frames) ? std::move (source) : nullptr;
}

int millisecondBlockSize (int sampleRate)
{
    if (sampleRate <= 0)
        return 0;
    const auto perWholeMillisecond = sampleRate / std::gcd (sampleRate, 1000);
    if (perWholeMillisecond > largestFlacBlock)
        return 0;
    return perWholeMillisecond * std::max (1, preferredBlock / perWholeMillisecond);
}

bool hasMillisecondFlacBlocks (const juce::File& file)
{
    const auto format = av::openFormat (file);
    if (format == nullptr)
        return false;
    const auto index = av::audioStreamIndex (*format, 0);
    if (index < 0)
        return false;
    const auto& parameters = *format->streams[index]->codecpar;
    if (parameters.codec_id != AV_CODEC_ID_FLAC || parameters.sample_rate <= 0)
        return false;
    const auto [smallest, largest] = blockSizesOf (parameters);
    return smallest == largest && largest > 0 && (juce::int64) largest * 1000 % parameters.sample_rate == 0;
}

juce::int64 startTrimLostByCopy (const juce::File& file)
{
    // The skip sits on the first packet or two; a handful is plenty.
    constexpr int packetsToLookAt = 16;
    const auto format = av::openFormat (file);
    if (format == nullptr)
        return 0;
    const auto index = av::audioStreamIndex (*format, 0);
    if (index < 0)
        return 0;
    av::Packet packet { av_packet_alloc() };
    juce::int64 skipped = 0;
    for (int seen = 0; seen < packetsToLookAt && av_read_frame (format.get(), packet.get()) >= 0;)
    {
        if (packet->stream_index == index)
        {
            size_t size = 0;
            const auto* skip = av_packet_get_side_data (packet.get(), AV_PKT_DATA_SKIP_SAMPLES, &size);
            if (skip != nullptr && size >= 4)
                skipped += AV_RL32 (skip);
            ++seen;
        }
        av_packet_unref (packet.get());
    }
    return std::max<juce::int64> (skipped - format->streams[index]->codecpar->initial_padding, 0);
}
}
