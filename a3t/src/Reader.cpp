// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Reader.h"

#include "AvPtr.h"
#include "a3t/Audio.h"
#include "a3t/Sidecar.h"

#include <cmath>

namespace a3t
{
    namespace
    {
        using av::audioStreamIndex;
        using av::declaredLength;

        constexpr int fiveTracks = 1 + numStems;

        bool isMatroska (const AVFormatContext& format)
        {
            return format.iformat != nullptr && juce::String (format.iformat->name).contains ("matroska");
        }

        juce::String attachedJson (const AVFormatContext& format)
        {
            for (unsigned i = 0; i < format.nb_streams; ++i)
            {
                const auto& stream = *format.streams[i];
                if (stream.codecpar->codec_type != AVMEDIA_TYPE_ATTACHMENT
                    || av::metadata (stream.metadata, "filename") != "a3t.json")
                    continue;
                const auto& p = *stream.codecpar;
                return juce::String::fromUTF8 ((const char*) p.extradata, p.extradata_size);
            }
            return {};
        }

        int countAudioTracks (const AVFormatContext& format)
        {
            int count = 0;
            while (audioStreamIndex (format, count) >= 0)
                ++count;
            return count;
        }

        // a3t.json's number is trusted only if it agrees with the container's own (millisecond) duration.
        bool agreesWithContainer (juce::int64 frames, juce::int64 declared, int rate)
        {
            return frames > 0 && std::llabs (frames - declared) <= rate / 100 + 2;
        }

        juce::int64 exactLength (const juce::File& file, const AVStream& stream, const AVFormatContext& format,
                                 const A3tData* data)
        {
            const auto rate = stream.codecpar->sample_rate;
            if (data != nullptr)
            {
                const auto fromJson = lengthFramesAt (data->track.audio, rate);
                if (agreesWithContainer (fromJson, declaredLength (format, stream), rate))
                    return fromJson;
            }
            return countFrames (file, 0).value_or (declaredLength (format, stream));
        }

        TrackLayout layoutOf (const juce::File& file, const AVFormatContext& format, const A3tData* data)
        {
            TrackLayout layout;
            layout.audioTracks = countAudioTracks (format);
            layout.isFiveTrack = layout.audioTracks == fiveTracks;
            const auto& stream = *format.streams[audioStreamIndex (format, 0)];
            layout.sampleRate = stream.codecpar->sample_rate;
            layout.lengthFrames = exactLength (file, stream, format, data);
            return layout;
        }
    }

    OpenResult openTrack (const juce::File& file)
    {
        OpenResult result;
        try
        {
            if (! file.existsAsFile())
                return result;
            const auto format = av::openFormat (file);
            if (format == nullptr || ! isMatroska (*format) || audioStreamIndex (*format, 0) < 0)
                return result;

            auto loaded = parse (attachedJson (*format));
            const auto usable = loaded.status == Status::ok;
            result.status = loaded.status == Status::none ? Status::damaged : loaded.status;
            if (usable)
                result.data = std::move (loaded.data);
            result.layout = layoutOf (file, *format, usable ? &result.data : nullptr);
        }
        catch (...)
        {
            return {};
        }
        return result;
    }

    namespace
    {
        // The sidecar named by the file's name if it is ours, else the one that names our uuid.
        std::optional<juce::File> ownSidecar (const juce::File& file, const A3tData& data, juce::String& error)
        {
            const auto beside = sidecarFileFor (file);
            if (beside.existsAsFile())
            {
                const auto loaded = loadSidecar (beside);
                if (loaded.status != Status::ok)
                {
                    error = "the sidecar " + beside.getFileName() + " is unreadable or newer";
                    return std::nullopt;
                }
                if (belongsTo (loaded.sidecar, data))
                    return beside;
                error = "the sidecar " + beside.getFileName() + " belongs to another track";
            }
            if (auto byUuid = findSidecarByUuid (file.getParentDirectory(), data.uuid))
            {
                error.clear();
                return byUuid;
            }
            return std::nullopt;
        }
    }

    OpenResult openTrackWithSidecar (const juce::File& file)
    {
        auto result = openTrack (file);
        if (result.status != Status::ok)
            return result;
        const auto found = ownSidecar (file, result.data, result.error);
        if (! found.has_value())
            return result;
        const auto loaded = loadSidecar (*found);
        if (loaded.status == Status::ok && belongsTo (loaded.sidecar, result.data))
        {
            result.data = overlay (result.data, loaded.sidecar);
            result.appliedSidecar = *found;
        }
        return result;
    }
}
