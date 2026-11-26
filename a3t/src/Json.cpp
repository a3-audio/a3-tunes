// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Json.h"

#include <algorithm>
#include <cmath>

namespace a3t
{
    namespace
    {
        // ---- reading helpers: a bad or missing field gives the fallback ----

        bool isNumber (const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

        bool isFiniteNumber (const juce::var& v)
        {
            return isNumber (v) && std::isfinite (static_cast<double> (v));
        }

        double numberOr (const juce::var& v, double fallback)
        {
            return isFiniteNumber (v) ? static_cast<double> (v) : fallback;
        }

        int intOr (const juce::var& v, int fallback)
        {
            return isFiniteNumber (v) ? static_cast<int> (static_cast<double> (v)) : fallback;
        }

        bool boolOr (const juce::var& v, bool fallback) { return v.isBool() ? static_cast<bool> (v) : fallback; }

        juce::String stringOr (const juce::var& v) { return v.isString() ? v.toString() : juce::String(); }

        // The keys of `obj` that are not in `known`, or void when there are none.
        juce::var unknownKeys (const juce::var& obj, const juce::StringArray& known)
        {
            auto* src = obj.getDynamicObject();
            if (src == nullptr)
                return {};
            auto* out = new juce::DynamicObject();
            juce::var result (out);
            for (const auto& p : src->getProperties())
                if (! known.contains (p.name.toString()))
                    out->setProperty (p.name, p.value);
            return out->getProperties().size() > 0 ? result : juce::var();
        }

        void copyUnknown (juce::DynamicObject& target, const juce::var& extra)
        {
            auto* src = extra.getDynamicObject();
            if (src == nullptr)
                return;
            for (const auto& p : src->getProperties())
                if (! target.hasProperty (p.name))
                    target.setProperty (p.name, p.value);
        }

        juce::var makeObject (juce::DynamicObject* o) { return juce::var (o); }

        // ---- enums ----

        const char* kindName (Cue::Kind k)
        {
            switch (k)
            {
                case Cue::Kind::hot: return "hot";
                case Cue::Kind::memory: return "memory";
                case Cue::Kind::load: return "load";
            }
            return "memory";
        }

        const juce::StringArray phraseKinds { "intro", "up", "down", "chorus", "bridge", "outro" };
        const juce::StringArray phraseMoods { "high", "mid", "low" };

        // ---- audio ----

        juce::var audioToVar (const AudioInfo& a)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("sampleRate", a.sampleRate);
            o->setProperty ("bitDepth", a.bitDepth);
            o->setProperty ("channels", a.channels);
            o->setProperty ("bitrate", a.bitrate);
            o->setProperty ("codec", a.codec);
            o->setProperty ("source", a.source);
            o->setProperty ("lengthSeconds", a.lengthSeconds);
            return makeObject (o);
        }

        AudioInfo audioFromVar (const juce::var& v)
        {
            AudioInfo a;
            a.sampleRate = intOr (v["sampleRate"], 0);
            a.bitDepth = intOr (v["bitDepth"], 0);
            a.channels = intOr (v["channels"], 0);
            a.bitrate = static_cast<juce::int64> (numberOr (v["bitrate"], 0.0));
            a.codec = stringOr (v["codec"]);
            a.source = stringOr (v["source"]);
            a.lengthSeconds = numberOr (v["lengthSeconds"], 0.0);
            return a;
        }

        // ---- track ----

        const juce::StringArray knownTrackKeys {
            "title", "artist", "album", "albumArtist", "genre", "label", "remixer", "composer",
            "lyricist", "year", "trackNo", "discNo", "comment", "isrc", "rating", "colour",
            "playCount", "dateAdded", "myTags", "source", "audio", "gainDb" };

        juce::var tagsToVar (const juce::StringArray& tags)
        {
            juce::Array<juce::var> arr;
            for (const auto& t : tags)
                arr.add (t);
            return arr;
        }

        juce::StringArray tagsFromVar (const juce::var& v)
        {
            juce::StringArray tags;
            if (auto* arr = v.getArray())
                for (const auto& t : *arr)
                    if (t.isString())
                        tags.add (t.toString());
            return tags;
        }

        juce::var sourceToVar (const TrackInfo& t)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("service", t.sourceService);
            o->setProperty ("id", t.sourceId);
            return makeObject (o);
        }

        void setTrackStrings (juce::DynamicObject& o, const TrackInfo& t)
        {
            o.setProperty ("title", t.title);
            o.setProperty ("artist", t.artist);
            o.setProperty ("album", t.album);
            o.setProperty ("albumArtist", t.albumArtist);
            o.setProperty ("genre", t.genre);
            o.setProperty ("label", t.label);
            o.setProperty ("remixer", t.remixer);
            o.setProperty ("composer", t.composer);
            o.setProperty ("lyricist", t.lyricist);
            o.setProperty ("comment", t.comment);
            o.setProperty ("isrc", t.isrc);
            o.setProperty ("colour", t.colour);
            o.setProperty ("dateAdded", t.dateAdded);
        }

        void setTrackNumbers (juce::DynamicObject& o, const TrackInfo& t)
        {
            o.setProperty ("year", t.year);
            o.setProperty ("trackNo", t.trackNo);
            o.setProperty ("discNo", t.discNo);
            o.setProperty ("rating", t.rating);
            o.setProperty ("playCount", t.playCount);
            o.setProperty ("gainDb", t.gainDb);
        }

        void readTrackStrings (TrackInfo& t, const juce::var& v)
        {
            t.title = stringOr (v["title"]);
            t.artist = stringOr (v["artist"]);
            t.album = stringOr (v["album"]);
            t.albumArtist = stringOr (v["albumArtist"]);
            t.genre = stringOr (v["genre"]);
            t.label = stringOr (v["label"]);
            t.remixer = stringOr (v["remixer"]);
            t.composer = stringOr (v["composer"]);
            t.lyricist = stringOr (v["lyricist"]);
            t.comment = stringOr (v["comment"]);
            t.isrc = stringOr (v["isrc"]);
            t.colour = stringOr (v["colour"]);
            t.dateAdded = stringOr (v["dateAdded"]);
        }

        void readTrackNumbers (TrackInfo& t, const juce::var& v)
        {
            t.year = intOr (v["year"], 0);
            t.trackNo = intOr (v["trackNo"], 0);
            t.discNo = intOr (v["discNo"], 0);
            t.rating = std::clamp (intOr (v["rating"], 0), 0, 5);
            t.playCount = intOr (v["playCount"], 0);
            t.gainDb = numberOr (v["gainDb"], 0.0);
        }

        // ---- analysis ----

        juce::var keyToVar (const Key& k)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("camelot", k.camelot);
            o->setProperty ("musical", k.musical);
            o->setProperty ("confidence", k.confidence);
            return makeObject (o);
        }

        Key keyFromVar (const juce::var& v)
        {
            Key k;
            k.camelot = stringOr (v["camelot"]);
            k.musical = stringOr (v["musical"]);
            k.confidence = numberOr (v["confidence"], 0.0);
            return k;
        }

        juce::var gridToVar (const Grid& g)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("firstBeat", g.firstBeat);
            o->setProperty ("bpm", g.bpm);
            o->setProperty ("beatsPerBar", g.beatsPerBar);
            o->setProperty ("gridVersion", g.gridVersion);
            o->setProperty ("corrected", g.corrected);
            return makeObject (o);
        }

        Grid gridFromVar (const juce::var& v)
        {
            Grid g;
            g.firstBeat = numberOr (v["firstBeat"], g.firstBeat);
            g.bpm = numberOr (v["bpm"], g.bpm);
            g.beatsPerBar = intOr (v["beatsPerBar"], g.beatsPerBar);
            g.gridVersion = intOr (v["gridVersion"], g.gridVersion);
            g.corrected = boolOr (v["corrected"], g.corrected);
            return g;
        }

        juce::var loudnessToVar (const Loudness& l)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("lufs", l.lufs);
            o->setProperty ("peakDb", l.peakDb);
            return makeObject (o);
        }

        Loudness loudnessFromVar (const juce::var& v)
        {
            Loudness l;
            l.lufs = numberOr (v["lufs"], 0.0);
            l.peakDb = numberOr (v["peakDb"], 0.0);
            return l;
        }

        juce::var analysisToVar (const Analysis& a)
        {
            auto* o = new juce::DynamicObject();
            if (a.bpm)
                o->setProperty ("bpm", *a.bpm);
            if (a.key)
                o->setProperty ("key", keyToVar (*a.key));
            if (a.grid)
                o->setProperty ("grid", gridToVar (*a.grid));
            if (a.loudness)
                o->setProperty ("loudness", loudnessToVar (*a.loudness));
            return makeObject (o);
        }

        Analysis analysisFromVar (const juce::var& v)
        {
            Analysis a;
            if (isFiniteNumber (v["bpm"]))
                a.bpm = static_cast<double> (v["bpm"]);
            if (v["key"].getDynamicObject() != nullptr)
                a.key = keyFromVar (v["key"]);
            if (v["grid"].getDynamicObject() != nullptr)
                a.grid = gridFromVar (v["grid"]);
            if (v["loudness"].getDynamicObject() != nullptr)
                a.loudness = loudnessFromVar (v["loudness"]);
            return a;
        }

        // ---- cues, loops, phrases ----

        bool validSlot (int slot) { return slot >= 0 && slot < 16; }

        juce::var cueToVar (const Cue& c)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("kind", kindName (c.kind));
            if (c.kind == Cue::Kind::hot)
                o->setProperty ("slot", c.slot);
            o->setProperty ("time", c.time);
            if (c.kind != Cue::Kind::load)
            {
                o->setProperty ("colour", c.colour);
                o->setProperty ("name", c.name);
            }
            return makeObject (o);
        }

        std::optional<Cue> cueFromVar (const juce::var& v)
        {
            if (! isFiniteNumber (v["time"]))
                return std::nullopt;
            const auto kind = v["kind"].toString();
            Cue c;
            if (kind == "hot") c.kind = Cue::Kind::hot;
            else if (kind == "memory") c.kind = Cue::Kind::memory;
            else if (kind == "load") c.kind = Cue::Kind::load;
            else return std::nullopt;

            if (c.kind == Cue::Kind::hot)
            {
                c.slot = intOr (v["slot"], -1);
                if (! validSlot (c.slot))
                    return std::nullopt;
            }
            c.time = static_cast<double> (v["time"]);
            c.colour = stringOr (v["colour"]);
            c.name = stringOr (v["name"]);
            return c;
        }

        juce::var loopToVar (const Loop& l)
        {
            auto* o = new juce::DynamicObject();
            if (l.slot >= 0)
                o->setProperty ("slot", l.slot);
            o->setProperty ("in", l.in);
            o->setProperty ("out", l.out);
            o->setProperty ("colour", l.colour);
            o->setProperty ("name", l.name);
            o->setProperty ("active", l.active);
            return makeObject (o);
        }

        std::optional<Loop> loopFromVar (const juce::var& v)
        {
            if (! isFiniteNumber (v["in"]) || ! isFiniteNumber (v["out"]))
                return std::nullopt;
            Loop l;
            if (v.hasProperty ("slot"))
            {
                l.slot = intOr (v["slot"], -1);
                if (! validSlot (l.slot))
                    return std::nullopt;
            }
            l.in = static_cast<double> (v["in"]);
            l.out = static_cast<double> (v["out"]);
            l.colour = stringOr (v["colour"]);
            l.name = stringOr (v["name"]);
            l.active = boolOr (v["active"], false);
            return l;
        }

        juce::var phraseToVar (const Phrase& p)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("start", p.start);
            o->setProperty ("end", p.end);
            o->setProperty ("kind", phraseKinds[static_cast<int> (p.kind)]);
            o->setProperty ("mood", phraseMoods[static_cast<int> (p.mood)]);
            o->setProperty ("label", p.label);
            return makeObject (o);
        }

        std::optional<Phrase> phraseFromVar (const juce::var& v)
        {
            const int kind = phraseKinds.indexOf (v["kind"].toString());
            const int mood = phraseMoods.indexOf (v["mood"].toString());
            if (kind < 0 || mood < 0 || ! isFiniteNumber (v["start"]) || ! isFiniteNumber (v["end"]))
                return std::nullopt;
            Phrase p;
            p.kind = static_cast<Phrase::Kind> (kind);
            p.mood = static_cast<Phrase::Mood> (mood);
            p.start = static_cast<double> (v["start"]);
            p.end = static_cast<double> (v["end"]);
            p.label = stringOr (v["label"]);
            return p;
        }

        // ---- lists ----

        template <typename T, typename ToVar>
        juce::var listToVar (const std::vector<T>& items, ToVar toVar)
        {
            juce::Array<juce::var> arr;
            for (const auto& i : items)
                arr.add (toVar (i));
            return arr;
        }

        template <typename T, typename FromVar>
        std::vector<T> listFromVar (const juce::var& v, FromVar fromVar, size_t cap = SIZE_MAX)
        {
            std::vector<T> out;
            if (auto* arr = v.getArray())
                for (const auto& item : *arr)
                    if (auto parsedItem = fromVar (item))
                    {
                        if (out.size() >= cap)
                            break;
                        out.push_back (*parsedItem);
                    }
            return out;
        }

        juce::var stemsToVar (const std::array<StemLabel, numStems>& stems)
        {
            juce::Array<juce::var> arr;
            for (const auto& s : stems)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("label", s.label);
                o->setProperty ("colour", s.colour);
                arr.add (makeObject (o));
            }
            return arr;
        }

        void stemsFromVar (std::array<StemLabel, numStems>& stems, const juce::var& v)
        {
            auto* arr = v.getArray();
            if (arr == nullptr)
                return;
            for (int i = 0; i < numStems && i < arr->size(); ++i)
            {
                stems[static_cast<size_t> (i)].label = stringOr ((*arr)[i]["label"]);
                stems[static_cast<size_t> (i)].colour = stringOr ((*arr)[i]["colour"]);
            }
        }

        const juce::StringArray knownTopKeys {
            "version", "uuid", "revision", "track", "analysis", "stems", "cues", "loops", "phrases", "events" };

        A3tData dataFromObject (const juce::var& v)
        {
            A3tData d;
            d.uuid = stringOr (v["uuid"]);
            d.revision = intOr (v["revision"], 0);
            d.track = trackFromVar (v["track"]);
            d.analysis = analysisFromVar (v["analysis"]);
            stemsFromVar (d.stems, v["stems"]);
            d.cues = listFromVar<Cue> (v["cues"], cueFromVar);
            d.loops = listFromVar<Loop> (v["loops"], loopFromVar);
            d.phrases = listFromVar<Phrase> (v["phrases"], phraseFromVar);
            d.events = listFromVar<Event> (v["events"], eventFromVar, maxEvents);
            d.extra = unknownKeys (v, knownTopKeys);
            return d;
        }
    }

    juce::var trackToVar (const TrackInfo& t)
    {
        auto* o = new juce::DynamicObject();
        setTrackStrings (*o, t);
        setTrackNumbers (*o, t);
        o->setProperty ("myTags", tagsToVar (t.myTags));
        if (t.sourceService.isNotEmpty() || t.sourceId.isNotEmpty())
            o->setProperty ("source", sourceToVar (t));
        o->setProperty ("audio", audioToVar (t.audio));
        copyUnknown (*o, t.trackExtra);
        return makeObject (o);
    }

    TrackInfo trackFromVar (const juce::var& v)
    {
        TrackInfo t;
        readTrackStrings (t, v);
        readTrackNumbers (t, v);
        t.myTags = tagsFromVar (v["myTags"]);
        t.sourceService = stringOr (v["source"]["service"]);
        t.sourceId = stringOr (v["source"]["id"]);
        t.audio = audioFromVar (v["audio"]);
        t.trackExtra = unknownKeys (v, knownTrackKeys);
        return t;
    }

    Loaded parse (const juce::String& json)
    {
        Loaded out;
        out.status = Status::damaged;

        juce::var v;
        if (juce::JSON::parse (json, v).failed() || v.getDynamicObject() == nullptr)
            return out;

        const auto version = v["version"];
        if (! (version.isInt() || version.isInt64()))
            return out;
        if (static_cast<int> (version) > formatVersion)
        {
            out.status = Status::newer;
            return out;
        }
        if (static_cast<int> (version) < 1)
            return out;

        out.data = dataFromObject (v);
        out.status = Status::ok;
        return out;
    }

    juce::String serialise (const A3tData& d)
    {
        auto* o = new juce::DynamicObject();
        juce::var root (o);
        o->setProperty ("version", formatVersion);
        o->setProperty ("uuid", d.uuid);
        o->setProperty ("revision", d.revision);
        o->setProperty ("track", trackToVar (d.track));
        o->setProperty ("analysis", analysisToVar (d.analysis));
        o->setProperty ("stems", stemsToVar (d.stems));
        o->setProperty ("cues", listToVar (d.cues, cueToVar));
        o->setProperty ("loops", listToVar (d.loops, loopToVar));
        o->setProperty ("phrases", listToVar (d.phrases, phraseToVar));
        o->setProperty ("events", listToVar (d.events, [] (const Event& e) { return toVar (e); }));
        copyUnknown (*o, d.extra);
        return juce::JSON::toString (root, false);
    }
}
