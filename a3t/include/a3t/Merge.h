// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "a3t/Sidecar.h"

#include <set>
#include <vector>

// Laptop-side merge of the rig's sidecar: a list of per-key differences the DJ
// chooses from, and the application of that choice.
namespace a3t
{
    struct Change
    {
        enum class Section { event, cue, loop, phrase, grid, analysis, trackField } section = Section::event;
        enum class Kind { added, removed, changed } kind = Kind::changed;
        // Stable: "event:2:32.000000:position", "cue:hot:3", "cue:memory:31.200000",
        // "loop:3", "loop:in:61.000000", "phrase:96.000000", "trackField:rating",
        // "grid", "analysis:bpm", "analysis:key", "analysis:loudness".
        juce::String key;
        juce::String laptop, rig;   // compact JSON of the item; empty when absent
        bool automatic = false;     // playCount: the larger value wins, nobody chooses
    };

    // Only sections present in the sidecar are compared; a present section is complete.
    std::vector<Change> diff (const A3tData& laptop, const Sidecar& rig);

    // Starts from the laptop; takes the rig's version of every key in takeFromRig.
    // Automatic changes apply regardless. revision + 1, uuid unchanged.
    A3tData applyChanges (const A3tData& laptop, const Sidecar& rig, const std::set<juce::String>& takeFromRig);

    // Assumes the sidecar belongs to the data (see belongsTo).
    // The laptop was saved after the rig's sidecar was made.
    bool laptopChangedSince (const A3tData&, const Sidecar&);
}
