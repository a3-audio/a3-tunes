// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Audio.h"
#include "a3t/Json.h"
#include "a3t/Merge.h"
#include "a3t/Reader.h"
#include "a3t/Sidecar.h"
#include "a3t/Writer.h"

#include <juce_core/juce_core.h>

#include <iostream>
#include <map>
#include <set>

namespace
{
    constexpr int exitOk = 0, exitUsage = 1, exitFailure = 2;

    const char* const usageText =
        "usage:\n"
        "  a3t build  --original F --stem1 F --stem2 F --stem3 F --stem4 F --out F\n"
        "             [--meta meta.json] [--cover F] [--convert RATE/DEPTH]\n"
        "  a3t info   FILE\n"
        "  a3t verify FILE\n"
        "  a3t merge  FILE --take all|none|KEY[,KEY...] [--list]\n";

    // Relative paths are the shell's, so they resolve against the working directory.
    juce::File fileArg (const juce::String& path)
    {
        return juce::File::getCurrentWorkingDirectory().getChildFile (path);
    }

    int usageError (const juce::String& why)
    {
        std::cerr << "a3t: " << why << "\n" << usageText;
        return exitUsage;
    }

    int failure (const juce::String& why)
    {
        std::cerr << "a3t: " << why << "\n";
        return exitFailure;
    }

    // Flags that take a value, flags that do not, and the positional arguments in between.
    struct Options
    {
        std::map<juce::String, juce::String> values;
        std::set<juce::String> switches;
        juce::StringArray positional;
        juce::String error;

        bool has (const juce::String& name) const { return values.count (name) > 0 || switches.count (name) > 0; }
        juce::String value (const juce::String& name) const
        {
            const auto it = values.find (name);
            return it == values.end() ? juce::String() : it->second;
        }
    };

    Options parseOptions (const juce::StringArray& args, const juce::StringArray& valueFlags,
                          const juce::StringArray& switchFlags)
    {
        Options options;
        for (int i = 0; i < args.size() && options.error.isEmpty(); ++i)
        {
            const auto& arg = args[i];
            if (! arg.startsWith ("--"))
                options.positional.add (arg);
            else if (switchFlags.contains (arg))
                options.switches.insert (arg);
            else if (! valueFlags.contains (arg))
                options.error = "unknown option " + arg;
            else if (i + 1 >= args.size() || args[i + 1].startsWith ("--"))
                options.error = "option " + arg + " needs a value";
            else if (options.has (arg))
                options.error = "option " + arg + " given twice";
            else
                options.values[arg] = args[++i];
        }
        return options;
    }

    juce::String missingOf (const Options& options, const juce::StringArray& required)
    {
        for (const auto& name : required)
            if (! options.has (name))
                return "missing " + name;
        return {};
    }

    juce::String statusName (a3t::Status s)
    {
        switch (s)
        {
            case a3t::Status::ok: return "ok";
            case a3t::Status::damaged: return "damaged";
            case a3t::Status::newer: return "newer";
            case a3t::Status::none: break;
        }
        return "none";
    }

    juce::String whyNotOpened (const a3t::OpenResult& opened)
    {
        switch (opened.status)
        {
            case a3t::Status::damaged: return "a3t.json is missing or unreadable";
            case a3t::Status::newer: return "the file was written by a newer a3t";
            case a3t::Status::none: return "not a readable a3mkv file";
            case a3t::Status::ok: break;
        }
        return {};
    }

    // --- build ----------------------------------------------------------------------------------

    bool isDigits (const juce::String& text, int maxLength)
    {
        return text.isNotEmpty() && text.length() <= maxLength && text.containsOnly ("0123456789");
    }

    // "RATE/DEPTH": exactly one slash, a rate in 1..768000 and a depth of 16 or 24.
    bool parseConvert (const juce::String& text, a3t::ConvertTarget& target)
    {
        constexpr int maxRate = 768000;
        const auto slash = text.indexOfChar ('/');
        if (slash < 0 || text.indexOfChar (slash + 1, '/') >= 0)
            return false;
        const auto rateText = text.substring (0, slash), depthText = text.substring (slash + 1);
        if (! isDigits (rateText, 7) || ! isDigits (depthText, 2))
            return false;
        const auto rate = rateText.getIntValue(), depth = depthText.getIntValue();
        if (rate <= 0 || rate > maxRate || (depth != 16 && depth != 24))
            return false;
        target = { false, rate, depth };
        return true;
    }

    // A meta file may be any subset of a3t.json, and may lack "version"; parse itself stays strict.
    bool parseMeta (const juce::File& metaFile, bool hasMeta, a3t::A3tData& data, juce::String& error)
    {
        auto text = hasMeta ? metaFile.loadFileAsString() : juce::String ("{}");
        if (hasMeta && ! metaFile.existsAsFile())
        {
            error = "cannot read " + metaFile.getFullPathName();
            return false;
        }
        if (a3t::nestsTooDeep (text))
        {
            error = "the meta file nests too deeply";
            return false;
        }
        const auto json = juce::JSON::parse (text);
        auto* object = json.getDynamicObject();
        if (object == nullptr)
        {
            error = "the meta file is not a JSON object";
            return false;
        }
        if (! object->hasProperty ("version"))
            object->setProperty ("version", a3t::formatVersion);
        const auto loaded = a3t::parse (juce::JSON::toString (json));
        if (loaded.status != a3t::Status::ok)
        {
            error = "the meta file is not valid a3t.json (" + statusName (loaded.status) + ")";
            return false;
        }
        data = loaded.data;
        return true;
    }

    int build (const juce::StringArray& args)
    {
        const auto options = parseOptions (args, { "--original", "--stem1", "--stem2", "--stem3", "--stem4", "--meta",
                                                   "--out", "--cover", "--convert" }, {});
        if (options.error.isNotEmpty() || options.positional.size() > 0)
            return usageError (options.error.isNotEmpty() ? options.error : "unexpected argument " + options.positional[0]);
        const auto missing = missingOf (options, { "--original", "--stem1", "--stem2", "--stem3", "--stem4", "--out" });
        if (missing.isNotEmpty())
            return usageError (missing);

        a3t::WriteRequest request;
        if (options.has ("--convert") && ! parseConvert (options.value ("--convert"), request.convert))
            return usageError ("--convert takes RATE/DEPTH, for example 44100/24 (depth 16 or 24)");

        juce::String error;
        if (! parseMeta (fileArg (options.value ("--meta")), options.has ("--meta"), request.data, error))
            return failure (error);
        if (request.data.revision == 0)
            request.data.revision = 1;

        request.original = fileArg (options.value ("--original"));
        for (int s = 0; s < a3t::numStems; ++s)
            request.stems[(size_t) s] = fileArg (options.value ("--stem" + juce::String (s + 1)));
        if (options.has ("--cover"))
            request.cover = fileArg (options.value ("--cover"));
        request.output = fileArg (options.value ("--out"));

        const auto result = a3t::writeTrack (request);
        if (! result.ok)
            return failure (result.error);
        std::cout << request.output.getFullPathName() << "\n";
        return exitOk;
    }

    // --- info -----------------------------------------------------------------------------------

    // Tells merged data from rig-overlaid data.
    juce::String sidecarLine (const a3t::OpenResult& opened)
    {
        if (opened.appliedSidecar != juce::File())
            return "sidecar: applied (" + opened.appliedSidecar.getFullPathName() + ")";
        if (opened.error.isNotEmpty())
            return "sidecar: ignored (" + opened.error + ")";
        return "sidecar: none";
    }

    int info (const juce::StringArray& args)
    {
        const auto options = parseOptions (args, {}, {});
        if (options.error.isNotEmpty() || options.positional.size() != 1)
            return usageError (options.error.isNotEmpty() ? options.error : "info takes one FILE");

        const auto file = fileArg (options.positional[0]);
        const auto opened = a3t::openTrackWithSidecar (file);
        if (opened.status == a3t::Status::none || opened.status == a3t::Status::newer)
            return failure (whyNotOpened (opened));

        if (opened.status == a3t::Status::ok)
            std::cout << a3t::serialise (opened.data) << "\n";
        std::cout << "status: " << statusName (opened.status) << "\n"
                  << "tracks: " << opened.layout.audioTracks << "\n"
                  << "rate: " << opened.layout.sampleRate << "\n"
                  << "frames: " << opened.layout.lengthFrames << "\n"
                  << sidecarLine (opened) << "\n";
        return exitOk;
    }

    // --- verify ---------------------------------------------------------------------------------

    int verify (const juce::StringArray& args)
    {
        const auto options = parseOptions (args, {}, {});
        if (options.error.isNotEmpty() || options.positional.size() != 1)
            return usageError (options.error.isNotEmpty() ? options.error : "verify takes one FILE");

        const auto file = fileArg (options.positional[0]);
        const auto opened = a3t::openTrack (file);
        if (opened.status != a3t::Status::ok)
            return failure (whyNotOpened (opened));
        if (! opened.layout.isFiveTrack)
            return failure ("expected 5 audio tracks, found " + juce::String (opened.layout.audioTracks));

        for (int track = 0; track < 1 + a3t::numStems; ++track)
        {
            const auto frames = a3t::countFrames (file, track);
            if (! frames.has_value())
                return failure ("audio track " + juce::String (track + 1) + " cannot be decoded");
            if (*frames != opened.layout.lengthFrames)
                return failure ("audio track " + juce::String (track + 1) + " has " + juce::String (*frames)
                                + " frames, the original has " + juce::String (opened.layout.lengthFrames));
        }
        return exitOk;
    }

    // --- merge ----------------------------------------------------------------------------------

    struct MergeInput
    {
        a3t::A3tData laptop;
        a3t::Sidecar rig;
        juce::File sidecarFile;
        juce::String error;
    };

    MergeInput loadMergeInput (const juce::File& file)
    {
        MergeInput in;
        const auto opened = a3t::openTrack (file);
        if (opened.status != a3t::Status::ok)
        {
            in.error = whyNotOpened (opened);
            return in;
        }
        in.laptop = opened.data;

        in.sidecarFile = a3t::sidecarFileFor (file);
        if (! in.sidecarFile.existsAsFile())
        {
            const auto byUuid = a3t::findSidecarByUuid (file.getParentDirectory(), in.laptop.uuid);
            if (! byUuid.has_value())
            {
                in.error = "no sidecar found for this track";
                return in;
            }
            in.sidecarFile = *byUuid;
        }
        const auto loaded = a3t::loadSidecar (in.sidecarFile);
        if (loaded.status != a3t::Status::ok)
            in.error = "the sidecar is " + statusName (loaded.status) + ": " + in.sidecarFile.getFullPathName();
        else if (! a3t::belongsTo (loaded.sidecar, in.laptop))
            in.error = "the sidecar belongs to another track: " + in.sidecarFile.getFullPathName();
        in.rig = loaded.sidecar;
        return in;
    }

    void printChanges (const std::vector<a3t::Change>& changes)
    {
        const char* const kinds[] = { "added", "removed", "changed" };
        for (const auto& c : changes)
            std::cout << c.key << "\t" << kinds[(int) c.kind] << "\t" << c.laptop << "\t" << c.rig
                      << (c.automatic ? "\tautomatic" : "") << "\n";
    }

    // Splits on commas and keeps empty elements, so "a,,b" can be told from "a,b".
    juce::StringArray splitKeys (const juce::String& text)
    {
        juce::StringArray keys;
        int start = 0;
        for (int comma; (comma = text.indexOfChar (start, ',')) >= 0; start = comma + 1)
            keys.add (text.substring (start, comma));
        keys.add (text.substring (start));
        return keys;
    }

    // "all", "none" or a comma-separated list without empty elements; empty keys on error.
    bool validTake (const juce::String& take)
    {
        if (take == "all" || take == "none")
            return true;
        for (const auto& key : splitKeys (take))
            if (key.trim().isEmpty())
                return false;
        return true;
    }

    // Empty set for "none"; every chosen key must exist in the diff, otherwise `error` says which.
    std::set<juce::String> keysToTake (const juce::String& take, const std::vector<a3t::Change>& changes,
                                       juce::String& error)
    {
        std::set<juce::String> known, chosen;
        for (const auto& c : changes)
        {
            known.insert (c.key);
            if (take == "all" && ! c.automatic)
                chosen.insert (c.key);
        }
        if (take == "all" || take == "none")
            return chosen;
        for (const auto& key : splitKeys (take))
        {
            if (known.count (key) == 0)
                error = "no such change: " + key;
            chosen.insert (key);
        }
        return chosen;
    }

    int merge (const juce::StringArray& args)
    {
        const auto options = parseOptions (args, { "--take" }, { "--list" });
        if (options.error.isNotEmpty() || options.positional.size() != 1)
            return usageError (options.error.isNotEmpty() ? options.error : "merge takes one FILE");
        if (! options.has ("--list") && ! options.has ("--take"))
            return usageError ("merge needs --take or --list");
        if (options.has ("--list") && options.has ("--take"))
            return usageError ("--take and --list cannot be combined");
        if (options.has ("--take") && ! validTake (options.value ("--take")))
            return usageError ("--take needs all, none or KEY[,KEY...] without empty keys");

        const auto file = fileArg (options.positional[0]);
        const auto in = loadMergeInput (file);
        if (in.error.isNotEmpty())
            return failure (in.error);

        if (a3t::laptopChangedSince (in.laptop, in.rig))
            std::cerr << "a3t: note: the laptop's copy changed after the rig's sidecar was made\n";

        const auto changes = a3t::diff (in.laptop, in.rig);
        if (options.has ("--list"))
        {
            printChanges (changes);
            return exitOk;
        }

        juce::String error;
        const auto take = options.value ("--take");
        const auto chosen = keysToTake (take, changes, error);
        if (error.isNotEmpty())
            return failure (error);
        if (changes.empty())
        {
            std::cerr << "a3t: nothing to merge\n";
            return exitOk;
        }
        if (take == "none")
            return exitOk;

        const auto merged = a3t::applyChanges (in.laptop, in.rig, chosen);
        const auto written = a3t::rewriteMetadata (file, merged);
        if (! written.ok)
            return failure (written.error);
        // Only after the file is safely rewritten: an empty sidecar deletes its file.
        if (! a3t::saveSidecar (in.sidecarFile, a3t::Sidecar()))
            return failure ("merged, but the sidecar could not be removed: " + in.sidecarFile.getFullPathName());
        return exitOk;
    }
}

int main (int argc, char** argv)
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));
    if (args.isEmpty())
        return usageError ("no command given");

    const auto command = args[0];
    args.remove (0);
    if (command == "build")  return build (args);
    if (command == "info")   return info (args);
    if (command == "verify") return verify (args);
    if (command == "merge")  return merge (args);
    return usageError ("unknown command " + command);
}
