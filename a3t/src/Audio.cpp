// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Audio.h"
#include "AvPtr.h"

#include <algorithm>
#include <vector>

namespace a3t
{
namespace
{
    int bitDepthOf (const AVCodecParameters& p)
    {
        if (p.bits_per_raw_sample > 0)
            return p.bits_per_raw_sample;
        if (p.bits_per_coded_sample > 0)
            return p.bits_per_coded_sample;
        return av_get_bytes_per_sample ((AVSampleFormat) p.format) * 8;
    }
}

struct TrackDecoder::Impl
{
    av::Format format;
    av::Codec codec;
    av::Packet packet { av_packet_alloc() };
    av::Frame frame { av_frame_alloc() };
    av::Swr swr;
    int streamIndex = -1, rate = 0, numChannels = 0;
    juce::int64 length = 0;

    std::vector<std::vector<float>> pending;   // decoded, not yet handed out
    size_t pendingStart = 0;
    juce::int64 skip = 0;                      // frames to drop to reach a seek target
    bool aligned = true;                       // false after a seek until the first frame's position is known
    juce::int64 target = 0;
    bool flushed = false;

    bool init (const juce::File& file, int track)
    {
        format = av::openFormat (file);
        if (format == nullptr || packet == nullptr || frame == nullptr)
            return false;
        streamIndex = av::audioStreamIndex (*format, track);
        if (streamIndex < 0)
            return false;
        for (unsigned i = 0; i < format->nb_streams; ++i)
            format->streams[i]->discard = (int) i == streamIndex ? AVDISCARD_DEFAULT : AVDISCARD_ALL;

        const auto& stream = *format->streams[streamIndex];
        const auto* decoder = avcodec_find_decoder (stream.codecpar->codec_id);
        codec.reset (decoder != nullptr ? avcodec_alloc_context3 (decoder) : nullptr);
        if (codec == nullptr || avcodec_parameters_to_context (codec.get(), stream.codecpar) < 0
            || avcodec_open2 (codec.get(), decoder, nullptr) < 0)
            return false;
        rate = stream.codecpar->sample_rate;
        numChannels = stream.codecpar->ch_layout.nb_channels;
        if (rate <= 0 || numChannels <= 0)
            return false;
        pending.assign ((size_t) numChannels, {});
        length = av::declaredLength (*format, stream);
        return makeResampler();
    }

    // Always through swr, even at the same rate: it is what turns any sample format into planar float.
    bool makeResampler()
    {
        AVChannelLayout layout;
        if (av_channel_layout_copy (&layout, &codec->ch_layout) < 0)
            return false;
        SwrContext* raw = nullptr;
        const auto result = swr_alloc_set_opts2 (&raw, &layout, AV_SAMPLE_FMT_FLTP, rate, &codec->ch_layout,
                                                 codec->sample_fmt, rate, 0, nullptr);
        av_channel_layout_uninit (&layout);
        swr.reset (raw);
        return result >= 0 && swr_init (swr.get()) >= 0;
    }

    int bufferedFrames() const { return (int) (pending[0].size() - pendingStart); }

    void append (const std::vector<std::vector<float>>& chunk, int count)
    {
        if (pendingStart > 0 && pendingStart == pending[0].size())
        {
            for (auto& c : pending)
                c.clear();
            pendingStart = 0;
        }
        for (int c = 0; c < numChannels; ++c)
            pending[(size_t) c].insert (pending[(size_t) c].end(), chunk[(size_t) c].begin(),
                                        chunk[(size_t) c].begin() + count);
    }

    // Converts `in` (null = flush) and stores what comes out, minus the frames a seek is skipping.
    void convert (const AVFrame* in)
    {
        const auto capacity = std::max (swr_get_out_samples (swr.get(), in != nullptr ? in->nb_samples : 0), 1);
        std::vector<std::vector<float>> out ((size_t) numChannels, std::vector<float> ((size_t) capacity));
        std::vector<uint8_t*> planes;
        for (auto& c : out)
            planes.push_back (reinterpret_cast<uint8_t*> (c.data()));
        const auto got = swr_convert (swr.get(), planes.data(), capacity,
                                      in != nullptr ? const_cast<const uint8_t**> (in->extended_data) : nullptr,
                                      in != nullptr ? in->nb_samples : 0);
        if (got <= 0)
            return;
        const auto dropped = (int) std::min<juce::int64> (skip, got);
        skip -= dropped;
        if (dropped >= got)
            return;
        for (auto& c : out)
            c.erase (c.begin(), c.begin() + dropped);
        append (out, got - dropped);
    }

    // Where a decoded frame starts, in samples from the stream's start; -1 if it carries no timestamp.
    juce::int64 positionOf (const AVFrame& f) const
    {
        if (f.pts == AV_NOPTS_VALUE)
            return -1;
        const auto& stream = *format->streams[streamIndex];
        const auto start = stream.start_time == AV_NOPTS_VALUE ? 0 : stream.start_time;
        return av_rescale_q (f.pts - start, stream.time_base, AVRational { 1, rate });
    }

    void onDecodedFrame()
    {
        if (! aligned)
        {
            aligned = true;
            const auto position = positionOf (*frame);
            skip = position >= 0 ? std::max<juce::int64> (target - position, 0) : target;
        }
        convert (frame.get());
    }

    // Drains the decoder; false once it needs another packet or is finished.
    void receiveFrames()
    {
        while (avcodec_receive_frame (codec.get(), frame.get()) >= 0)
        {
            onDecodedFrame();
            av_frame_unref (frame.get());
        }
    }

    void finish()
    {
        if (flushed)
            return;
        flushed = true;
        avcodec_send_packet (codec.get(), nullptr);
        receiveFrames();
        convert (nullptr);
    }

    // One more packet's worth of audio into `pending`; false when nothing more will come.
    bool decodeSome()
    {
        if (flushed)
            return false;
        while (av_read_frame (format.get(), packet.get()) >= 0)
        {
            const auto mine = packet->stream_index == streamIndex;
            const auto sent = mine && avcodec_send_packet (codec.get(), packet.get()) >= 0;
            av_packet_unref (packet.get());
            if (! sent)
                continue;
            receiveFrames();
            return true;
        }
        finish();
        return true;
    }

    bool seekTo (juce::int64 frameIndex)
    {
        const auto& stream = *format->streams[streamIndex];
        const auto start = stream.start_time == AV_NOPTS_VALUE ? 0 : stream.start_time;
        const auto ts = start + av_rescale_q (frameIndex, AVRational { 1, rate }, stream.time_base);
        if (av_seek_frame (format.get(), streamIndex, ts, AVSEEK_FLAG_BACKWARD) < 0)
            return false;
        avcodec_flush_buffers (codec.get());
        for (auto& c : pending)
            c.clear();
        pendingStart = 0;
        flushed = false;
        target = frameIndex;
        skip = 0;
        aligned = false;
        return makeResampler();
    }

    int read (float* const* out, int numFrames)
    {
        while (bufferedFrames() < numFrames && decodeSome()) {}
        const auto n = std::min (numFrames, bufferedFrames());
        for (int c = 0; c < numChannels; ++c)
            std::copy_n (pending[(size_t) c].begin() + (long) pendingStart, n, out[c]);
        pendingStart += (size_t) n;
        return n;
    }

    juce::int64 countFrames()
    {
        juce::int64 total = 0;
        std::vector<std::vector<float>> block ((size_t) numChannels, std::vector<float> (4096));
        std::vector<float*> planes;
        for (auto& b : block)
            planes.push_back (b.data());
        for (int n; (n = read (planes.data(), 4096)) > 0;)
            total += n;
        return total;
    }
};

TrackDecoder::TrackDecoder (std::unique_ptr<Impl> i) : impl (std::move (i)) {}
TrackDecoder::~TrackDecoder() = default;

std::unique_ptr<TrackDecoder> TrackDecoder::open (const juce::File& file, int audioTrack)
{
    auto impl = std::make_unique<Impl>();
    if (! impl->init (file, audioTrack))
        return nullptr;
    std::unique_ptr<TrackDecoder> decoder (new TrackDecoder (std::move (impl)));
    // A container that does not state its length (some raw PCM): count it, then go back to the start.
    if (decoder->impl->length <= 0)
    {
        decoder->impl->length = decoder->impl->countFrames();
        if (! decoder->seek (0))
            return nullptr;
    }
    return decoder;
}

int TrackDecoder::sampleRate() const { return impl->rate; }
int TrackDecoder::channels() const { return impl->numChannels; }
juce::int64 TrackDecoder::lengthFrames() const { return impl->length; }
bool TrackDecoder::seek (juce::int64 frame) { return impl->seekTo (std::max<juce::int64> (frame, 0)); }
int TrackDecoder::read (float* const* channelData, int numFrames) { return impl->read (channelData, numFrames); }

std::optional<SourceInfo> probeAudio (const juce::File& file, int audioTrack)
{
    const auto format = av::openFormat (file);
    if (format == nullptr)
        return std::nullopt;
    const auto index = av::audioStreamIndex (*format, audioTrack);
    if (index < 0)
        return std::nullopt;

    const auto& parameters = *format->streams[index]->codecpar;
    SourceInfo info;
    info.sampleRate = parameters.sample_rate;
    info.channels = parameters.ch_layout.nb_channels;
    info.bitDepth = bitDepthOf (parameters);
    info.codecName = avcodec_get_name (parameters.codec_id);
    info.bitrate = parameters.bit_rate > 0 ? parameters.bit_rate : format->bit_rate;
    info.lengthFrames = av::declaredLength (*format, *format->streams[index]);
    if (info.lengthFrames <= 0)
    {
        const auto decoder = TrackDecoder::open (file, audioTrack);
        if (decoder == nullptr)
            return std::nullopt;
        info.lengthFrames = decoder->lengthFrames();
    }
    return info;
}
}
