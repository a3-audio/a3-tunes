// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Derived.h"

#include <algorithm>
#include <iterator>
#include <optional>

namespace a3t
{
    namespace
    {
        constexpr double minimalChapterLength = 0.001;

        juce::String positiveNumber (int n)
        {
            return n > 0 ? juce::String (n) : juce::String();
        }

        // Up to two decimals, no trailing zeros: 126, 125.5, 125.25.
        juce::String bpmText (const std::optional<double>& bpm)
        {
            if (! bpm)
                return {};
            auto text = juce::String (*bpm, 2);
            while (text.endsWithChar ('0'))
                text = text.dropLastCharacters (1);
            return text.endsWithChar ('.') ? text.dropLastCharacters (1) : text;
        }

        juce::String keyText (const std::optional<Key>& key)
        {
            return key ? key->musical : juce::String();
        }

        bool isChapterCue (const Cue& c)
        {
            return c.kind == Cue::Kind::hot || c.kind == Cue::Kind::memory;
        }

        juce::String chapterTitle (const Cue& c)
        {
            constexpr int numHotCueSlots = 16;
            if (c.kind != Cue::Kind::hot || c.slot < 0 || c.slot >= numHotCueSlots)
                return c.name;
            const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + c.slot));
            return c.name.isEmpty() ? letter : letter + ": " + c.name;
        }
    }

    std::vector<Tag> tagsFor (const A3tData& d)
    {
        const auto& t = d.track;
        const std::vector<Tag> all {
            { "TITLE", t.title },
            { "ARTIST", t.artist },
            { "ALBUM", t.album },
            { "ALBUM_ARTIST", t.albumArtist },
            { "GENRE", t.genre },
            { "LABEL", t.label },
            { "REMIXED_BY", t.remixer },
            { "COMPOSER", t.composer },
            { "LYRICIST", t.lyricist },
            { "COMMENT", t.comment },
            { "ISRC", t.isrc },
            { "DATE_RELEASED", positiveNumber (t.year) },
            { "PART_NUMBER", positiveNumber (t.trackNo) },
            { "BPM", bpmText (d.analysis.bpm) },
            { "INITIAL_KEY", keyText (d.analysis.key) },
            { "A3T_UUID", d.uuid },
            { "A3T_VERSION", juce::String (formatVersion) },
        };

        std::vector<Tag> tags;
        std::copy_if (all.begin(), all.end(), std::back_inserter (tags),
                      [] (const Tag& tag) { return tag.second.isNotEmpty(); });
        return tags;
    }

    std::vector<Chapter> chaptersFor (const A3tData& d, double lengthSeconds)
    {
        std::vector<Cue> cues;
        // A muxer refuses chapters outside the track: those past the end go, an early one starts at 0.
        std::copy_if (d.cues.begin(), d.cues.end(), std::back_inserter (cues),
                      [lengthSeconds] (const Cue& c) { return isChapterCue (c) && c.time < lengthSeconds; });
        std::stable_sort (cues.begin(), cues.end(),
                          [] (const Cue& a, const Cue& b) { return a.time < b.time; });

        std::vector<Chapter> chapters;
        for (size_t i = 0; i < cues.size(); ++i)
        {
            Chapter c;
            c.start = std::max (cues[i].time, 0.0);
            c.end = i + 1 < cues.size() ? cues[i + 1].time : lengthSeconds;
            if (c.end <= c.start)
                c.end = c.start + minimalChapterLength;
            c.end = std::min (c.end, lengthSeconds);
            c.title = chapterTitle (cues[i]);
            chapters.push_back (std::move (c));
        }
        return chapters;
    }
}
