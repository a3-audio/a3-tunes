// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "a3t/Model.h"

#include <utility>
#include <vector>

// Tags and chapters are a view of a3t.json for ffprobe, VLC and file managers; a3t.json stays the truth.
namespace a3t
{
    using Tag = std::pair<juce::String, juce::String>;

    // Fixed order; empty values are omitted, A3T_UUID and A3T_VERSION are always there.
    std::vector<Tag> tagsFor (const A3tData&);

    struct Chapter
    {
        double start = 0.0, end = 0.0;
        juce::String title;
    };

    // Hot and memory cues by time; each chapter ends where the next starts, the last at lengthSeconds.
    // Every chapter lies inside [0, lengthSeconds]: cues at or past the end are left out.
    std::vector<Chapter> chaptersFor (const A3tData&, double lengthSeconds);
}
