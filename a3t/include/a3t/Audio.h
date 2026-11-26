// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>

#include <memory>
#include <optional>

namespace a3t
{
struct SourceInfo
{
    int sampleRate = 0, channels = 0, bitDepth = 0;
    juce::int64 lengthFrames = 0;
    juce::String codecName;
    juce::int64 bitrate = 0;
};

// The audioTrack-th audio stream (0-based among audio streams only); nullopt if unreadable.
std::optional<SourceInfo> probeAudio (const juce::File&, int audioTrack = 0);

// Decodes one audio stream to planar float at the stream's own rate and channel count.
class TrackDecoder
{
public:
    static std::unique_ptr<TrackDecoder> open (const juce::File&, int audioTrack);
    ~TrackDecoder();

    int sampleRate() const;
    int channels() const;
    juce::int64 lengthFrames() const;

    // Sample-exact: lands on the container's nearest earlier point, then decodes up to `frame`.
    bool seek (juce::int64 frame);
    // Frames read; 0 at the end (and on every call after it).
    int read (float* const* channelData, int numFrames);

private:
    struct Impl;
    explicit TrackDecoder (std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
};
}
