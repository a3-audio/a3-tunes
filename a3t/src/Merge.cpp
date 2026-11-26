// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Merge.h"

#include <algorithm>
#include <map>

namespace a3t
{
    namespace
    {
        using Section = Change::Section;
        using Kind = Change::Kind;

        const juce::String playCountField = "playCount";

        // ---- keys: the only place a key is built ----

        juce::String beatText (double beat) { return juce::String (beat, 6); }

        juce::String changeKey (const Event& e)
        {
            static const char* const slotNames[] = { "position", "shot", "unknown" };
            return "event:" + juce::String (e.stem) + ":" + beatText (e.beat) + ":" + slotNames[EventList::slotKind (e)];
        }

        juce::String changeKey (const Cue& c)
        {
            static const char* const kindNames[] = { "hot", "memory", "load" };
            const auto prefix = juce::String ("cue:") + kindNames[static_cast<int> (c.kind)] + ":";
            const bool bySlot = c.kind != Cue::Kind::memory && c.slot >= 0;
            return prefix + (bySlot ? juce::String (c.slot) : beatText (c.time));
        }

        juce::String changeKey (const Loop& l)
        {
            return l.slot >= 0 ? "loop:" + juce::String (l.slot) : "loop:in:" + beatText (l.in);
        }

        juce::String changeKey (const Phrase& p) { return "phrase:" + beatText (p.start); }

        juce::String trackFieldKey (const juce::String& name) { return "trackField:" + name; }

        // ---- display text, which doubles as the equality test ----

        juce::String text (const juce::var& v) { return juce::JSON::toString (v, true); }
        juce::String describe (const Event& e) { return text (toVar (e)); }
        juce::String describe (const Cue& c) { return text (cuesToVar ({ c })[0]); }
        juce::String describe (const Loop& l) { return text (loopsToVar ({ l })[0]); }
        juce::String describe (const Phrase& p) { return text (phrasesToVar ({ p })[0]); }

        // ---- sections of items, keyed ----

        template <typename T>
        struct Keyed
        {
            juce::String key;
            const T* item;
        };

        // Two items can share a key (unknown events); the later ones get a counter.
        template <typename T>
        std::vector<Keyed<T>> keyed (const std::vector<T>& items)
        {
            std::vector<Keyed<T>> out;
            std::map<juce::String, int> seen;
            for (const auto& item : items)
            {
                auto key = changeKey (item);
                if (const auto n = seen[key]++; n > 0)
                    key += "#" + juce::String (n + 1);
                out.push_back ({ key, &item });
            }
            return out;
        }

        template <typename T>
        std::map<juce::String, const T*> byKey (const std::vector<Keyed<T>>& items)
        {
            std::map<juce::String, const T*> out;
            for (const auto& k : items)
                out[k.key] = k.item;
            return out;
        }

        Change makeChange (Section section, Kind kind, const juce::String& key,
                           const juce::String& laptop, const juce::String& rig)
        {
            Change c;
            c.section = section;
            c.kind = kind;
            c.key = key;
            c.laptop = laptop;
            c.rig = rig;
            return c;
        }

        template <typename T>
        void diffSection (Section section, const std::vector<T>& laptop, const std::vector<T>& rig,
                          std::vector<Change>& out)
        {
            const auto laptopKeyed = keyed (laptop);
            const auto rigKeyed = keyed (rig);
            const auto laptopByKey = byKey (laptopKeyed);
            const auto rigByKey = byKey (rigKeyed);

            for (const auto& l : laptopKeyed)
            {
                const auto r = rigByKey.find (l.key);
                if (r == rigByKey.end())
                    out.push_back (makeChange (section, Kind::removed, l.key, describe (*l.item), {}));
                else if (describe (*r->second) != describe (*l.item))
                    out.push_back (makeChange (section, Kind::changed, l.key, describe (*l.item), describe (*r->second)));
            }
            for (const auto& r : rigKeyed)
                if (laptopByKey.count (r.key) == 0)
                    out.push_back (makeChange (section, Kind::added, r.key, {}, describe (*r.item)));
        }

        // Order is left to canonicalOrder, which overlay applies.
        template <typename T>
        std::vector<T> applySection (const std::vector<T>& laptop, const std::vector<T>& rig,
                                     const std::set<juce::String>& taken)
        {
            const auto laptopKeyed = keyed (laptop);
            const auto rigKeyed = keyed (rig);
            const auto laptopByKey = byKey (laptopKeyed);
            const auto rigByKey = byKey (rigKeyed);

            std::vector<T> out;
            for (const auto& l : laptopKeyed)
            {
                if (taken.count (l.key) == 0)
                    out.push_back (*l.item);
                else if (const auto r = rigByKey.find (l.key); r != rigByKey.end())
                    out.push_back (*r->second);
            }
            for (const auto& r : rigKeyed)
                if (taken.count (r.key) > 0 && laptopByKey.count (r.key) == 0)
                    out.push_back (*r.item);
            return out;
        }

        // ---- analysis: one item per member ----

        template <typename T, typename Describe>
        void diffMember (const juce::String& key, Section section, const std::optional<T>& laptop,
                         const std::optional<T>& rig, Describe describeValue, std::vector<Change>& out)
        {
            if (! rig)   // the rig did not change this member
                return;
            if (! laptop)
                out.push_back (makeChange (section, Kind::added, key, {}, describeValue (*rig)));
            else if (describeValue (*laptop) != describeValue (*rig))
                out.push_back (makeChange (section, Kind::changed, key, describeValue (*laptop), describeValue (*rig)));
        }

        juce::String describe (const Analysis& a) { return text (analysisToVar (a)); }

        template <typename T>
        juce::String describeMember (const T& value, std::optional<T> Analysis::*member)
        {
            Analysis a;
            a.*member = value;
            return describe (a);
        }

        void diffAnalysis (const Analysis& laptop, const Analysis& rig, std::vector<Change>& out)
        {
            diffMember<double> ("analysis:bpm", Section::analysis, laptop.bpm, rig.bpm,
                                [] (double v) { return text (v); }, out);
            diffMember<Key> ("analysis:key", Section::analysis, laptop.key, rig.key,
                             [] (const Key& v) { return describeMember (v, &Analysis::key); }, out);
            diffMember<Grid> ("grid", Section::grid, laptop.grid, rig.grid,
                              [] (const Grid& v) { return describeMember (v, &Analysis::grid); }, out);
            diffMember<Loudness> ("analysis:loudness", Section::analysis, laptop.loudness, rig.loudness,
                                  [] (const Loudness& v) { return describeMember (v, &Analysis::loudness); }, out);
        }

        // Only the members whose key was taken; overlay then does the merging.
        Analysis takenAnalysis (const Analysis& rig, const std::set<juce::String>& taken)
        {
            Analysis a;
            if (taken.count ("analysis:bpm") > 0) a.bpm = rig.bpm;
            if (taken.count ("analysis:key") > 0) a.key = rig.key;
            if (taken.count ("grid") > 0) a.grid = rig.grid;
            if (taken.count ("analysis:loudness") > 0) a.loudness = rig.loudness;
            return a;
        }

        // ---- track fields ----

        // The rig's value as the file would hold it once taken (clamped, quantised, typed): what the
        // merge compares, shows and writes, so the three cannot disagree.
        juce::var normalisedField (const TrackInfo& laptop, const juce::Identifier& name, const juce::var& value)
        {
            const auto v = trackToVar (laptop);
            v.getDynamicObject()->setProperty (name, value);
            return trackToVar (trackFromVar (v)).getProperty (name, {});
        }

        // playCount only grows: the rig's value when it is larger, else nothing.
        std::optional<int> largerPlayCount (const TrackInfo& laptop, const juce::var& rigValue)
        {
            const auto theirs = static_cast<int> (normalisedField (laptop, playCountField, rigValue));
            return theirs > laptop.playCount ? std::optional<int> (theirs) : std::nullopt;
        }

        void diffPlayCount (const A3tData& laptop, const juce::var& rigValue, std::vector<Change>& out)
        {
            const auto theirs = largerPlayCount (laptop.track, rigValue);
            if (! theirs)   // a no-op is no difference
                return;
            auto c = makeChange (Section::trackField, Kind::changed, trackFieldKey (playCountField),
                                 juce::String (laptop.track.playCount), juce::String (*theirs));
            c.automatic = true;
            out.push_back (c);
        }

        void diffTrackFields (const A3tData& laptop, const juce::NamedValueSet& rig, std::vector<Change>& out)
        {
            const auto mine = trackToVar (laptop.track);
            for (const auto& field : rig)
            {
                const auto name = field.name.toString();
                if (name == playCountField)
                {
                    diffPlayCount (laptop, field.value, out);
                    continue;
                }
                const auto before = mine.getProperty (field.name, {});
                const auto after = normalisedField (laptop.track, field.name, field.value);
                if (before.isVoid())
                    out.push_back (makeChange (Section::trackField, Kind::added, trackFieldKey (name), {}, text (after)));
                else if (text (before) != text (after))
                    out.push_back (makeChange (Section::trackField, Kind::changed, trackFieldKey (name), text (before), text (after)));
            }
        }

        juce::NamedValueSet takenTrackFields (const A3tData& laptop, const juce::NamedValueSet& rig,
                                              const std::set<juce::String>& taken)
        {
            juce::NamedValueSet out;
            for (const auto& field : rig)
            {
                const auto name = field.name.toString();
                if (name == playCountField)   // automatic: the larger value, whatever was chosen
                {
                    if (const auto theirs = largerPlayCount (laptop.track, field.value))
                        out.set (field.name, *theirs);
                }
                else if (taken.count (trackFieldKey (name)) > 0)
                    out.set (field.name, normalisedField (laptop.track, field.name, field.value));
            }
            return out;
        }
    }

    namespace
    {
        const juce::String stemsKey = "stems";

        void diffStems (const A3tData& laptop, const Sidecar& rig, std::vector<Change>& out)
        {
            const auto mine = text (stemsToVar (laptop.stems)), theirs = text (stemsToVar (*rig.stems));
            if (mine != theirs)
                out.push_back (makeChange (Section::stems, Kind::changed, stemsKey, mine, theirs));
        }
    }

    std::vector<Change> diff (const A3tData& laptop, const Sidecar& rig)
    {
        std::vector<Change> out;
        if (rig.events) diffSection (Section::event, laptop.events, *rig.events, out);
        if (rig.cues) diffSection (Section::cue, laptop.cues, *rig.cues, out);
        if (rig.loops) diffSection (Section::loop, laptop.loops, *rig.loops, out);
        if (rig.phrases) diffSection (Section::phrase, laptop.phrases, *rig.phrases, out);
        if (rig.analysis) diffAnalysis (laptop.analysis, *rig.analysis, out);
        if (rig.stems) diffStems (laptop, rig, out);
        diffTrackFields (laptop, rig.trackFields, out);
        return out;
    }

    A3tData applyChanges (const A3tData& laptop, const Sidecar& rig, const std::set<juce::String>& takeFromRig)
    {
        // The chosen parts go through overlay, so a merge and an overlay cannot disagree on a field.
        Sidecar chosen;
        if (rig.events) chosen.events = applySection (laptop.events, *rig.events, takeFromRig);
        if (rig.cues) chosen.cues = applySection (laptop.cues, *rig.cues, takeFromRig);
        if (rig.loops) chosen.loops = applySection (laptop.loops, *rig.loops, takeFromRig);
        if (rig.phrases) chosen.phrases = applySection (laptop.phrases, *rig.phrases, takeFromRig);
        if (rig.analysis) chosen.analysis = takenAnalysis (*rig.analysis, takeFromRig);
        chosen.trackFields = takenTrackFields (laptop, rig.trackFields, takeFromRig);
        if (rig.stems && takeFromRig.count (stemsKey) > 0) chosen.stems = rig.stems;

        auto out = overlay (laptop, chosen);
        out.revision = laptop.revision + 1;
        return out;
    }

    bool laptopChangedSince (const A3tData& data, const Sidecar& rig)
    {
        return data.revision > rig.basedOnRevision;
    }
}
