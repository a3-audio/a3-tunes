// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>
#include <vector>

// A deck's planned events: one-shot movements and home-pose changes.
// Mirrors the a3events file of StemDeck, plus setpos/resetpos.
namespace a3t
{
    enum class Shape { orbit, ping, rise, scat };
    enum class Origin { hand, prepared };
    enum class EventType { shot, setpos, resetpos, unknown };

    struct Event
    {
        EventType type = EventType::shot;
        int stem = 0;
        double beat = 0.0;
        bool active = true;
        Origin origin = Origin::hand;
        // shot
        Shape shape = Shape::orbit;
        double bars = 4.0;
        bool reversed = false;
        double speed = 1.0;
        std::uint64_t seed = 0;
        // setpos
        double azimuth = 0.0, elevation = 0.0;
        // unknown: the object as read
        juce::var raw;
    };

    bool operator== (const Event&, const Event&);

    constexpr int maxEvents = 512;

    // Unknown: returns raw unchanged.
    juce::var toVar (const Event&);
    // nullopt = skip (bad stem, non-finite beat, bad shape/origin/seed).
    std::optional<Event> eventFromVar (const juce::var&);

    double validBars (double);
    double validSpeed (double);

    // Sorted by beat, then stem, then position-before-shot.
    class EventList
    {
    public:
        enum class Add { added, occupied, full };

        Add add (const Event&);
        const std::vector<Event>& events() const { return list; }
        bool remove (int index);
        // 0 = position (setpos, resetpos), 1 = shot, 2 = unknown (never occupied).
        // A slot is (stem, beat, slotKind): one event each.
        static int slotKind (const Event&);

    private:
        std::vector<Event> list;
    };

    struct HomePose
    {
        double azimuth = 0.0, elevation = 0.0;
    };

    // `events` must be sorted as EventList sorts it: the scan runs backwards.
    // The last active position event of the stem at or before the beat;
    // a resetpos (or none) gives the stem's default.
    HomePose homePoseAt (const std::vector<Event>&, int stem, double beat, HomePose stemDefault);
}
