// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

// A scratch directory that removes itself, so a failing test leaves nothing behind.
struct TempDir
{
    TempDir() : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("a3t-test-" + juce::Uuid().toString()))
    {
        dir.createDirectory();
    }
    ~TempDir() { dir.deleteRecursively(); }
    juce::File dir;
};

// A PCM WAV written by hand (16, 24 or 32 bit): a sine of amplitude 0.5, the same in every channel.
inline juce::File writeTestWav (const juce::File& dir, const juce::String& name, int sampleRate, int channels,
                                int bits, juce::int64 frames, double freqHz)
{
    const int bytes = bits / 8;
    const auto dataSize = (juce::uint32) (frames * channels * bytes);
    std::vector<char> out;
    const auto put32 = [&out] (juce::uint32 v) { for (int i = 0; i < 4; ++i) out.push_back ((char) (v >> (8 * i))); };
    const auto put16 = [&out] (juce::uint16 v) { for (int i = 0; i < 2; ++i) out.push_back ((char) (v >> (8 * i))); };
    const auto putTag = [&out] (const char* t) { out.insert (out.end(), t, t + 4); };

    putTag ("RIFF"); put32 (36 + dataSize); putTag ("WAVE");
    putTag ("fmt "); put32 (16); put16 (1); put16 ((juce::uint16) channels);
    put32 ((juce::uint32) sampleRate); put32 ((juce::uint32) (sampleRate * channels * bytes));
    put16 ((juce::uint16) (channels * bytes)); put16 ((juce::uint16) bits);
    putTag ("data"); put32 (dataSize);

    const double scale = std::pow (2.0, bits - 1) - 1.0;
    for (juce::int64 n = 0; n < frames; ++n)
    {
        const auto sample = (juce::int32) std::lround (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                                     * freqHz * (double) n / sampleRate) * scale);
        for (int c = 0; c < channels; ++c)
            for (int b = 0; b < bytes; ++b)
                out.push_back ((char) (sample >> (8 * b)));
    }

    const auto file = dir.getChildFile (name);
    file.replaceWithData (out.data(), out.size());
    return file;
}
