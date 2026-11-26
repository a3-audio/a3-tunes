// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Sidecar.h"

namespace a3t
{
    namespace
    {
        const juce::String sidecarSuffix = ".a3events.json";
        const juce::StringArray knownSidecarKeys {
            "version", "based_on", "events", "cues", "loops", "phrases", "analysis", "track", "stems" };
        const juce::String trackSuffix = ".a3mkv";

        // ---- reading ----

        template <typename T, typename FromVar>
        std::optional<std::vector<T>> sectionFrom (const juce::var& root, const char* key, FromVar fromVar)
        {
            if (! root[key].isArray())
                return std::nullopt;
            return fromVar (root[key]);
        }

        void readTrackFields (Sidecar& s, const juce::var& track)
        {
            if (auto* o = track.getDynamicObject())
                s.trackFields = o->getProperties();
        }

        void readSections (Sidecar& s, const juce::var& root)
        {
            s.events = sectionFrom<Event> (root, "events", eventsFromVar);
            s.cues = sectionFrom<Cue> (root, "cues", cuesFromVar);
            s.loops = sectionFrom<Loop> (root, "loops", loopsFromVar);
            s.phrases = sectionFrom<Phrase> (root, "phrases", phrasesFromVar);
            if (root["analysis"].getDynamicObject() != nullptr)
                s.analysis = analysisFromVar (root["analysis"]);
            readTrackFields (s, root["track"]);
            if (root["stems"].isArray())
                s.stems = stemsFromVar (root["stems"]);
            s.extra = unknownKeys (root, knownSidecarKeys);
        }

        void readBasedOn (Sidecar& s, const juce::var& basedOn)
        {
            if (basedOn["uuid"].isString())
                s.basedOnUuid = basedOn["uuid"].toString();
            s.basedOnRevision = intOr (basedOn["revision"], 0);
        }

        juce::var parseObject (const juce::File& file)
        {
            const auto text = file.loadFileAsString();
            if (nestsTooDeep (text))
                return {};
            juce::var v;
            if (juce::JSON::parse (text, v).failed() || v.getDynamicObject() == nullptr)
                return {};
            return v;
        }

        LoadedSidecar loadChecked (const juce::File& file)
        {
            LoadedSidecar out;
            out.status = Status::damaged;
            const auto root = parseObject (file);
            if (! (root["version"].isInt() || root["version"].isInt64()))
                return out;

            // Whole: cut to an int, 2^32 + 1 would read as version 1.
            const auto version = static_cast<juce::int64> (root["version"]);
            if (version > sidecarVersion)
            {
                out.status = Status::newer;
                return out;
            }
            // The StemDeck v1 file always carries its events.
            if (version < 1 || (version == 1 && ! root["events"].isArray()))
                return out;

            out.sidecar.version = static_cast<int> (version);
            readBasedOn (out.sidecar, root["based_on"]);
            readSections (out.sidecar, root);
            out.status = Status::ok;
            return out;
        }

        // ---- writing ----

        juce::var documentFor (const Sidecar& s)
        {
            auto* root = new juce::DynamicObject();
            juce::var result (root);
            root->setProperty ("version", sidecarVersion);

            auto* basedOn = new juce::DynamicObject();
            basedOn->setProperty ("uuid", s.basedOnUuid);
            basedOn->setProperty ("revision", s.basedOnRevision);
            root->setProperty ("based_on", juce::var (basedOn));

            if (s.events) root->setProperty ("events", eventsToVar (*s.events));
            if (s.cues) root->setProperty ("cues", cuesToVar (*s.cues));
            if (s.loops) root->setProperty ("loops", loopsToVar (*s.loops));
            if (s.phrases) root->setProperty ("phrases", phrasesToVar (*s.phrases));
            if (s.analysis) root->setProperty ("analysis", analysisToVar (*s.analysis));
            if (s.trackFields.size() > 0)
            {
                auto* track = new juce::DynamicObject();
                for (const auto& p : s.trackFields)
                    track->setProperty (p.name, p.value);
                root->setProperty ("track", juce::var (track));
            }
            if (s.stems) root->setProperty ("stems", stemsToVar (*s.stems));
            copyUnknown (*root, s.extra);
            return result;
        }

        bool writeAtomically (const juce::File& file, const juce::String& text)
        {
            juce::TemporaryFile temporary (file);   // same folder, so the final move is a rename
            return temporary.getFile().replaceWithText (text)
                && temporary.overwriteTargetFileWithTemporary();
        }

        // ---- overlay ----

        void applyAnalysis (Analysis& target, const Analysis& rig)
        {
            if (rig.bpm) target.bpm = rig.bpm;
            if (rig.key) target.key = rig.key;
            if (rig.grid) target.grid = rig.grid;
            if (rig.loudness) target.loudness = rig.loudness;
            if (auto* extra = rig.extra.getDynamicObject())
            {
                if (target.extra.getDynamicObject() == nullptr)
                    target.extra = juce::var (new juce::DynamicObject());
                for (const auto& p : extra->getProperties())
                    target.extra.getDynamicObject()->setProperty (p.name, p.value);
            }
        }

        TrackInfo applyTrackFields (const TrackInfo& track, const juce::NamedValueSet& fields)
        {
            if (fields.size() == 0)
                return track;
            const auto var = trackToVar (track);
            auto* object = var.getDynamicObject();
            for (const auto& p : fields)
                object->setProperty (p.name, p.value);
            return trackFromVar (var);
        }
    }

    bool Sidecar::empty() const
    {
        return ! events && ! cues && ! loops && ! phrases && ! analysis && trackFields.size() == 0 && ! stems
            && extra.getDynamicObject() == nullptr;
    }

    juce::StringArray Sidecar::unknownKeyNames() const
    {
        juce::StringArray names;
        if (auto* o = extra.getDynamicObject())
            for (const auto& p : o->getProperties())
                names.add (p.name.toString());
        if (analysis)
            if (auto* o = analysis->extra.getDynamicObject())
                for (const auto& p : o->getProperties())
                    names.add ("analysis." + p.name.toString());
        return names;
    }

    juce::File sidecarFileFor (const juce::File& track)
    {
        const auto name = track.getFileName();
        const auto base = name.endsWithIgnoreCase (trackSuffix)
                              ? name.dropLastCharacters (trackSuffix.length())
                              : name;
        return track.getSiblingFile (base + sidecarSuffix);
    }

    LoadedSidecar loadSidecar (const juce::File& file)
    {
        if (! file.existsAsFile())
            return {};
        return loadChecked (file);
    }

    bool saveSidecar (const juce::File& file, const Sidecar& sidecar)
    {
        const auto existing = loadSidecar (file).status;
        if (existing == Status::newer || existing == Status::damaged)
            return false;

        if (sidecar.empty())
            return ! file.existsAsFile() || file.deleteFile();

        return writeAtomically (file, juce::JSON::toString (documentFor (sidecar), false));
    }

    bool deleteSidecarIfUnchanged (const juce::File& file, const juce::MemoryBlock& asLoaded)
    {
        juce::MemoryBlock now;
        if (! file.loadFileAsData (now) || now != asLoaded)
            return false;
        return file.deleteFile();
    }

    A3tData overlay (const A3tData& base, const Sidecar& sidecar)
    {
        auto out = base;
        if (sidecar.events) out.events = *sidecar.events;
        if (sidecar.cues) out.cues = *sidecar.cues;
        if (sidecar.loops) out.loops = *sidecar.loops;
        if (sidecar.phrases) out.phrases = *sidecar.phrases;
        if (sidecar.stems) out.stems = *sidecar.stems;
        if (sidecar.analysis) applyAnalysis (out.analysis, *sidecar.analysis);
        out.track = applyTrackFields (out.track, sidecar.trackFields);
        canonicalOrder (out);
        return out;
    }

    bool belongsTo (const Sidecar& sidecar, const A3tData& data)
    {
        return sidecar.basedOnUuid.isEmpty() || sidecar.basedOnUuid == data.uuid;
    }

    std::optional<juce::File> findSidecarByUuid (const juce::File& folder, const juce::String& uuid)
    {
        if (uuid.isEmpty())
            return std::nullopt;
        for (const auto& candidate : folder.findChildFiles (juce::File::findFiles, false, "*" + sidecarSuffix))
        {
            const auto loaded = loadSidecar (candidate);
            if (loaded.status == Status::ok && loaded.sidecar.basedOnUuid == uuid)
                return candidate;
        }
        return std::nullopt;
    }
}
