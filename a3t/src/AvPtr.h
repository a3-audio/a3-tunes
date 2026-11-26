// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Internal: RAII handles for libav objects and the two lookups every reader needs.
#pragma once
#include <juce_core/juce_core.h>

#include <memory>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace a3t::av
{
struct FormatCloser { void operator() (AVFormatContext* c) const { avformat_close_input (&c); } };
struct CodecCloser  { void operator() (AVCodecContext* c) const  { avcodec_free_context (&c); } };
struct PacketFree   { void operator() (AVPacket* p) const        { av_packet_free (&p); } };
struct FrameFree    { void operator() (AVFrame* f) const         { av_frame_free (&f); } };
struct SwrFree      { void operator() (SwrContext* s) const      { swr_free (&s); } };

using Format = std::unique_ptr<AVFormatContext, FormatCloser>;
using Codec = std::unique_ptr<AVCodecContext, CodecCloser>;
using Packet = std::unique_ptr<AVPacket, PacketFree>;
using Frame = std::unique_ptr<AVFrame, FrameFree>;
using Swr = std::unique_ptr<SwrContext, SwrFree>;

// Opens a container and reads its stream info; empty on failure.
inline Format openFormat (const juce::File& file)
{
    // Containers make libav warn about harmless things on every open; only errors may speak.
    static const bool quiet = [] { av_log_set_level (AV_LOG_ERROR); return true; }();
    juce::ignoreUnused (quiet);
    AVFormatContext* context = nullptr;
    if (avformat_open_input (&context, file.getFullPathName().toRawUTF8(), nullptr, nullptr) < 0)
        return {};
    Format format (context);
    if (avformat_find_stream_info (context, nullptr) < 0)
        return {};
    return format;
}

// The stream index of the `track`-th audio stream (attachments and video do not count); -1 if none.
inline int audioStreamIndex (const AVFormatContext& format, int track)
{
    if (track < 0)
        return -1;
    int audio = 0;
    for (unsigned i = 0; i < format.nb_streams; ++i)
        if (format.streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio++ == track)
            return (int) i;
    return -1;
}

// Samples in the stream: its own duration, else the container's; 0 if neither is known.
inline juce::int64 declaredLength (const AVFormatContext& format, const AVStream& stream)
{
    const auto rate = stream.codecpar->sample_rate;
    if (rate <= 0)
        return 0;
    if (stream.duration > 0)
        return av_rescale_q_rnd (stream.duration, stream.time_base, AVRational { 1, rate }, AV_ROUND_NEAR_INF);
    return format.duration > 0 ? av_rescale_rnd (format.duration, rate, AV_TIME_BASE, AV_ROUND_NEAR_INF) : 0;
}
}
