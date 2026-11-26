// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Writer.h"

#include "WriteSteps.h"
#include "WriterTesting.h"

namespace a3t
{
bool testing::refuseLossyCopyUpFront = true;

namespace
{
    using namespace writing;

    struct Input
    {
        juce::File file;
        SourceInfo info;
        juce::int64 frames = 0;   // counted by decoding: a declared duration can be rounded
    };
    using Inputs = std::array<Input, numTracks>;

    juce::String readInputs (const WriteRequest& request, Inputs& inputs)
    {
        for (int i = 0; i < numTracks; ++i)
        {
            const auto& file = i == 0 ? request.original : request.stems[(size_t) i - 1];
            const auto info = probeAudio (file);
            const auto frames = info ? countFrames (file) : std::nullopt;
            if (! frames)
                return "cannot read " + trackNames[(size_t) i] + ": " + file.getFullPathName();
            inputs[(size_t) i] = { file, *info, *frames };
        }
        return {};
    }

    juce::int64 framesAt (const Input& in, int rate)
    {
        return av_rescale_rnd (in.frames, rate, in.info.sampleRate, AV_ROUND_NEAR_INF);
    }

    juce::String describe (const Inputs& inputs)
    {
        juce::StringArray parts;
        for (size_t i = 0; i < inputs.size(); ++i)
            parts.add (trackNames[i] + " " + juce::String (inputs[i].frames) + " frames at "
                       + juce::String (inputs[i].info.sampleRate) + " Hz");
        return parts.joinIntoString (", ");
    }

    // Tracks at one rate must match frame for frame; tracks at different rates (conversion only)
    // must match once both are at the target rate.
    bool lengthsAgree (const Input& a, const Input& b, int targetRate)
    {
        if (a.info.sampleRate == b.info.sampleRate)
            return a.frames == b.frames;
        return framesAt (a, targetRate) == framesAt (b, targetRate);
    }

    // The length every written track gets, or an error naming all five.
    juce::String commonLength (const Inputs& inputs, const ConvertTarget& convert, juce::int64& frames)
    {
        const auto& first = inputs[0];
        for (const auto& in : inputs)
            if (convert.keep && in.info.sampleRate != first.info.sampleRate)
                return "sample rates differ (convert to one rate, or give stems at the original's): " + describe (inputs);
        for (size_t i = 0; i < inputs.size(); ++i)
            for (size_t j = i + 1; j < inputs.size(); ++j)
                if (! lengthsAgree (inputs[i], inputs[j], convert.sampleRate))
                    return "track lengths differ: " + describe (inputs);
        frames = convert.keep ? first.frames : framesAt (first, convert.sampleRate);
        return {};
    }

    juce::String loadCover (const juce::File& file, std::vector<Attachment>& attachments)
    {
        if (file == juce::File())
            return {};
        juce::MemoryBlock bytes;
        if (! file.loadFileAsData (bytes))
            return "cannot read the cover: " + file.getFullPathName();
        const auto* b = static_cast<const unsigned char*> (bytes.getData());
        const auto size = bytes.getSize();
        if (size >= 8 && std::memcmp (b, "\x89PNG\r\n\x1a\n", 8) == 0)
            attachments.push_back ({ "cover.png", "image/png", bytes });
        else if (size >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff)
            attachments.push_back ({ "cover.jpg", "image/jpeg", bytes });
        else
            return "the cover is neither JPEG nor PNG: " + file.getFullPathName();
        return {};
    }

    // What a3t.json says about the file itself: its identity and the original as it came in.
    A3tData withFileFacts (A3tData data, const Input& original)
    {
        if (data.uuid.isEmpty())
            data.uuid = newUuid();
        auto& audio = data.track.audio;
        setMeasuredFacts (audio, original.info, original.frames);
        if (audio.source.isEmpty())
            audio.source = original.file.getFileName();
        return data;
    }

    // FLAC keeps 16 bit as 16; anything deeper is stored as 24.
    int keptDepth (int bits)
    {
        return bits <= 16 ? 16 : 24;
    }

    std::unique_ptr<TrackSource> sourceFor (const Input& in, bool isOriginal, const ConvertTarget& convert,
                                            juce::int64 frames)
    {
        if (isOriginal && convert.keep)
            return copyPackets (in.file);
        const auto rate = convert.keep ? in.info.sampleRate : convert.sampleRate;
        const auto depth = convert.keep ? keptDepth (in.info.bitDepth) : convert.bitDepth;
        const auto atTarget = convert.keep || (in.info.sampleRate == rate && in.info.bitDepth == depth);
        if (in.info.codecName == "flac" && atTarget && hasMillisecondFlacBlocks (in.file))
            return copyPackets (in.file);
        return encodeFlac (in.file, rate, depth, frames);
    }

    juce::String openSources (const Inputs& inputs, const ConvertTarget& convert, juce::int64 frames,
                              Sources& sources)
    {
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            sources[i] = sourceFor (inputs[i], i == 0, convert, frames);
            if (sources[i] == nullptr)
                return "cannot prepare " + trackNames[i] + " from " + inputs[i].file.getFullPathName();
        }
        return {};
    }

    // Keep mode copies the original's packets; a gapless lossy original would come out longer.
    juce::String checkOriginalCopy (const Input& original, juce::int64 frames)
    {
        if (! testing::refuseLossyCopyUpFront)
            return {};
        const auto lost = startTrimLostByCopy (original.file);
        return lost > 0 ? lossyCopyAdvice (original.info.codecName, frames + lost, frames) : juce::String();
    }

    juce::String checkRequest (const WriteRequest& request)
    {
        if (request.output == juce::File())
            return "no output file given";
        const auto& c = request.convert;
        if (! c.keep && (c.sampleRate <= 0 || (c.bitDepth != 16 && c.bitDepth != 24)))
            return "conversion needs a positive rate and 16 or 24 bit";
        return {};
    }
}

WriteResult verifyTrack (const juce::File& file)
{
    return verifyExisting (file);
}

WriteResult writeTrack (const WriteRequest& request)
{
    if (const auto error = checkRequest (request); error.isNotEmpty())
        return failure (error);
    const auto output = resolvedTarget (request.output);
    // First, so that it is cleaned up whatever fails after it.
    TemporaryFile temporary (output);
    if (! temporary.exists())
        return failure (cannotCreateTemporary (output));

    Inputs inputs;
    if (const auto error = readInputs (request, inputs); error.isNotEmpty())
        return failure (error);
    juce::int64 frames = 0;
    if (const auto error = commonLength (inputs, request.convert, frames); error.isNotEmpty())
        return failure (error);
    if (request.convert.keep)
        if (const auto error = checkOriginalCopy (inputs[0], frames); error.isNotEmpty())
            return failure (error);
    std::vector<Attachment> attachments;
    if (const auto error = loadCover (request.cover, attachments); error.isNotEmpty())
        return failure (error);

    Sources sources;
    if (const auto error = openSources (inputs, request.convert, frames, sources); error.isNotEmpty())
        return failure (error);
    const auto rate = request.convert.keep ? inputs[0].info.sampleRate : request.convert.sampleRate;
    const auto data = withFileFacts (request.data, inputs[0]);

    const auto copiedCodec = request.convert.keep ? std::optional<juce::String> (inputs[0].info.codecName) : std::nullopt;
    return writeVerified (temporary, output, sources, data, attachments, frames, rate, copiedCodec);
}
}
