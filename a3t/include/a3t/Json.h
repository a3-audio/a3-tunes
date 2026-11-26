// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "a3t/Model.h"
#include "a3t/Quantise.h"


namespace a3t
{
    enum class Status { none, ok, damaged, newer };
    struct Loaded { A3tData data; Status status = Status::none; };

    Loaded parse (const juce::String& json);          // never throws
    juce::String serialise (const A3tData&);          // pretty-printed, stable key order

    // The "track" object alone; the sidecar overlay reuses these.
    juce::var trackToVar (const TrackInfo&);
    TrackInfo trackFromVar (const juce::var&);

    // Sections of a3t.json, shared with the sidecar. *FromVar skips bad items;
    // a void or non-array input gives an empty list.
    juce::var cuesToVar (const std::vector<Cue>&);
    std::vector<Cue> cuesFromVar (const juce::var&);
    juce::var loopsToVar (const std::vector<Loop>&);
    std::vector<Loop> loopsFromVar (const juce::var&);
    juce::var phrasesToVar (const std::vector<Phrase>&);
    std::vector<Phrase> phrasesFromVar (const juce::var&);
    juce::var eventsToVar (const std::vector<Event>&);
    std::vector<Event> eventsFromVar (const juce::var&);   // capped at maxEvents
    juce::var analysisToVar (const Analysis&);             // only the members that are set
    Analysis analysisFromVar (const juce::var&);

    // JUCE's parser recurses without a limit; check before handing it text.
    bool nestsTooDeep (const juce::String& text);
}
