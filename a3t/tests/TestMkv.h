// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Looking inside written .a3mkv files: streams, attachments, packets, decoded samples.
#pragma once
#include "a3t/Audio.h"
#include "a3t/Model.h"
#include "AvPtr.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using Channels = std::vector<std::vector<float>>;

// The writer's temporary files are <output>.<random>.a3tmp beside the output.
inline bool noTemporaryIn (const juce::File& dir)
{
    return dir.findChildFiles (juce::File::findFiles, false, "*.a3tmp").isEmpty();
}

inline Channels decodeAll (const juce::File& file, int track)
{
    const auto d = a3t::TrackDecoder::open (file, track);
    if (d == nullptr)
        return {};
    Channels all ((size_t) d->channels());
    Channels block ((size_t) d->channels(), std::vector<float> (1000));
    std::vector<float*> planes;
    for (auto& b : block)
        planes.push_back (b.data());
    for (int n; (n = d->read (planes.data(), 1000)) > 0;)
        for (size_t c = 0; c < all.size(); ++c)
            all[c].insert (all[c].end(), block[c].begin(), block[c].begin() + n);
    return all;
}

struct StreamView
{
    AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
    std::string codec, title, filename, mimetype, bytes;
    bool isDefault = false;
};

inline std::string metadata (const AVDictionary* dict, const char* key)
{
    const auto* entry = av_dict_get (dict, key, nullptr, 0);
    return entry != nullptr ? entry->value : "";
}

inline std::vector<StreamView> streamsOf (const juce::File& file)
{
    const auto format = a3t::av::openFormat (file);
    std::vector<StreamView> views;
    if (format == nullptr)
        return views;
    for (unsigned i = 0; i < format->nb_streams; ++i)
    {
        const auto& s = *format->streams[i];
        StreamView v;
        v.type = s.codecpar->codec_type;
        v.codec = avcodec_get_name (s.codecpar->codec_id);
        v.title = metadata (s.metadata, "title");
        v.filename = metadata (s.metadata, "filename");
        v.mimetype = metadata (s.metadata, "mimetype");
        v.isDefault = (s.disposition & AV_DISPOSITION_DEFAULT) != 0;
        if (s.codecpar->extradata_size > 0)
            v.bytes.assign ((const char*) s.codecpar->extradata, (size_t) s.codecpar->extradata_size);
        else if (s.attached_pic.size > 0)
            v.bytes.assign ((const char*) s.attached_pic.data, (size_t) s.attached_pic.size);
        views.push_back (std::move (v));
    }
    return views;
}

inline std::vector<StreamView> audioStreamsOf (const juce::File& file)
{
    std::vector<StreamView> audio;
    for (auto& v : streamsOf (file))
        if (v.type == AVMEDIA_TYPE_AUDIO)
            audio.push_back (v);
    return audio;
}

inline const StreamView* attachmentNamed (const std::vector<StreamView>& streams, const std::string& name)
{
    for (const auto& v : streams)
        if (v.filename == name)
            return &v;
    return nullptr;
}

// The first `count` packets of one audio track, as raw bytes.
inline std::vector<std::string> packetsOf (const juce::File& file, int track, int count)
{
    const auto format = a3t::av::openFormat (file);
    std::vector<std::string> packets;
    if (format == nullptr)
        return packets;
    const auto index = a3t::av::audioStreamIndex (*format, track);
    a3t::av::Packet packet { av_packet_alloc() };
    while ((int) packets.size() < count && av_read_frame (format.get(), packet.get()) >= 0)
    {
        if (packet->stream_index == index)
            packets.emplace_back ((const char*) packet->data, (size_t) packet->size);
        av_packet_unref (packet.get());
    }
    return packets;
}

inline juce::MemoryBlock bytesOf (const juce::File& f)
{
    juce::MemoryBlock block;
    f.loadFileAsData (block);
    return block;
}

inline a3t::Cue hotCue (int slot, double time, const char* name)
{
    a3t::Cue c;
    c.kind = a3t::Cue::Kind::hot;
    c.slot = slot;
    c.time = time;
    c.name = name;
    return c;
}

// a3t.json sits in the file as plain text, so a same-length replacement edits it in place.
inline void patchBytes (const juce::File& file, const juce::String& from, const juce::String& to)
{
    ASSERT_EQ (from.length(), to.length());
    juce::MemoryBlock bytes;
    ASSERT_TRUE (file.loadFileAsData (bytes));
    const auto needle = from.toStdString();
    const std::string text ((const char*) bytes.getData(), bytes.getSize());
    const auto at = text.find (needle);
    ASSERT_NE (at, std::string::npos);
    bytes.copyFrom (to.toRawUTF8(), (int) at, (size_t) to.getNumBytesAsUTF8());
    ASSERT_TRUE (file.replaceWithData (bytes.getData(), bytes.getSize()));
}

// Stems are what StemDeck seeks in: every seek on a stereo `track` of `frames` lands on the exact sample.
inline void expectSampleExactSeeks (const juce::File& file, int track, juce::int64 frames,
                                    std::initializer_list<juce::int64> targets)
{
    const auto straight = decodeAll (file, track);
    ASSERT_EQ (straight.size(), 2u);
    ASSERT_EQ (straight[0].size(), (size_t) frames);
    for (const auto frame : targets)
    {
        auto d = a3t::TrackDecoder::open (file, track);
        ASSERT_NE (d, nullptr);
        ASSERT_TRUE (d->seek (frame)) << frame;
        std::vector<float> left (64), right (64);
        float* planes[] = { left.data(), right.data() };
        const auto n = d->read (planes, 64);
        ASSERT_EQ (n, (int) std::min<juce::int64> (64, frames - frame)) << frame;
        ASSERT_FALSE (d->failed());
        for (int i = 0; i < n; ++i)
        {
            EXPECT_EQ (left[(size_t) i], straight[0][(size_t) (frame + i)]) << frame << "+" << i;
            EXPECT_EQ (right[(size_t) i], straight[1][(size_t) (frame + i)]) << frame << "+" << i;
        }
    }
    auto d = a3t::TrackDecoder::open (file, track);
    ASSERT_NE (d, nullptr);
    EXPECT_FALSE (d->seek (frames + 1));
}

constexpr unsigned char tinyPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00,
    0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x00, 0x00, 0x03, 0x01,
    0x01, 0x00, 0xc9, 0xfe, 0x92, 0xef, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
    0x82
};
