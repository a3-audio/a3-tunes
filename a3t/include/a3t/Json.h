// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "a3t/Model.h"

namespace a3t
{
    enum class Status { none, ok, damaged, newer };
    struct Loaded { A3tData data; Status status = Status::none; };

    Loaded parse (const juce::String& json);          // never throws
    juce::String serialise (const A3tData&);          // pretty-printed, stable key order

    // The "track" object alone; the sidecar overlay reuses these.
    juce::var trackToVar (const TrackInfo&);
    TrackInfo trackFromVar (const juce::var&);
}
