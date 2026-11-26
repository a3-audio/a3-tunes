// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Internal: a Matroska file built in the order libav wants it — streams, attachments, chapters and
// tags first, then start(), packets, finish().
#pragma once
#include "AvPtr.h"

namespace a3t
{
class MkvMuxer
{
public:
    MkvMuxer() = default;
    ~MkvMuxer();
    MkvMuxer (const MkvMuxer&) = delete;
    MkvMuxer& operator= (const MkvMuxer&) = delete;

    bool create (const juce::File&);
    // The stream index, or -1. `timeBase` is the one the source's packets are in.
    int addAudio (const AVCodecParameters&, AVRational timeBase, const juce::String& name, bool isDefault);
    bool addAttachment (const juce::String& filename, const juce::String& mimetype, const juce::MemoryBlock&);
    bool addChapter (juce::int64 id, double startSeconds, double endSeconds, const juce::String& title);
    bool setTag (const juce::String& key, const juce::String& value);

    bool start();
    // Takes the packet's data; its timestamps are in `from`.
    bool write (AVPacket&, int stream, AVRational from);
    bool finish();

    const juce::String& error() const { return lastError; }

private:
    bool fail (const juce::String& what, int code = 0);
    void closeOutput();

    AVFormatContext* context = nullptr;
    bool outputOpen = false;
    juce::String lastError;
};
}
