// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Internal: the steps every .a3mkv write shares, whether it builds a file from source audio
// (writeTrack) or copies an existing one with new metadata (rewriteMetadata).
#pragma once
#include "TrackSource.h"
#include "a3t/Audio.h"
#include "a3t/Writer.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace a3t::writing
{
constexpr int numTracks = numStems + 1;
extern const std::array<juce::String, numTracks> trackNames;
using Sources = std::array<std::unique_ptr<TrackSource>, numTracks>;

struct Attachment
{
    juce::String filename, mimetype;
    juce::MemoryBlock bytes;
};

// A file beside the target under a random name, created exclusively so that no existing file is
// ever taken over, and removed again unless it was renamed into place. Only a file this object
// created is ever deleted.
class TemporaryFile
{
public:
    explicit TemporaryFile (const juce::File& target);
    ~TemporaryFile();
    TemporaryFile (const TemporaryFile&) = delete;
    TemporaryFile& operator= (const TemporaryFile&) = delete;

    bool exists() const { return created; }
    const juce::File& path() const { return file; }
    // Flushed to disk, then renamed over target in one step; an error text, or empty.
    juce::String moveTo (const juce::File& target);

private:
    bool flushToDisk() const;

    juce::File file;
    bool created = false;
};

// The error for a TemporaryFile that does not exist, while errno still says why.
juce::String cannotCreateTemporary (const juce::File& target);

// The facts a3t.json records about the original, measured from `info` and an exact frame count.
void setMeasuredFacts (AudioInfo&, const SourceInfo& info, juce::int64 frames);

juce::String lossyCopyAdvice (const juce::String& codec, juce::int64 copiedFrames, juce::int64 stemFrames);

// The five sources, a3t.json from `data`, the other attachments, and the tags and chapters derived from
// `data`, into `temporary`; then the verification (every track `frames` long at `rate`, a3t.json exactly
// as serialised) and the rename over `target`. `copiedCodec`: the original was packet-copied from that codec.
WriteResult writeVerified (TemporaryFile& temporary, const juce::File& target, Sources&, const A3tData& data,
                           const std::vector<Attachment>& attachments, juce::int64 frames, int rate,
                           const std::optional<juce::String>& copiedCodec);

WriteResult failure (const juce::String& error);
}
