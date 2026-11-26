// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Writer.h"

#include "WriteSteps.h"
#include "a3t/Reader.h"

#include <algorithm>

namespace a3t
{
namespace
{
    using namespace writing;

    // A rewrite never repairs: only a file whose a3t.json reads cleanly, as this very track, is rewritten.
    juce::String checkRewritable (const OpenResult& existing, const A3tData& data)
    {
        switch (existing.status)
        {
            case Status::none:    return "not a readable Matroska file with audio";
            case Status::damaged: return "its a3t.json is missing or damaged; a rewrite does not repair it";
            case Status::newer:   return "its a3t.json is from a newer version of the format";
            case Status::ok:      break;
        }
        if (! existing.layout.isFiveTrack)
            return "it has " + juce::String (existing.layout.audioTracks) + " audio tracks, not 5";
        if (data.uuid != existing.data.uuid)
            return "the data belongs to another track (uuid " + data.uuid + ", the file's is "
                   + existing.data.uuid + ")";
        return {};
    }

    juce::String metadataOf (const AVDictionary* dictionary, const char* key)
    {
        const auto* entry = av_dict_get (dictionary, key, nullptr, 0);
        return entry != nullptr ? juce::String::fromUTF8 (entry->value) : juce::String();
    }

    // libav hands an image attachment (the cover) out as a picture stream, anything else as extradata.
    juce::MemoryBlock attachedBytes (const AVStream& stream)
    {
        const auto& p = *stream.codecpar;
        if (p.extradata_size > 0)
            return { p.extradata, (size_t) p.extradata_size };
        return { stream.attached_pic.data, (size_t) std::max (stream.attached_pic.size, 0) };
    }

    // Every attachment except a3t.json, which is written anew. Any other kind of stream would be lost
    // by the copy, so such a file is refused.
    juce::String readAttachments (const juce::File& file, std::vector<Attachment>& attachments)
    {
        const auto format = av::openFormat (file);
        if (format == nullptr)
            return "cannot open " + file.getFullPathName();
        for (unsigned i = 0; i < format->nb_streams; ++i)
        {
            const auto& stream = *format->streams[i];
            if (stream.codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                continue;
            const auto filename = metadataOf (stream.metadata, "filename");
            if (filename.isEmpty())
                return "stream " + juce::String (i) + " is neither audio nor an attachment; a rewrite would drop it";
            if (filename != "a3t.json")
                attachments.push_back ({ filename, metadataOf (stream.metadata, "mimetype"), attachedBytes (stream) });
        }
        return {};
    }

    juce::String openCopies (const juce::File& file, Sources& sources)
    {
        for (size_t i = 0; i < sources.size(); ++i)
        {
            sources[i] = copyPackets (file, (int) i);
            if (sources[i] == nullptr)
                return "cannot read " + trackNames[i] + " of " + file.getFullPathName();
        }
        return {};
    }

    // a3t.json records the original as the writer measured it, before any conversion; that record stays
    // while it agrees with the file to the frame. Otherwise the file's own first track is measured.
    juce::String fileFacts (const juce::File& file, const AudioInfo& recorded, juce::int64 frames, int rate,
                            AudioInfo& facts)
    {
        facts = recorded;
        if (lengthFramesAt (recorded, rate) == frames)
            return {};
        const auto info = probeAudio (file, 0);
        if (! info)
            return "cannot read the original of " + file.getFullPathName();
        setMeasuredFacts (facts, *info, frames);
        return {};
    }
}

WriteResult rewriteMetadata (const juce::File& a3mkv, const A3tData& data)
{
    const auto existing = openTrack (a3mkv);
    if (const auto error = checkRewritable (existing, data); error.isNotEmpty())
        return failure ("cannot rewrite " + a3mkv.getFullPathName() + ": " + error);
    std::vector<Attachment> attachments;
    if (const auto error = readAttachments (a3mkv, attachments); error.isNotEmpty())
        return failure (error);
    const auto frames = countFrames (a3mkv, 0);
    if (! frames)
        return failure ("cannot count the frames of " + a3mkv.getFullPathName());

    TemporaryFile temporary (a3mkv);
    if (! temporary.exists())
        return failure (cannotCreateTemporary (a3mkv));
    Sources sources;
    if (const auto error = openCopies (a3mkv, sources); error.isNotEmpty())
        return failure (error);
    const auto rate = sources[0]->parameters().sample_rate;
    auto written = data;
    if (const auto error = fileFacts (a3mkv, existing.data.track.audio, *frames, rate, written.track.audio);
        error.isNotEmpty())
        return failure (error);
    return writeVerified (temporary, a3mkv, sources, written, attachments, *frames, rate, std::nullopt);
}
}
