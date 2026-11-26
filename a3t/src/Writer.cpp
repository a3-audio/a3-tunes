// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Writer.h"

#include "a3t/Audio.h"
#include "a3t/Derived.h"
#include "a3t/Json.h"
#include "MkvMuxer.h"
#include "TrackSource.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <optional>

namespace a3t
{
namespace
{
    constexpr int numTracks = numStems + 1;
    const std::array<juce::String, numTracks> trackNames { "Original", "Stem 1", "Stem 2", "Stem 3", "Stem 4" };

    struct Input
    {
        juce::File file;
        SourceInfo info;
        juce::int64 frames = 0;   // counted by decoding: a declared duration can be rounded
    };
    using Inputs = std::array<Input, numTracks>;
    using Sources = std::array<std::unique_ptr<TrackSource>, numTracks>;

    struct Cover
    {
        juce::String filename, mimetype;
        juce::MemoryBlock bytes;
    };

    // Deletes the temporary file unless it has been renamed into place.
    struct TemporaryFile
    {
        explicit TemporaryFile (juce::File f) : file (std::move (f)) { file.deleteFile(); }
        ~TemporaryFile() { file.deleteFile(); }
        juce::File file;
    };

    juce::String readInputs (const WriteRequest& request, Inputs& inputs)
    {
        for (int i = 0; i < numTracks; ++i)
        {
            const auto& file = i == 0 ? request.original : request.stems[(size_t) i - 1];
            const auto info = probeAudio (file);
            const auto frames = info ? countFrames (file) : std::nullopt;
            if (! frames)
                return "cannot read " + trackNames[(size_t) i] + ": " + file.getFullPathName();
            inputs[(size_t) i] = { file, *info, *frames };
        }
        return {};
    }

    juce::int64 framesAt (const Input& in, int rate)
    {
        return av_rescale_rnd (in.frames, rate, in.info.sampleRate, AV_ROUND_NEAR_INF);
    }

    juce::String describe (const Inputs& inputs)
    {
        juce::StringArray parts;
        for (size_t i = 0; i < inputs.size(); ++i)
            parts.add (trackNames[i] + " " + juce::String (inputs[i].frames) + " frames at "
                       + juce::String (inputs[i].info.sampleRate) + " Hz");
        return parts.joinIntoString (", ");
    }

    // Tracks at one rate must match frame for frame; tracks at different rates (conversion only)
    // must match once both are at the target rate.
    bool lengthsAgree (const Input& a, const Input& b, int targetRate)
    {
        if (a.info.sampleRate == b.info.sampleRate)
            return a.frames == b.frames;
        return framesAt (a, targetRate) == framesAt (b, targetRate);
    }

    // The length every written track gets, or an error naming all five.
    juce::String commonLength (const Inputs& inputs, const ConvertTarget& convert, juce::int64& frames)
    {
        const auto& first = inputs[0];
        for (const auto& in : inputs)
            if (convert.keep && in.info.sampleRate != first.info.sampleRate)
                return "sample rates differ (convert to one rate, or give stems at the original's): " + describe (inputs);
        for (size_t i = 0; i < inputs.size(); ++i)
            for (size_t j = i + 1; j < inputs.size(); ++j)
                if (! lengthsAgree (inputs[i], inputs[j], convert.sampleRate))
                    return "track lengths differ: " + describe (inputs);
        frames = convert.keep ? first.frames : framesAt (first, convert.sampleRate);
        return {};
    }

    juce::String loadCover (const juce::File& file, std::optional<Cover>& cover)
    {
        if (file == juce::File())
            return {};
        juce::MemoryBlock bytes;
        if (! file.loadFileAsData (bytes))
            return "cannot read the cover: " + file.getFullPathName();
        const auto* b = static_cast<const unsigned char*> (bytes.getData());
        const auto size = bytes.getSize();
        if (size >= 8 && std::memcmp (b, "\x89PNG\r\n\x1a\n", 8) == 0)
            cover = Cover { "cover.png", "image/png", bytes };
        else if (size >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff)
            cover = Cover { "cover.jpg", "image/jpeg", bytes };
        else
            return "the cover is neither JPEG nor PNG: " + file.getFullPathName();
        return {};
    }

    // What a3t.json says about the file itself: its identity and the original as it came in.
    A3tData withFileFacts (A3tData data, const Input& original)
    {
        if (data.uuid.isEmpty())
            data.uuid = newUuid();
        auto& audio = data.track.audio;
        audio.sampleRate = original.info.sampleRate;
        audio.bitDepth = original.info.bitDepth;
        audio.channels = original.info.channels;
        audio.bitrate = original.info.bitrate;
        audio.codec = original.info.codecName;
        audio.lengthSeconds = (double) original.frames / original.info.sampleRate;
        if (audio.source.isEmpty())
            audio.source = original.file.getFileName();
        return data;
    }

    // FLAC keeps 16 bit as 16; anything deeper is stored as 24.
    int keptDepth (int bits)
    {
        return bits <= 16 ? 16 : 24;
    }

    std::unique_ptr<TrackSource> sourceFor (const Input& in, bool isOriginal, const ConvertTarget& convert,
                                            juce::int64 frames)
    {
        if (isOriginal && convert.keep)
            return copyPackets (in.file);
        const auto rate = convert.keep ? in.info.sampleRate : convert.sampleRate;
        const auto depth = convert.keep ? keptDepth (in.info.bitDepth) : convert.bitDepth;
        const auto atTarget = convert.keep || (in.info.sampleRate == rate && in.info.bitDepth == depth);
        if (in.info.codecName == "flac" && atTarget && hasMillisecondFlacBlocks (in.file))
            return copyPackets (in.file);
        return encodeFlac (in.file, rate, depth, frames);
    }

    juce::String openSources (const Inputs& inputs, const ConvertTarget& convert, juce::int64 frames,
                              Sources& sources)
    {
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            sources[i] = sourceFor (inputs[i], i == 0, convert, frames);
            if (sources[i] == nullptr)
                return "cannot prepare " + trackNames[i] + " from " + inputs[i].file.getFullPathName();
        }
        return {};
    }

    // Packets go to the muxer in time order across the five tracks, so it never holds more than a few.
    juce::String interleave (MkvMuxer& muxer, Sources& sources, const std::array<int, numTracks>& streams)
    {
        std::array<av::Packet, numTracks> packets;
        std::array<bool, numTracks> pending {};
        const auto refill = [&] (size_t i)
        {
            pending[i] = sources[i]->next (*packets[i]);
            return ! sources[i]->failed();
        };
        for (size_t i = 0; i < packets.size(); ++i)
        {
            packets[i].reset (av_packet_alloc());
            if (packets[i] == nullptr || ! refill (i))
                return "cannot read " + trackNames[i];
        }
        for (;;)
        {
            int earliest = -1;
            for (int i = 0; i < numTracks; ++i)
                if (pending[(size_t) i]
                    && (earliest < 0
                        || av_compare_ts (packets[(size_t) i]->pts, sources[(size_t) i]->timeBase(),
                                          packets[(size_t) earliest]->pts, sources[(size_t) earliest]->timeBase())
                               < 0))
                    earliest = i;
            if (earliest < 0)
                return {};
            const auto e = (size_t) earliest;
            if (! muxer.write (*packets[e], streams[e], sources[e]->timeBase()))
                return muxer.error();
            if (! refill (e))
                return "cannot read " + trackNames[e];
        }
    }

    juce::String addMetadata (MkvMuxer& muxer, const A3tData& data, const std::optional<Cover>& cover,
                              double lengthSeconds)
    {
        const auto json = serialise (data).toStdString();
        if (! muxer.addAttachment ("a3t.json", "application/json", juce::MemoryBlock (json.data(), json.size())))
            return muxer.error();
        if (cover && ! muxer.addAttachment (cover->filename, cover->mimetype, cover->bytes))
            return muxer.error();
        const auto chapters = chaptersFor (data, lengthSeconds);
        for (size_t i = 0; i < chapters.size(); ++i)
            if (! muxer.addChapter ((juce::int64) i + 1, chapters[i].start, chapters[i].end, chapters[i].title))
                return muxer.error();
        for (const auto& [key, value] : tagsFor (data))
            if (! muxer.setTag (key, value))
                return muxer.error();
        return {};
    }

    juce::String writeFile (const juce::File& file, Sources& sources, const A3tData& data,
                            const std::optional<Cover>& cover, double lengthSeconds)
    {
        MkvMuxer muxer;
        if (! muxer.create (file))
            return muxer.error();
        std::array<int, numTracks> streams {};
        for (size_t i = 0; i < sources.size(); ++i)
        {
            streams[i] = muxer.addAudio (sources[i]->parameters(), sources[i]->timeBase(), trackNames[i], i == 0);
            if (streams[i] < 0)
                return muxer.error();
        }
        if (const auto error = addMetadata (muxer, data, cover, lengthSeconds); error.isNotEmpty())
            return error;
        if (! muxer.start())
            return muxer.error();
        if (const auto error = interleave (muxer, sources, streams); error.isNotEmpty())
            return error;
        return muxer.finish() ? juce::String() : muxer.error();
    }

    juce::String metadataOf (const AVDictionary* dictionary, const char* key)
    {
        const auto* entry = av_dict_get (dictionary, key, nullptr, 0);
        return entry != nullptr ? juce::String::fromUTF8 (entry->value) : juce::String();
    }

    juce::String checkTrack (const AVStream& stream, size_t i)
    {
        if (metadataOf (stream.metadata, "title") != trackNames[i])
            return "track " + juce::String (i + 1) + " is not named " + trackNames[i];
        if (((stream.disposition & AV_DISPOSITION_DEFAULT) != 0) != (i == 0))
            return trackNames[i] + " has the wrong default flag";
        if (i > 0 && stream.codecpar->codec_id != AV_CODEC_ID_FLAC)
            return trackNames[i] + " is not FLAC";
        return {};
    }

    juce::String checkJson (const AVStream* stream)
    {
        if (stream == nullptr)
            return "a3t.json is missing";
        const auto& p = *stream->codecpar;
        const auto text = juce::String::fromUTF8 ((const char*) p.extradata, p.extradata_size);
        return parse (text).status == Status::ok ? juce::String() : "a3t.json does not parse";
    }

    // What a reader relies on: five named audio tracks in order, equally long, and a3t.json.
    juce::String verify (const juce::File& file, juce::int64 frames)
    {
        const auto format = av::openFormat (file);
        if (format == nullptr)
            return "the written file cannot be opened";
        std::vector<const AVStream*> audio;
        const AVStream* json = nullptr;
        for (unsigned i = 0; i < format->nb_streams; ++i)
        {
            const auto* stream = format->streams[i];
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                audio.push_back (stream);
            else if (metadataOf (stream->metadata, "filename") == "a3t.json")
                json = stream;
        }
        if (audio.size() != (size_t) numTracks)
            return "the written file has " + juce::String ((int) audio.size()) + " audio tracks, not 5";
        for (size_t i = 0; i < audio.size(); ++i)
            if (const auto error = checkTrack (*audio[i], i); error.isNotEmpty())
                return error;
        if (const auto error = checkJson (json); error.isNotEmpty())
            return error;
        for (int i = 0; i < numTracks; ++i)
            if (const auto counted = countFrames (file, i); counted != frames)
                return trackNames[(size_t) i] + " was written " + juce::String (counted.value_or (-1))
                       + " frames long, not " + juce::String (frames);
        return {};
    }

    juce::String checkRequest (const WriteRequest& request)
    {
        if (request.output == juce::File())
            return "no output file given";
        const auto& c = request.convert;
        if (! c.keep && (c.sampleRate <= 0 || (c.bitDepth != 16 && c.bitDepth != 24)))
            return "conversion needs a positive rate and 16 or 24 bit";
        return {};
    }

    WriteResult failure (const juce::String& error)
    {
        return { false, error };
    }
}

WriteResult writeTrack (const WriteRequest& request)
{
    if (const auto error = checkRequest (request); error.isNotEmpty())
        return failure (error);
    Inputs inputs;
    if (const auto error = readInputs (request, inputs); error.isNotEmpty())
        return failure (error);
    juce::int64 frames = 0;
    if (const auto error = commonLength (inputs, request.convert, frames); error.isNotEmpty())
        return failure (error);
    std::optional<Cover> cover;
    if (const auto error = loadCover (request.cover, cover); error.isNotEmpty())
        return failure (error);

    Sources sources;
    if (const auto error = openSources (inputs, request.convert, frames, sources); error.isNotEmpty())
        return failure (error);
    const auto rate = request.convert.keep ? inputs[0].info.sampleRate : request.convert.sampleRate;
    const auto data = withFileFacts (request.data, inputs[0]);

    const TemporaryFile temporary (juce::File (request.output.getFullPathName() + ".tmp"));
    if (const auto error = writeFile (temporary.file, sources, data, cover, (double) frames / rate);
        error.isNotEmpty())
        return failure (error);
    if (const auto error = verify (temporary.file, frames); error.isNotEmpty())
        return failure ("verification failed: " + error);
    // rename(2) replaces the target in one step; JUCE's moveFileTo deletes it first.
    if (std::rename (temporary.file.getFullPathName().toRawUTF8(), request.output.getFullPathName().toRawUTF8()) != 0)
        return failure ("cannot rename into place: " + juce::String (std::strerror (errno)));
    return { true, {} };
}
}
