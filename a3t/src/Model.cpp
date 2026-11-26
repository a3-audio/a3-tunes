// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Model.h"

#include <algorithm>
#include <cmath>

namespace a3t
{
    namespace
    {
        bool same (const juce::var& a, const juce::var& b)
        {
            return juce::JSON::toString (a, true) == juce::JSON::toString (b, true);
        }

        bool same (const Key& a, const Key& b)
        {
            return a.camelot == b.camelot && a.musical == b.musical && a.confidence == b.confidence;
        }

        bool same (const Grid& a, const Grid& b)
        {
            return a.firstBeat == b.firstBeat && a.bpm == b.bpm && a.beatsPerBar == b.beatsPerBar
                && a.gridVersion == b.gridVersion && a.corrected == b.corrected;
        }

        bool same (const Loudness& a, const Loudness& b)
        {
            return a.lufs == b.lufs && a.peakDb == b.peakDb;
        }

        bool same (const AudioInfo& a, const AudioInfo& b)
        {
            return a.sampleRate == b.sampleRate && a.bitDepth == b.bitDepth && a.channels == b.channels
                && a.bitrate == b.bitrate && a.codec == b.codec && a.source == b.source
                && a.lengthSeconds == b.lengthSeconds;
        }

        bool sameStrings (const TrackInfo& a, const TrackInfo& b)
        {
            return a.title == b.title && a.artist == b.artist && a.album == b.album
                && a.albumArtist == b.albumArtist && a.genre == b.genre && a.label == b.label
                && a.remixer == b.remixer && a.composer == b.composer && a.lyricist == b.lyricist
                && a.comment == b.comment && a.isrc == b.isrc && a.colour == b.colour
                && a.dateAdded == b.dateAdded && a.sourceService == b.sourceService
                && a.sourceId == b.sourceId;
        }

        bool same (const TrackInfo& a, const TrackInfo& b)
        {
            return sameStrings (a, b) && a.year == b.year && a.trackNo == b.trackNo
                && a.discNo == b.discNo && a.rating == b.rating && a.playCount == b.playCount
                && a.gainDb == b.gainDb && a.myTags == b.myTags && same (a.audio, b.audio)
                && same (a.trackExtra, b.trackExtra);
        }

        bool same (const Cue& a, const Cue& b)
        {
            return a.kind == b.kind && a.slot == b.slot && a.time == b.time
                && a.colour == b.colour && a.name == b.name;
        }

        bool same (const Loop& a, const Loop& b)
        {
            return a.slot == b.slot && a.in == b.in && a.out == b.out && a.colour == b.colour
                && a.name == b.name && a.active == b.active;
        }

        bool same (const Phrase& a, const Phrase& b)
        {
            return a.kind == b.kind && a.mood == b.mood && a.start == b.start && a.end == b.end
                && a.label == b.label;
        }

        bool same (const StemLabel& a, const StemLabel& b)
        {
            return a.label == b.label && a.colour == b.colour;
        }

        bool same (const Event& a, const Event& b) { return a == b; }

        template <typename T>
        bool same (const std::optional<T>& a, const std::optional<T>& b)
        {
            if (a.has_value() != b.has_value())
                return false;
            return ! a || same (*a, *b);
        }

        bool same (const std::optional<double>& a, const std::optional<double>& b)
        {
            return a == b;
        }

        template <typename Seq>
        bool sameAll (const Seq& a, const Seq& b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (! same (a[i], b[i]))
                    return false;
            return true;
        }

        bool same (const Analysis& a, const Analysis& b)
        {
            return same (a.bpm, b.bpm) && same (a.key, b.key) && same (a.grid, b.grid)
                && same (a.loudness, b.loudness);
        }
    }

    juce::String newUuid()
    {
        return juce::Uuid().toDashedString().toLowerCase();
    }

    void canonicalOrder (A3tData& d)
    {
        std::stable_sort (d.cues.begin(), d.cues.end(), [] (const Cue& a, const Cue& b)
        {
            if (a.time != b.time) return a.time < b.time;
            if (a.kind != b.kind) return static_cast<int> (a.kind) < static_cast<int> (b.kind);
            return a.slot < b.slot;
        });
        std::stable_sort (d.loops.begin(), d.loops.end(), [] (const Loop& a, const Loop& b)
        {
            return a.in != b.in ? a.in < b.in : a.slot < b.slot;
        });
        std::stable_sort (d.phrases.begin(), d.phrases.end(), [] (const Phrase& a, const Phrase& b)
        {
            return a.start < b.start;
        });
        sortEvents (d.events);
    }

    bool operator== (const A3tData& a, const A3tData& b)
    {
        return a.uuid == b.uuid && a.revision == b.revision && same (a.track, b.track)
            && same (a.analysis, b.analysis) && sameAll (a.stems, b.stems)
            && sameAll (a.cues, b.cues) && sameAll (a.loops, b.loops)
            && sameAll (a.phrases, b.phrases) && sameAll (a.events, b.events)
            && same (a.extra, b.extra);
    }

    juce::int64 lengthFramesAt (const AudioInfo& audio, int sampleRate)
    {
        if (audio.sampleRate <= 0 || audio.lengthSeconds <= 0.0 || sampleRate <= 0)
            return 0;
        const auto sourceFrames = (juce::int64) std::llround (audio.lengthSeconds * audio.sampleRate);
        // av_rescale_rnd with AV_ROUND_NEAR_INF, for a positive count; Model stays free of libav.
        return (sourceFrames * sampleRate + audio.sampleRate / 2) / audio.sampleRate;
    }
}
