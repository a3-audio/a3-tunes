// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Events.h"
#include "a3t/Quantise.h"

#include <gtest/gtest.h>

#include <limits>

using namespace a3t;

namespace
{
    juce::var parse (const char* json) { return juce::JSON::parse (juce::String (json)); }

    const char* v1Shot = R"({"stem":1,"beat":8.5,"shape":"ping","bars":2,"reversed":true,
        "seed":"123","active":false,"origin":"prepared"})";

    Event shotAt (int stem, double beat)
    {
        Event e;
        e.stem = stem;
        e.beat = beat;
        return e;
    }

    Event setposAt (int stem, double beat, double az = 0.0, double el = 0.0)
    {
        Event e;
        e.type = EventType::setpos;
        e.stem = stem;
        e.beat = beat;
        e.azimuth = az;
        e.elevation = el;
        return e;
    }

    Event resetposAt (int stem, double beat)
    {
        Event e;
        e.type = EventType::resetpos;
        e.stem = stem;
        e.beat = beat;
        return e;
    }
}

TEST (Events, ShotRoundTripsAsToday)
{
    const auto e = eventFromVar (parse (v1Shot));
    ASSERT_TRUE (e.has_value());
    EXPECT_EQ (e->type, EventType::shot);
    EXPECT_EQ (e->stem, 1);
    EXPECT_EQ (e->beat, 8.5);
    EXPECT_EQ (e->shape, Shape::ping);
    EXPECT_EQ (e->bars, 2.0);
    EXPECT_TRUE (e->reversed);
    EXPECT_EQ (e->seed, 123u);
    EXPECT_FALSE (e->active);
    EXPECT_EQ (e->origin, Origin::prepared);

    const auto written = toVar (*e);
    EXPECT_EQ (written["type"].toString(), "shot");
    EXPECT_EQ ((double) written["speed"], 1.0);
    EXPECT_EQ (written["shape"].toString(), "ping");
    EXPECT_EQ (written["seed"].toString(), "123");
    const auto again = eventFromVar (written);
    ASSERT_TRUE (again.has_value());
    EXPECT_TRUE (*again == *e);
}

TEST (Events, MissingSpeedIsOne)
{
    const auto e = eventFromVar (parse (v1Shot));
    ASSERT_TRUE (e.has_value());
    EXPECT_EQ (e->speed, 1.0);
}

TEST (Events, BarsOutsideTheSetReadAsFour)
{
    auto v = parse (v1Shot);
    v.getDynamicObject()->setProperty ("bars", 3);
    EXPECT_EQ (eventFromVar (v)->bars, 4.0);
    v.getDynamicObject()->setProperty ("bars", 0.5);
    EXPECT_EQ (eventFromVar (v)->bars, 0.5);
    EXPECT_EQ (validBars (8.0), 8.0);
    EXPECT_EQ (validSpeed (3.0), 1.0);
    EXPECT_EQ (validSpeed (0.25), 0.25);
}

TEST (Events, SeedIsADecimalString)
{
    auto v = parse (v1Shot);
    v.getDynamicObject()->setProperty ("seed", "18446744073709551615");
    const auto e = eventFromVar (v);
    ASSERT_TRUE (e.has_value());
    EXPECT_EQ (e->seed, 18446744073709551615ull);
    EXPECT_EQ (toVar (*e)["seed"].toString(), "18446744073709551615");

    v.getDynamicObject()->setProperty ("seed", "abc");
    EXPECT_FALSE (eventFromVar (v).has_value());
    v.getDynamicObject()->setProperty ("seed", "123456789012345678901");
    EXPECT_FALSE (eventFromVar (v).has_value());
}

TEST (Events, StemOutsideZeroToThreeIsSkipped)
{
    auto v = parse (v1Shot);
    v.getDynamicObject()->setProperty ("stem", 4);
    EXPECT_FALSE (eventFromVar (v).has_value());
    v.getDynamicObject()->setProperty ("stem", -1);
    EXPECT_FALSE (eventFromVar (v).has_value());
    v.getDynamicObject()->setProperty ("stem", 3);
    EXPECT_TRUE (eventFromVar (v).has_value());
}

TEST (Events, SetposRoundTrips)
{
    const auto e = eventFromVar (parse (R"({"type":"setpos","stem":2,"beat":16,"azimuth":-45,"elevation":20})"));
    ASSERT_TRUE (e.has_value());
    EXPECT_EQ (e->type, EventType::setpos);
    EXPECT_EQ (e->azimuth, -45.0);
    EXPECT_EQ (e->elevation, 20.0);
    const auto again = eventFromVar (toVar (*e));
    ASSERT_TRUE (again.has_value());
    EXPECT_TRUE (*again == *e);
}

TEST (Events, UnknownTypeIsKeptVerbatim)
{
    const auto v = parse (R"({"type":"glide","beat":1,"stem":0,"x":7})");
    const auto e = eventFromVar (v);
    ASSERT_TRUE (e.has_value());
    EXPECT_EQ (e->type, EventType::unknown);
    const auto written = toVar (*e);
    EXPECT_EQ (juce::JSON::toString (written), juce::JSON::toString (v));
    EXPECT_EQ ((int) written["x"], 7);
}

TEST (Events, ShotAndSetposShareASlot)
{
    EventList list;
    EXPECT_EQ (list.add (shotAt (2, 32)), EventList::Add::added);
    EXPECT_EQ (list.add (setposAt (2, 32)), EventList::Add::added);
}

TEST (Events, SetposAndResetposShareOneSlot)
{
    EventList list;
    EXPECT_EQ (list.add (setposAt (1, 4)), EventList::Add::added);
    EXPECT_EQ (list.add (resetposAt (1, 4)), EventList::Add::occupied);
}

TEST (Events, TwoShotsShareNoSlot)
{
    EventList list;
    EXPECT_EQ (list.add (shotAt (0, 4)), EventList::Add::added);
    EXPECT_EQ (list.add (shotAt (0, 4)), EventList::Add::occupied);
}

TEST (Events, CapIsFiveHundredTwelve)
{
    EventList list;
    for (int i = 0; i < maxEvents; ++i)
        ASSERT_EQ (list.add (shotAt (i % 4, (double) i)), EventList::Add::added);
    EXPECT_EQ (list.add (shotAt (0, 1000)), EventList::Add::full);
    EXPECT_EQ ((int) list.events().size(), maxEvents);
}

TEST (Events, OrderIsBeatStemPositionFirst)
{
    EventList list;
    list.add (shotAt (1, 8));
    list.add (shotAt (0, 8));
    list.add (setposAt (1, 8));
    list.add (shotAt (3, 2));
    const auto& e = list.events();
    ASSERT_EQ (e.size(), 4u);
    EXPECT_EQ (e[0].beat, 2.0);
    EXPECT_EQ (e[1].stem, 0);
    EXPECT_EQ (e[2].type, EventType::setpos);
    EXPECT_EQ (e[3].type, EventType::shot);
    EXPECT_EQ (e[3].stem, 1);
}

TEST (Events, HomePoseIsTheLastPositionEventAtOrBeforeTheBeat)
{
    EventList list;
    list.add (setposAt (0, 10, 90, 5));
    list.add (resetposAt (0, 20));
    list.add (setposAt (0, 15, 45, 0));
    auto inactive = setposAt (0, 5, 10, 10);
    inactive.active = false;
    list.add (inactive);
    Event unknown;
    unknown.type = EventType::unknown;
    unknown.stem = 0;
    unknown.beat = 12;
    unknown.raw = parse (R"({"type":"glide","beat":12,"stem":0})");
    list.add (unknown);

    const HomePose def { 1.0, 2.0 };
    const auto& ev = list.events();
    EXPECT_EQ (homePoseAt (ev, 0, 9.99, def).azimuth, 1.0);
    EXPECT_EQ (homePoseAt (ev, 0, 5.0, def).azimuth, 1.0);   // inactive ignored
    EXPECT_EQ (homePoseAt (ev, 0, 10, def).azimuth, 90.0);
    EXPECT_EQ (homePoseAt (ev, 0, 10, def).elevation, 5.0);
    EXPECT_EQ (homePoseAt (ev, 0, 12, def).azimuth, 90.0);   // unknown ignored
    EXPECT_EQ (homePoseAt (ev, 0, 16, def).azimuth, 45.0);
    EXPECT_EQ (homePoseAt (ev, 0, 20, def).azimuth, 1.0);
    EXPECT_EQ (homePoseAt (ev, 0, 20, def).elevation, 2.0);
    EXPECT_EQ (homePoseAt (ev, 1, 16, def).azimuth, 1.0);
    EXPECT_EQ (homePoseAt ({}, 0, 16, HomePose {}).azimuth, 0.0);
}

TEST (Events, UnknownTypeWithoutStemOrBeatIsKept)
{
    for (const char* json : { R"({"type":"glide","x":1})",
                              R"({"type":"glide","stem":9,"beat":"x","origin":"nobody"})" })
    {
        const auto v = parse (json);
        const auto e = eventFromVar (v);
        ASSERT_TRUE (e.has_value()) << json;
        EXPECT_EQ (e->type, EventType::unknown);
        EXPECT_EQ (e->stem, 0);
        EXPECT_EQ (e->beat, 0.0);
        EXPECT_EQ (juce::JSON::toString (toVar (*e), true), juce::JSON::toString (v, true));
    }
}

TEST (Events, UnknownTypeWithoutStemStillAddsToTheList)
{
    EventList list;
    EXPECT_EQ (list.add (*eventFromVar (parse (R"({"type":"glide"})"))), EventList::Add::added);
    EXPECT_EQ (list.add (*eventFromVar (parse (R"({"type":"glide"})"))), EventList::Add::added);
}

TEST (Events, BadShapeIsSkipped)
{
    auto v = parse (v1Shot);
    v.getDynamicObject()->setProperty ("shape", "spiral");
    EXPECT_FALSE (eventFromVar (v).has_value());
}

TEST (Events, BadOriginOnAKnownTypeIsSkipped)
{
    auto v = parse (v1Shot);
    v.getDynamicObject()->setProperty ("origin", "nobody");
    EXPECT_FALSE (eventFromVar (v).has_value());
}

TEST (Events, MissingOriginDefaultsToHand)
{
    const auto e = eventFromVar (parse (R"({"type":"resetpos","stem":0,"beat":1})"));
    ASSERT_TRUE (e.has_value());
    EXPECT_EQ (e->origin, Origin::hand);
}

TEST (Events, NonFiniteBeatOnAKnownTypeIsSkipped)
{
    auto v = parse (v1Shot);
    v.getDynamicObject()->setProperty ("beat", std::numeric_limits<double>::infinity());
    EXPECT_FALSE (eventFromVar (v).has_value());
    v.getDynamicObject()->setProperty ("beat", std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE (eventFromVar (v).has_value());
}

TEST (Events, DoublesRoundTripExactlyThroughTheText)
{
    Event shot; shot.beat = 0.1 + 0.2;
    Event pos; pos.type = EventType::setpos; pos.beat = 1.0 / 3.0;
    pos.azimuth = 1.0 / 3.0; pos.elevation = 2.0 / 3.0;
    for (const auto& e : { shot, pos })
    {
        const auto once = eventFromVar (juce::JSON::parse (juce::JSON::toString (toVar (e), true)));
        ASSERT_TRUE (once.has_value());
        const auto twice = eventFromVar (juce::JSON::parse (juce::JSON::toString (toVar (*once), true)));
        ASSERT_TRUE (twice.has_value());
        EXPECT_TRUE (*once == *twice);
        EXPECT_EQ (once->beat, quantised (e.beat));
        EXPECT_EQ (once->azimuth, quantised (e.azimuth));
        EXPECT_EQ (once->elevation, quantised (e.elevation));
    }
}

TEST (Events, QuantisedKeepsHugeFiniteValues)
{
    EXPECT_EQ (quantised (1e308), 1e308);
    EXPECT_EQ (quantised (-1e308), -1e308);
    EXPECT_EQ (quantised (1e300), 1e300);
    EXPECT_EQ (quantised (0.1 + 0.2), 0.3);
}
