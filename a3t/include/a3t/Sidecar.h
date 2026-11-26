// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "a3t/Json.h"

#include <optional>
#include <vector>

// The rig's changes to a track, beside the file: a sparse overlay of a3t.json.
namespace a3t
{
    struct Sidecar
    {
        int version = sidecarVersion;
        juce::String basedOnUuid;   // empty: a v1 file, which only knows its name
        int basedOnRevision = 0;
        std::optional<std::vector<Event>> events;
        std::optional<std::vector<Cue>> cues;
        std::optional<std::vector<Loop>> loops;
        std::optional<std::vector<Phrase>> phrases;
        std::optional<Analysis> analysis;   // only the members the rig changed are set
        juce::NamedValueSet trackFields;    // field by field: rating, playCount, colour, comment, ...

        bool empty() const;
    };

    struct LoadedSidecar
    {
        Sidecar sidecar;
        Status status = Status::none;
    };

    // "<name without a final .a3mkv>.a3events.json" beside the track; any other name: append.
    juce::File sidecarFileFor (const juce::File& track);
    // Never throws. v1 files (events only, no based_on) load as ok with an empty basedOnUuid.
    LoadedSidecar loadSidecar (const juce::File&);
    // Atomic. An empty sidecar deletes the file. Refuses (false) to touch a newer or damaged file.
    bool saveSidecar (const juce::File&, const Sidecar&);

    // Sections replace whole; analysis member by member; track fields by name.
    A3tData overlay (const A3tData&, const Sidecar&);
    // A v1 sidecar (no uuid) was found by name and belongs; otherwise the uuids must match.
    bool belongsTo (const Sidecar&, const A3tData&);
    std::optional<juce::File> findSidecarByUuid (const juce::File& folder, const juce::String& uuid);
}
