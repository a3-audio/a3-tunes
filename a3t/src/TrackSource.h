// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Internal: one audio track of the file being written, as a stream of packets — either the
// input's own packets, copied, or its audio encoded to FLAC.
#pragma once
#include "AvPtr.h"

#include <memory>

namespace a3t
{
class TrackSource
{
public:
    virtual ~TrackSource() = default;
    virtual const AVCodecParameters& parameters() const = 0;
    virtual AVRational timeBase() const = 0;
    // The next packet, timestamps from 0 in timeBase(); false at the end or on an error (failed()).
    virtual bool next (AVPacket&) = 0;
    bool failed() const { return broken; }

protected:
    bool broken = false;
};

// The first audio stream's packets, unchanged; nullptr if the file cannot be read.
std::unique_ptr<TrackSource> copyPackets (const juce::File&);

// The first audio stream decoded, converted to `sampleRate` and `bitDepth` (16 or 24) and encoded
// to FLAC, exactly `frames` long (cut or padded with silence); nullptr if it cannot be set up.
std::unique_ptr<TrackSource> encodeFlac (const juce::File&, int sampleRate, int bitDepth, juce::int64 frames);

// Matroska stamps packets in whole milliseconds; a FLAC frame that starts between two of them gets a
// rounded timestamp and a seek lands a few samples off. A block of this size always starts on one.
int millisecondBlockSize (int sampleRate);

// The first audio stream is FLAC whose fixed block size starts every frame on a whole millisecond.
bool hasMillisecondFlacBlocks (const juce::File&);

// Frames a packet copy into Matroska would add at the start: the decoder skips them in the source
// (an MP3's LAME delay, as skip-samples side data), but Matroska carries only what initial_padding
// says (its CodecDelay). 0 when nothing is lost or the file cannot be read.
juce::int64 startTrimLostByCopy (const juce::File&);
}
