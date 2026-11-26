// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MkvMuxer.h"

#include <cmath>
#include <cstring>

namespace a3t
{
namespace
{
    juce::String describe (int code)
    {
        char text[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror (code, text, sizeof (text));
        return text;
    }

    constexpr AVRational nanoseconds { 1, 1000000000 };

    juce::int64 toNanoseconds (double seconds)
    {
        return (juce::int64) std::llround (seconds * 1e9);
    }
}

MkvMuxer::~MkvMuxer()
{
    closeOutput();
    avformat_free_context (context);
}

bool MkvMuxer::fail (const juce::String& what, int code)
{
    lastError = code < 0 ? what + ": " + describe (code) : what;
    return false;
}

void MkvMuxer::closeOutput()
{
    if (outputOpen)
        avio_closep (&context->pb);
    outputOpen = false;
}

bool MkvMuxer::create (const juce::File& file)
{
    av::quietLogs();
    const auto path = file.getFullPathName().toStdString();
    // The format is named, never guessed: the file name is arbitrary and may have no extension. The
    // path still goes in, as the context's url: moving the cues to the front re-opens the file by it.
    const auto result = avformat_alloc_output_context2 (&context, nullptr, "matroska", path.c_str());
    if (result < 0)
        return fail ("cannot create a Matroska muxer", result);
    const auto opened = avio_open (&context->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (opened < 0)
        return fail ("cannot open " + file.getFullPathName() + " for writing", opened);
    outputOpen = true;
    return true;
}

int MkvMuxer::addAudio (const AVCodecParameters& parameters, AVRational timeBase, const juce::String& name,
                        bool isDefault)
{
    auto* stream = avformat_new_stream (context, nullptr);
    if (stream == nullptr || avcodec_parameters_copy (stream->codecpar, &parameters) < 0)
    {
        fail ("cannot add the audio track " + name);
        return -1;
    }
    stream->codecpar->codec_tag = 0;   // the source container's tag means nothing to Matroska
    stream->time_base = timeBase;
    stream->disposition = isDefault ? AV_DISPOSITION_DEFAULT : 0;
    av_dict_set (&stream->metadata, "title", name.toRawUTF8(), 0);
    return stream->index;
}

bool MkvMuxer::addAttachment (const juce::String& filename, const juce::String& mimetype,
                              const juce::MemoryBlock& bytes)
{
    auto* stream = avformat_new_stream (context, nullptr);
    if (stream == nullptr)
        return fail ("cannot add the attachment " + filename);
    auto& parameters = *stream->codecpar;
    parameters.codec_type = AVMEDIA_TYPE_ATTACHMENT;
    parameters.codec_id = AV_CODEC_ID_NONE;
    parameters.extradata = (uint8_t*) av_mallocz (bytes.getSize() + AV_INPUT_BUFFER_PADDING_SIZE);
    if (parameters.extradata == nullptr)
        return fail ("out of memory for the attachment " + filename);
    std::memcpy (parameters.extradata, bytes.getData(), bytes.getSize());
    parameters.extradata_size = (int) bytes.getSize();
    av_dict_set (&stream->metadata, "filename", filename.toRawUTF8(), 0);
    av_dict_set (&stream->metadata, "mimetype", mimetype.toRawUTF8(), 0);
    return true;
}

bool MkvMuxer::addChapter (juce::int64 id, double startSeconds, double endSeconds, const juce::String& title)
{
    // libavformat has no public constructor for chapters; a muxing caller allocates them and
    // avformat_free_context frees them.
    auto* chapter = (AVChapter*) av_mallocz (sizeof (AVChapter));
    if (chapter == nullptr)
        return fail ("out of memory for a chapter");
    chapter->id = id;
    chapter->time_base = nanoseconds;
    chapter->start = toNanoseconds (startSeconds);
    chapter->end = toNanoseconds (endSeconds);
    av_dict_set (&chapter->metadata, "title", title.toRawUTF8(), 0);
    if (av_dynarray_add_nofree (&context->chapters, (int*) &context->nb_chapters, chapter) < 0)
    {
        av_dict_free (&chapter->metadata);
        av_free (chapter);
        return fail ("out of memory for a chapter");
    }
    return true;
}

bool MkvMuxer::setTag (const juce::String& key, const juce::String& value)
{
    return av_dict_set (&context->metadata, key.toRawUTF8(), value.toRawUTF8(), 0) >= 0
               || fail ("cannot set the tag " + key);
}

bool MkvMuxer::start()
{
    AVDictionary* options = nullptr;
    // About one second per cluster and the index at the front: a hot cue is a jump, and a jump on a
    // USB stick has to find its cluster without reading to the end of the file first.
    av_dict_set (&options, "cluster_time_limit", "1000", 0);
    av_dict_set (&options, "cues_to_front", "1", 0);
    const auto result = avformat_write_header (context, &options);
    const auto unused = av_dict_count (options);
    av_dict_free (&options);
    if (result < 0)
        return fail ("cannot write the Matroska header", result);
    return unused == 0 || fail ("the Matroska muxer ignored an option");
}

bool MkvMuxer::write (AVPacket& packet, int stream, AVRational from)
{
    packet.stream_index = stream;
    av_packet_rescale_ts (&packet, from, context->streams[stream]->time_base);
    const auto result = av_interleaved_write_frame (context, &packet);
    return result >= 0 || fail ("cannot write a packet", result);
}

bool MkvMuxer::finish()
{
    const auto result = av_write_trailer (context);
    closeOutput();
    return result >= 0 || fail ("cannot finish the Matroska file", result);
}
}
