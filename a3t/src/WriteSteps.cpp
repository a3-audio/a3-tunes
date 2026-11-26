// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "WriteSteps.h"

#include "MkvMuxer.h"
#include "a3t/Derived.h"
#include "a3t/Json.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace a3t::writing
{
const std::array<juce::String, numTracks> trackNames { "Original", "Stem 1", "Stem 2", "Stem 3", "Stem 4" };

TemporaryFile::TemporaryFile (const juce::File& target)
{
    constexpr int attempts = 16;
    for (int i = 0; i < attempts && ! created; ++i)
    {
        const auto random = juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
        const juce::File candidate (target.getFullPathName() + "." + random + ".a3tmp");
        const auto fd = ::open (candidate.getFullPathName().toRawUTF8(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd >= 0)
        {
            ::close (fd);
            file = candidate;
            created = true;
        }
        else if (errno != EEXIST)
            return;
    }
}

TemporaryFile::~TemporaryFile()
{
    if (created)
        file.deleteFile();
}

// On disk first, then rename(2), which replaces the target in one step (JUCE's moveFileTo
// deletes it first). FAT, as on most USB sticks, is not atomic about it anyway.
juce::String TemporaryFile::moveTo (const juce::File& target)
{
    struct stat existing {};
    const bool targetExists = ::stat (target.getFullPathName().toRawUTF8(), &existing) == 0;
    if (! flushToDisk (targetExists ? &existing : nullptr))
        return "cannot flush " + file.getFullPathName() + ": " + juce::String (std::strerror (errno));
    if (std::rename (file.getFullPathName().toRawUTF8(), target.getFullPathName().toRawUTF8()) != 0)
        return "cannot rename into place: " + juce::String (std::strerror (errno));
    created = false;
    return {};
}

bool TemporaryFile::flushToDisk (const struct stat* keep) const
{
    const auto fd = ::open (file.getFullPathName().toRawUTF8(), O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    if (keep != nullptr)
    {
        // Owner first: changing it can clear set-id bits that the mode then restores. Only root may
        // give a file away, so a failed chown is not an error.
        if (::fchown (fd, keep->st_uid, keep->st_gid) != 0) {}
        if (::fchmod (fd, keep->st_mode & 07777) != 0)
        {
            ::close (fd);
            return false;
        }
    }
    const auto synced = ::fsync (fd) == 0;
    return ::close (fd) == 0 && synced;
}

juce::File resolvedTarget (const juce::File& target)
{
    char* real = ::realpath (target.getFullPathName().toRawUTF8(), nullptr);
    if (real == nullptr)
        return target;
    const juce::File resolved (juce::String::fromUTF8 (real));
    std::free (real);
    return resolved;
}

juce::String cannotCreateTemporary (const juce::File& target)
{
    return "cannot create a temporary file beside " + target.getFullPathName() + ": " + juce::String (std::strerror (errno));
}

void setMeasuredFacts (AudioInfo& audio, const SourceInfo& info, juce::int64 frames)
{
    audio.sampleRate = info.sampleRate;
    audio.bitDepth = info.bitDepth;
    audio.channels = info.channels;
    audio.bitrate = info.bitrate;
    audio.codec = info.codecName;
    audio.lengthSeconds = info.sampleRate > 0 ? (double) frames / info.sampleRate : 0.0;
}

juce::String lossyCopyAdvice (const juce::String& codec, juce::int64 copiedFrames, juce::int64 stemFrames)
{
    return "the original (" + codec + ") decodes to " + juce::String (copiedFrames)
           + " frames once copied into Matroska, the stems to " + juce::String (stemFrames)
           + " (encoder delay and padding cannot be copied); write with conversion (ConvertTarget::flac44k24())";
}

WriteResult failure (const juce::String& error)
{
    return { false, error };
}

namespace
{
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

    juce::String addMetadata (MkvMuxer& muxer, const A3tData& data, const std::vector<Attachment>& attachments,
                              double lengthSeconds)
    {
        const auto json = serialise (data).toStdString();
        if (! muxer.addAttachment ("a3t.json", "application/json", juce::MemoryBlock (json.data(), json.size())))
            return muxer.error();
        for (const auto& a : attachments)
            if (! muxer.addAttachment (a.filename, a.mimetype, a.bytes))
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
                            const std::vector<Attachment>& attachments, double lengthSeconds)
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
        if (const auto error = addMetadata (muxer, data, attachments, lengthSeconds); error.isNotEmpty())
            return error;
        if (! muxer.start())
            return muxer.error();
        if (const auto error = interleave (muxer, sources, streams); error.isNotEmpty())
            return error;
        return muxer.finish() ? juce::String() : muxer.error();
    }

    juce::String checkTrack (const AVStream& stream, size_t i)
    {
        if (av::metadata (stream.metadata, "title") != trackNames[i])
            return "track " + juce::String (i + 1) + " is not named " + trackNames[i];
        if (((stream.disposition & AV_DISPOSITION_DEFAULT) != 0) != (i == 0))
            return trackNames[i] + " has the wrong default flag";
        if (i > 0 && stream.codecpar->codec_id != AV_CODEC_ID_FLAC)
            return trackNames[i] + " is not FLAC";
        return {};
    }

    juce::String checkJson (const AVStream* stream, const A3tData& data)
    {
        if (stream == nullptr)
            return "a3t.json is missing";
        const auto& p = *stream->codecpar;
        const auto text = juce::String::fromUTF8 ((const char*) p.extradata, p.extradata_size);
        if (parse (text).status != Status::ok)
            return "a3t.json does not parse";
        return text == serialise (data) ? juce::String() : "a3t.json is not what was written";
    }

    // Every track as long as the target; a copied original that alone differs gets the conversion advice.
    juce::String checkLengths (const juce::File& file, juce::int64 frames, const std::optional<juce::String>& copiedCodec)
    {
        std::array<juce::int64, numTracks> counted {};
        for (int i = 0; i < numTracks; ++i)
            counted[(size_t) i] = countFrames (file, i).value_or (-1);
        const auto stemsRight = std::all_of (counted.begin() + 1, counted.end(),
                                             [frames] (juce::int64 n) { return n == frames; });
        if (copiedCodec && stemsRight && counted[0] != frames && counted[0] >= 0)
            return lossyCopyAdvice (*copiedCodec, counted[0], frames);
        for (size_t i = 0; i < counted.size(); ++i)
            if (counted[i] != frames)
                return trackNames[i] + " is " + juce::String (counted[i]) + " frames long, not "
                       + juce::String (frames);
        return {};
    }

    // Five named audio tracks in order, flagged and coded as written; `json` is the a3t.json stream, if any.
    juce::String checkStreams (const AVFormatContext& format, const AVStream*& json)
    {
        std::vector<const AVStream*> audio;
        json = nullptr;
        for (unsigned i = 0; i < format.nb_streams; ++i)
        {
            const auto* stream = format.streams[i];
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                audio.push_back (stream);
            else if (av::metadata (stream->metadata, "filename") == "a3t.json")
                json = stream;
        }
        if (audio.size() != (size_t) numTracks)
            return "the file has " + juce::String ((int) audio.size()) + " audio tracks, not 5";
        for (size_t i = 0; i < audio.size(); ++i)
            if (const auto error = checkTrack (*audio[i], i); error.isNotEmpty())
                return error;
        return {};
    }

    // What a reader relies on: five named audio tracks in order, equally long, and a3t.json.
    juce::String verify (const juce::File& file, juce::int64 frames, const A3tData& data,
                         const std::optional<juce::String>& copiedCodec)
    {
        const auto format = av::openFormat (file);
        if (format == nullptr)
            return "the written file cannot be opened";
        const AVStream* json = nullptr;
        if (const auto error = checkStreams (*format, json); error.isNotEmpty())
            return error;
        if (const auto error = checkJson (json, data); error.isNotEmpty())
            return error;
        return checkLengths (file, frames, copiedCodec);
    }

    juce::String checkStoredJson (const AVStream* stream)
    {
        if (stream == nullptr)
            return "a3t.json is missing";
        const auto& p = *stream->codecpar;
        switch (parse (juce::String::fromUTF8 ((const char*) p.extradata, p.extradata_size)).status)
        {
            case Status::ok:    return {};
            case Status::newer: return "a3t.json is from a newer version of the format";
            default:            return "a3t.json does not parse";
        }
    }
}

WriteResult verifyExisting (const juce::File& file)
{
    const auto format = av::openFormat (file);
    if (format == nullptr)
        return failure ("not a readable Matroska file: " + file.getFullPathName());
    const AVStream* json = nullptr;
    if (const auto error = checkStreams (*format, json); error.isNotEmpty())
        return failure (error);
    if (const auto error = checkStoredJson (json); error.isNotEmpty())
        return failure (error);
    const auto frames = countFrames (file, 0);
    if (! frames)
        return failure ("the original cannot be decoded");
    if (const auto error = checkLengths (file, *frames, std::nullopt); error.isNotEmpty())
        return failure (error);
    return { true, {} };
}

namespace
{
}

WriteResult writeVerified (TemporaryFile& temporary, const juce::File& target, Sources& sources, const A3tData& data,
                           const std::vector<Attachment>& attachments, juce::int64 frames, int rate,
                           const std::optional<juce::String>& copiedCodec)
{
    // A reader keeps maxEvents; more would be written, verified against the same text, and cut on the next read.
    if (data.events.size() > (size_t) maxEvents)
        return failure (juce::String ((juce::int64) data.events.size()) + " events; the cap is " + juce::String (maxEvents));
    if (const auto error = writeFile (temporary.path(), sources, data, attachments, (double) frames / rate);
        error.isNotEmpty())
        return failure (error);
    if (const auto error = verify (temporary.path(), frames, data, copiedCodec); error.isNotEmpty())
        return failure ("verification failed: " + error);
    if (const auto error = temporary.moveTo (target); error.isNotEmpty())
        return failure (error);
    return { true, {} };
}
}
