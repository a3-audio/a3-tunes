// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Events.h"
#include "a3t/Quantise.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>

namespace a3t
{
    namespace
    {
        constexpr int stemCount = 4;
        constexpr double defaultBars = 4.0;
        constexpr double defaultSpeed = 1.0;
        constexpr double barSet[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
        constexpr double speedSet[] = { 0.25, 0.5, 1.0, 2.0, 4.0 };
        // "ping" is the pad CROSS; the file keeps the word.
        const char* const shapeNames[] = { "orbit", "ping", "rise", "scat" };

        template <size_t N>
        double fromSet (const double (&set)[N], double value, double fallback)
        {
            return std::find (std::begin (set), std::end (set), value) != std::end (set) ? value : fallback;
        }

        std::optional<double> finiteNumber (const juce::var& v)
        {
            if (! v.isDouble() && ! v.isInt() && ! v.isInt64())
                return std::nullopt;
            const double value = v;
            if (! std::isfinite (value))
                return std::nullopt;
            return value;
        }

        std::optional<int> stemFrom (const juce::var& v)
        {
            const auto value = finiteNumber (v);
            if (! value || *value != std::floor (*value) || *value < 0.0 || *value >= (double) stemCount)
                return std::nullopt;
            return (int) *value;
        }

        std::optional<Shape> shapeFromName (const juce::String& name)
        {
            for (int i = 0; i < 4; ++i)
                if (name == shapeNames[i])
                    return (Shape) i;
            return std::nullopt;
        }

        std::optional<std::uint64_t> seedFromText (const juce::String& text)
        {
            if (text.isEmpty() || ! text.containsOnly ("0123456789") || text.length() > 20)
                return std::nullopt;
            try
            {
                return (std::uint64_t) std::stoull (text.toStdString());
            }
            catch (const std::exception&)
            {
                return std::nullopt;   // above 2^64 - 1
            }
        }

        bool hasAll (const juce::var& v, std::initializer_list<const char*> names)
        {
            return std::all_of (names.begin(), names.end(), [&] (const char* n) { return v.hasProperty (n); });
        }

        // Missing means hand/active, as the position events are written by the app only.
        std::optional<Origin> originFrom (const juce::var& v)
        {
            if (! v.hasProperty ("origin"))
                return Origin::hand;
            const auto name = v["origin"].toString();
            if (name == "hand")     return Origin::hand;
            if (name == "prepared") return Origin::prepared;
            return std::nullopt;
        }

        EventType typeFrom (const juce::var& v)
        {
            if (! v.hasProperty ("type"))
                return EventType::shot;
            const auto name = v["type"].toString();
            if (name == "shot")     return EventType::shot;
            if (name == "setpos")   return EventType::setpos;
            if (name == "resetpos") return EventType::resetpos;
            return EventType::unknown;
        }

        bool readShot (const juce::var& v, Event& e)
        {
            if (! hasAll (v, { "shape", "bars", "reversed", "seed" }))
                return false;
            const auto shape = shapeFromName (v["shape"].toString());
            const auto seed = seedFromText (v["seed"].toString());
            if (! shape || ! seed)
                return false;
            const auto bars = finiteNumber (v["bars"]);
            const auto speed = v.hasProperty ("speed") ? finiteNumber (v["speed"]) : std::nullopt;
            e.shape = *shape;
            e.bars = bars ? validBars (*bars) : defaultBars;
            e.reversed = (bool) v["reversed"];
            e.speed = speed ? validSpeed (*speed) : defaultSpeed;
            e.seed = *seed;
            return true;
        }

        bool readSetpos (const juce::var& v, Event& e)
        {
            const auto azimuth = finiteNumber (v["azimuth"]);
            const auto elevation = finiteNumber (v["elevation"]);
            if (! azimuth || ! elevation)
                return false;
            e.azimuth = quantised (*azimuth);
            e.elevation = quantised (*elevation);
            return true;
        }

        bool readTypeFields (const juce::var& v, Event& e)
        {
            switch (e.type)
            {
                case EventType::shot:    return readShot (v, e);
                case EventType::setpos:  return readSetpos (v, e);
                default:                 return true;
            }
        }

        bool sortsBefore (const Event& a, const Event& b)
        {
            if (a.beat != b.beat) return a.beat < b.beat;
            if (a.stem != b.stem) return a.stem < b.stem;
            return EventList::slotKind (a) < EventList::slotKind (b);
        }

        bool sameSlot (const Event& a, const Event& b)
        {
            const auto kind = EventList::slotKind (a);
            return kind != 2 && a.stem == b.stem && a.beat == b.beat && kind == EventList::slotKind (b);
        }
    }

    double validBars (double bars)   { return fromSet (barSet, bars, defaultBars); }
    double validSpeed (double speed) { return fromSet (speedSet, speed, defaultSpeed); }

    bool operator== (const Event& a, const Event& b)
    {
        return a.type == b.type && a.stem == b.stem && a.beat == b.beat && a.active == b.active
            && a.origin == b.origin && a.shape == b.shape && a.bars == b.bars && a.reversed == b.reversed
            && a.speed == b.speed && a.seed == b.seed && a.azimuth == b.azimuth && a.elevation == b.elevation
            && juce::JSON::toString (a.raw, true) == juce::JSON::toString (b.raw, true);
    }

    namespace
    {
        // An event of a type this version does not know is kept whole, so a
        // newer file loses nothing on a round trip; stem and beat only place it.
        Event unknownEventFrom (const juce::var& v)
        {
            Event e;
            e.type = EventType::unknown;
            e.stem = stemFrom (v["stem"]).value_or (0);
            e.beat = finiteNumber (v["beat"]).value_or (0.0);
            e.active = v.hasProperty ("active") ? (bool) v["active"] : true;
            e.raw = v;
            return e;
        }
    }

    std::optional<Event> eventFromVar (const juce::var& v)
    {
        if (! v.isObject())
            return std::nullopt;
        if (typeFrom (v) == EventType::unknown)
            return unknownEventFrom (v);

        const auto stem = stemFrom (v["stem"]);
        const auto beat = finiteNumber (v["beat"]);
        const auto origin = originFrom (v);
        if (! stem || ! beat || ! origin)
            return std::nullopt;

        Event e;
        e.type = typeFrom (v);
        e.stem = *stem;
        e.beat = quantised (*beat);
        e.origin = *origin;
        e.active = v.hasProperty ("active") ? (bool) v["active"] : true;
        if (e.type == EventType::unknown)
            e.raw = v;
        if (! readTypeFields (v, e))
            return std::nullopt;
        return e;
    }

    juce::var toVar (const Event& e)
    {
        if (e.type == EventType::unknown)
            return e.raw;

        auto* o = new juce::DynamicObject();
        const char* const typeName = e.type == EventType::shot ? "shot" : e.type == EventType::setpos ? "setpos" : "resetpos";
        o->setProperty ("type", typeName);
        o->setProperty ("stem", e.stem);
        o->setProperty ("beat", quantised (e.beat));
        if (e.type == EventType::shot)
        {
            o->setProperty ("shape", shapeNames[(int) e.shape]);
            o->setProperty ("bars", e.bars);
            o->setProperty ("reversed", e.reversed);
            o->setProperty ("speed", validSpeed (e.speed));
            o->setProperty ("seed", juce::String (std::to_string (e.seed)));
        }
        if (e.type == EventType::setpos)
        {
            o->setProperty ("azimuth", quantised (e.azimuth));
            o->setProperty ("elevation", quantised (e.elevation));
        }
        o->setProperty ("active", e.active);
        o->setProperty ("origin", e.origin == Origin::prepared ? "prepared" : "hand");
        return juce::var (o);
    }

    int EventList::slotKind (const Event& e)
    {
        switch (e.type)
        {
            case EventType::setpos:
            case EventType::resetpos: return 0;
            case EventType::shot:     return 1;
            default:                  return 2;
        }
    }

    EventList::Add EventList::add (const Event& e)
    {
        if (std::any_of (list.begin(), list.end(), [&] (const Event& other) { return sameSlot (e, other); }))
            return Add::occupied;
        if ((int) list.size() >= maxEvents)
            return Add::full;
        const auto where = std::upper_bound (list.begin(), list.end(), e, sortsBefore);
        list.insert (where, e);
        return Add::added;
    }

    bool EventList::remove (int index)
    {
        if (index < 0 || index >= (int) list.size())
            return false;
        list.erase (list.begin() + index);
        return true;
    }

    HomePose homePoseAt (const std::vector<Event>& events, int stem, double beat, HomePose stemDefault)
    {
        for (auto it = events.rbegin(); it != events.rend(); ++it)
        {
            if (it->stem != stem || it->beat > beat || ! it->active || EventList::slotKind (*it) != 0)
                continue;
            if (it->type == EventType::resetpos)
                return stemDefault;
            return { it->azimuth, it->elevation };
        }
        return stemDefault;
    }
}
