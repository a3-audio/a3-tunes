// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Audio.h"
#include "a3t/Json.h"
#include "a3t/Reader.h"
#include "a3t/Sidecar.h"
#include "TestAudio.h"
#include "TestMkv.h"

#include <gtest/gtest.h>

using namespace a3t;

namespace
{
    struct Run
    {
        int exitCode = -1;
        juce::String out, err;
    };

    // stderr goes to a file so it stays apart from stdout (ChildProcess would merge them).
    Run run (const TempDir& t, const juce::StringArray& args)
    {
        const auto errFile = t.dir.getChildFile ("stderr.txt");
        juce::StringArray command { "/bin/sh", "-c", "err=$1; shift; exec \"$@\" 2>\"$err\"", "sh",
                                    errFile.getFullPathName(), A3T_CLI_PATH };
        command.addArray (args);
        juce::ChildProcess child;
        Run result;
        if (! child.start (command, juce::ChildProcess::wantStdOut))
            return result;
        result.out = child.readAllProcessOutput();
        child.waitForProcessToFinish (60000);
        result.exitCode = (int) child.getExitCode();
        result.err = errFile.loadFileAsString();
        return result;
    }

    juce::String path (const juce::File& f) { return f.getFullPathName(); }

    juce::File writeMeta (const TempDir& t, const juce::String& json)
    {
        const auto f = t.dir.getChildFile ("meta.json");
        f.replaceWithText (json);
        return f;
    }

    juce::StringArray buildArgs (const TempDir& t, const juce::File& out, juce::int64 stem3Frames = 20000)
    {
        const auto original = writeTestWav (t.dir, "original.wav", 48000, 2, 16, 20000, 440.0);
        juce::StringArray args { "build", "--original", path (original) };
        for (int s = 1; s <= 4; ++s)
            args.addArray ({ "--stem" + juce::String (s),
                             path (writeTestWav (t.dir, "stem" + juce::String (s) + ".wav", 48000, 2, 16,
                                                 s == 3 ? stem3Frames : 20000, 220.0 * s)) });
        args.addArray ({ "--meta", path (writeMeta (t, R"({"track":{"title":"Cli Title"}})")), "--out", path (out) });
        return args;
    }

    juce::File built (const TempDir& t)
    {
        const auto out = t.dir.getChildFile ("song.a3mkv");
        const auto r = run (t, buildArgs (t, out));
        EXPECT_EQ (r.exitCode, 0) << r.err;
        return out;
    }

    // The rig changed the rating of the track beside `file`.
    juce::File rigSet (const juce::File& file, const juce::String& field, int value, const juce::String& basedOn = {})
    {
        Sidecar s;
        s.basedOnUuid = basedOn.isEmpty() ? openTrack (file).data.uuid : basedOn;
        s.basedOnRevision = 1;
        s.trackFields.set (field, value);
        const auto side = sidecarFileFor (file);
        EXPECT_TRUE (saveSidecar (side, s));
        return side;
    }

    juce::File rigRated (const juce::File& file, int rating, const juce::String& basedOn = {})
    {
        return rigSet (file, "rating", rating, basedOn);
    }

    // Runs `args` and expects a usage error that leaves the file and its sidecar byte-identical.
    void expectUsageErrorAndNothingWritten (const TempDir& t, const juce::File& file, const juce::File& side,
                                            const juce::StringArray& args)
    {
        const auto fileBefore = bytesOf (file);
        const auto sideBefore = bytesOf (side);
        const auto r = run (t, args);
        EXPECT_EQ (r.exitCode, 1) << args.joinIntoString (" ");
        EXPECT_FALSE (r.err.isEmpty());
        EXPECT_EQ (bytesOf (file), fileBefore);
        ASSERT_TRUE (side.existsAsFile());
        EXPECT_EQ (bytesOf (side), sideBefore);
    }
}

TEST (Cli, BuildThenInfoThenVerify)
{
    TempDir t;
    const auto out = t.dir.getChildFile ("song.a3mkv");
    const auto b = run (t, buildArgs (t, out));
    EXPECT_EQ (b.exitCode, 0) << b.err;
    EXPECT_EQ (b.out.trim(), path (out));
    ASSERT_TRUE (out.existsAsFile());

    const auto info = run (t, { "info", path (out) });
    EXPECT_EQ (info.exitCode, 0) << info.err;
    EXPECT_TRUE (info.out.contains ("Cli Title"));
    EXPECT_TRUE (info.out.contains ("\"revision\": 1"));

    const auto v = run (t, { "verify", path (out) });
    EXPECT_EQ (v.exitCode, 0) << v.err;
}

TEST (Cli, VerifyFailsOnAFileThatIsNotAnA3mkv)
{
    TempDir t;
    const auto junk = t.dir.getChildFile ("junk.a3mkv");
    junk.replaceWithText ("this is not matroska");
    const auto r = run (t, { "verify", path (junk) });
    EXPECT_EQ (r.exitCode, 2);
    EXPECT_FALSE (r.err.isEmpty());
}

TEST (Cli, BuildRefusesUnequalStems)
{
    TempDir t;
    const auto out = t.dir.getChildFile ("song.a3mkv");
    const auto r = run (t, buildArgs (t, out, 19990));
    EXPECT_EQ (r.exitCode, 2);
    EXPECT_TRUE (r.err.contains ("19990")) << r.err;
    EXPECT_TRUE (r.err.contains ("20000")) << r.err;
    EXPECT_FALSE (out.exists());
}

TEST (Cli, MergeListShowsTheRigsChange)
{
    TempDir t;
    const auto file = built (t);
    rigRated (file, 5);
    const auto r = run (t, { "merge", path (file), "--list" });
    EXPECT_EQ (r.exitCode, 0) << r.err;
    EXPECT_TRUE (r.out.contains ("trackField:rating")) << r.out;
}

TEST (Cli, MergeTakeAllAppliesAndDeletesTheSidecar)
{
    TempDir t;
    const auto file = built (t);
    const auto side = rigRated (file, 5);
    const auto r = run (t, { "merge", path (file), "--take", "all" });
    EXPECT_EQ (r.exitCode, 0) << r.err;
    EXPECT_FALSE (side.exists());

    const auto info = run (t, { "info", path (file) });
    EXPECT_TRUE (info.out.contains ("\"rating\": 5")) << info.out;
    EXPECT_EQ (openTrack (file).data.revision, 2);
}

TEST (Cli, MergeTakeNoneKeepsTheFile)
{
    TempDir t;
    const auto file = built (t);
    const auto side = rigRated (file, 5);
    const auto fileBefore = bytesOf (file);
    const auto sideBefore = bytesOf (side);
    const auto r = run (t, { "merge", path (file), "--take", "none" });
    EXPECT_EQ (r.exitCode, 0) << r.err;
    EXPECT_EQ (bytesOf (file), fileBefore);
    ASSERT_TRUE (side.existsAsFile());
    EXPECT_EQ (bytesOf (side), sideBefore);
}

TEST (Cli, UsageErrorIsExitOne)
{
    TempDir t;
    EXPECT_EQ (run (t, {}).exitCode, 1);
    EXPECT_EQ (run (t, { "frobnicate" }).exitCode, 1);
    EXPECT_EQ (run (t, { "info" }).exitCode, 1);
    EXPECT_EQ (run (t, { "build", "--original", "x" }).exitCode, 1);
    EXPECT_EQ (run (t, { "info", "f", "--nope", "1" }).exitCode, 1);
    const auto r = run (t, { "frobnicate" });
    EXPECT_FALSE (r.err.isEmpty());
    EXPECT_TRUE (r.out.isEmpty());
}

TEST (Cli, ConvertOptionIsParsed)
{
    TempDir t;
    const auto out = t.dir.getChildFile ("converted.a3mkv");
    auto args = buildArgs (t, out);
    auto bad = args;
    bad.addArray ({ "--convert", "abc" });
    EXPECT_EQ (run (t, bad).exitCode, 1);
    EXPECT_FALSE (out.exists());

    args.addArray ({ "--convert", "44100/24" });
    const auto r = run (t, args);
    ASSERT_EQ (r.exitCode, 0) << r.err;
    const auto info = probeAudio (out, 0);
    ASSERT_TRUE (info.has_value());
    EXPECT_EQ (info->sampleRate, 44100);
    EXPECT_EQ (info->bitDepth, 24);
}

TEST (Cli, MergeUnknownKeyWritesNothing)
{
    TempDir t;
    const auto file = built (t);
    const auto side = rigRated (file, 5);
    const auto fileBefore = bytesOf (file);
    const auto sideBefore = bytesOf (side);
    const auto r = run (t, { "merge", path (file), "--take", "trackField:rating,trackField:nonsense" });
    EXPECT_EQ (r.exitCode, 2);
    EXPECT_TRUE (r.err.contains ("trackField:nonsense")) << r.err;
    EXPECT_EQ (bytesOf (file), fileBefore);
    EXPECT_EQ (bytesOf (side), sideBefore);
}

TEST (Cli, MergeWithAForeignSidecarFails)
{
    TempDir t;
    const auto file = built (t);
    const auto side = rigRated (file, 5, "99999999-9999-9999-9999-999999999999");
    const auto fileBefore = bytesOf (file);
    const auto r = run (t, { "merge", path (file), "--take", "all" });
    EXPECT_EQ (r.exitCode, 2);
    EXPECT_FALSE (r.err.isEmpty());
    EXPECT_EQ (bytesOf (file), fileBefore);
    EXPECT_TRUE (side.existsAsFile());
}

TEST (Cli, InfoOnADamagedFilePrintsStatusAndExitsZero)
{
    TempDir t;
    const auto file = built (t);
    patchBytes (file, "\"version\": 1", "\"version\": x");
    ASSERT_EQ (openTrack (file).status, Status::damaged);
    const auto r = run (t, { "info", path (file) });
    EXPECT_EQ (r.exitCode, 0) << r.err;
    EXPECT_TRUE (r.out.contains ("damaged")) << r.out;
}

TEST (Cli, MergeTakeWithoutKeysIsAUsageErrorAndWritesNothing)
{
    TempDir t;
    const auto file = built (t);
    const auto side = rigRated (file, 5);
    for (const char* take : { "", "  ", ",", "trackField:rating,,trackField:rating", ",trackField:rating" })
        expectUsageErrorAndNothingWritten (t, file, side, { "merge", path (file), "--take", take });
}

TEST (Cli, MergeTakeWithListIsAUsageError)
{
    TempDir t;
    const auto file = built (t);
    const auto side = rigRated (file, 5);
    expectUsageErrorAndNothingWritten (t, file, side, { "merge", path (file), "--take", "all", "--list" });
}

TEST (Cli, MergeListAndTakeNoneLeaveFileAndSidecarIdentical)
{
    TempDir t;
    const auto file = built (t);
    const auto side = rigRated (file, 5);
    const auto fileBefore = bytesOf (file);
    const auto sideBefore = bytesOf (side);
    EXPECT_EQ (run (t, { "merge", path (file), "--list" }).exitCode, 0);
    EXPECT_EQ (bytesOf (file), fileBefore);
    EXPECT_EQ (bytesOf (side), sideBefore);
}

TEST (Cli, InfoSaysWhetherASidecarWasApplied)
{
    TempDir t;
    const auto file = built (t);
    EXPECT_TRUE (run (t, { "info", path (file) }).out.contains ("sidecar: none"));

    const auto side = rigRated (file, 5);
    const auto applied = run (t, { "info", path (file) });
    EXPECT_TRUE (applied.out.contains ("sidecar: applied (" + path (side) + ")")) << applied.out;

    rigRated (file, 5, "99999999-9999-9999-9999-999999999999");
    const auto ignored = run (t, { "info", path (file) });
    EXPECT_TRUE (ignored.out.contains ("sidecar: ignored (")) << ignored.out;
    EXPECT_TRUE (ignored.out.contains ("another track")) << ignored.out;
}

TEST (Cli, ConvertOptionRejectsOddValues)
{
    TempDir t;
    const auto out = t.dir.getChildFile ("converted.a3mkv");
    for (const char* bad : { "44100//24", "44100", "/24", "44100/", "0/24", "44100/32", "-1/24", "+44100/24",
                             "12345678/24", "768001/24", "44100/24/1", "44.1/24", "abc/24", "" })
    {
        auto args = buildArgs (t, out);
        args.addArray ({ "--convert", bad });
        EXPECT_EQ (run (t, args).exitCode, 1) << bad;
        EXPECT_FALSE (out.exists()) << bad;
    }
    for (const char* good : { "48000/16", "768000/24" })
    {
        auto args = buildArgs (t, out);
        args.addArray ({ "--convert", good });
        EXPECT_EQ (run (t, args).exitCode, 0) << good;
        out.deleteFile();
    }
}

TEST (Cli, OptionValueStartingWithDashesIsRefused)
{
    TempDir t;
    const auto r = run (t, { "build", "--out", "--stem1", "x" });
    EXPECT_EQ (r.exitCode, 1);
    EXPECT_TRUE (r.err.contains ("--out")) << r.err;
}

TEST (Cli, RepeatedOptionIsRefused)
{
    TempDir t;
    const auto out = t.dir.getChildFile ("song.a3mkv");
    auto args = buildArgs (t, out);
    args.addArray ({ "--stem1", path (t.dir.getChildFile ("stem2.wav")) });
    const auto r = run (t, args);
    EXPECT_EQ (r.exitCode, 1);
    EXPECT_TRUE (r.err.contains ("--stem1")) << r.err;
    EXPECT_FALSE (out.exists());
}

TEST (Cli, MergeWithNothingToMergeIsANoOp)
{
    TempDir t;
    const auto file = built (t);
    const auto side = rigRated (file, 0);   // the file's own rating: no difference
    const auto fileBefore = bytesOf (file);
    const auto r = run (t, { "merge", path (file), "--take", "all" });
    EXPECT_EQ (r.exitCode, 0) << r.err;
    // A sidecar equal to the file would later override newer laptop edits on the rig.
    EXPECT_TRUE (r.err.contains ("nothing to merge; the sidecar matched the file and was removed")) << r.err;
    EXPECT_EQ (bytesOf (file), fileBefore);
    EXPECT_FALSE (side.exists());
    EXPECT_EQ (openTrack (file).data.revision, 1);
}

TEST (Cli, MergeAppliesAnAutomaticChangeOnItsOwn)
{
    TempDir t;
    const auto file = built (t);
    const auto side = rigSet (file, "playCount", 7);
    const auto r = run (t, { "merge", path (file), "--take", "none" });
    EXPECT_EQ (r.exitCode, 0) << r.err;
    EXPECT_EQ (openTrack (file).data.revision, 1);   // none writes nothing

    EXPECT_EQ (run (t, { "merge", path (file), "--take", "all" }).exitCode, 0);
    EXPECT_EQ (openTrack (file).data.track.playCount, 7);
    EXPECT_EQ (openTrack (file).data.revision, 2);
    EXPECT_FALSE (side.exists());
}

TEST (Cli, VerifyFailsOnUnequalLengths)
{
    TempDir t;
    const auto file = handBuiltFile (t.dir, 44100, 30000, 7, "11111111-2222-3333-4444-555555555555");
    const auto r = run (t, { "verify", path (file) });
    EXPECT_EQ (r.exitCode, 2);
    EXPECT_TRUE (r.err.contains ("30007")) << r.err;
}

TEST (Cli, BuildFailsOnMissingOrBadMeta)
{
    TempDir t;
    const auto out = t.dir.getChildFile ("song.a3mkv");
    auto missing = buildArgs (t, out);
    missing.set (missing.indexOf ("--meta") + 1, path (t.dir.getChildFile ("nope.json")));
    EXPECT_EQ (run (t, missing).exitCode, 2);

    auto bad = buildArgs (t, out);
    bad.set (bad.indexOf ("--meta") + 1, path (writeMeta (t, "{ not json")));
    const auto r = run (t, bad);
    EXPECT_EQ (r.exitCode, 2);
    EXPECT_FALSE (r.err.isEmpty());
    EXPECT_FALSE (out.exists());
}

TEST (Cli, MergeFindsTheSidecarByUuidAfterARename)
{
    TempDir t;
    const auto file = built (t);
    rigRated (file, 5);
    const auto renamed = t.dir.getChildFile ("renamed.a3mkv");
    ASSERT_TRUE (file.moveFileTo (renamed));
    const auto r = run (t, { "merge", path (renamed), "--take", "all" });
    EXPECT_EQ (r.exitCode, 0) << r.err;
    EXPECT_EQ (openTrack (renamed).data.track.rating, 5);
    EXPECT_TRUE (t.dir.findChildFiles (juce::File::findFiles, false, "*.a3events.json").isEmpty());
}

namespace
{
    // True when `text` has a line that starts with `label` and continues with a number.
    bool hasNumberLine (const juce::String& text, const juce::String& label)
    {
        for (const auto& line : juce::StringArray::fromLines (text))
            if (line.startsWith (label) && line.substring (label.length()).trim().containsOnly ("0123456789."))
                return line.substring (label.length()).trim().isNotEmpty();
        return false;
    }
}

TEST (Cli, SeekbenchPrintsMedianAndMax)
{
    TempDir t;
    const auto file = built (t);
    const auto r = run (t, { "seekbench", path (file), "--seeks", "20" });
    EXPECT_EQ (r.exitCode, 0) << r.err;
    EXPECT_TRUE (r.out.contains ("seeks: 20\n"));
    EXPECT_TRUE (hasNumberLine (r.out, "median ms:")) << r.out;
    EXPECT_TRUE (hasNumberLine (r.out, "p95 ms:")) << r.out;
    EXPECT_TRUE (hasNumberLine (r.out, "max ms:")) << r.out;
    EXPECT_TRUE (r.out.contains ("bytes read per second of audio:"));
}

TEST (Cli, SeekbenchRefusesBadArguments)
{
    TempDir t;
    const auto file = built (t);
    EXPECT_EQ (run (t, { "seekbench", path (file), "--seeks", "0" }).exitCode, 1);
    EXPECT_EQ (run (t, { "seekbench", path (file), "--seeks", "abc" }).exitCode, 1);
    EXPECT_EQ (run (t, { "seekbench", path (file), "--seeks", "100001" }).exitCode, 1);
    EXPECT_EQ (run (t, { "seekbench" }).exitCode, 1);

    const auto notMkv = writeTestWav (t.dir, "plain.wav", 48000, 2, 16, 1000, 440.0);
    const auto r = run (t, { "seekbench", path (notMkv) });
    EXPECT_EQ (r.exitCode, 2);
    EXPECT_FALSE (r.err.isEmpty());
}

TEST (Cli, SeekbenchCompareReadsLooseStems)
{
    TempDir t;
    const auto file = built (t);
    const auto loose = t.dir.getChildFile ("loose");
    loose.createDirectory();
    // The decoder probes content, not names, so WAV bytes under the .flac names stand in for FLAC files.
    for (int s = 1; s <= 4; ++s)
        writeTestWav (loose, "stem" + juce::String (s) + ".flac", 48000, 2, 16, 20000, 220.0 * s);

    const auto r = run (t, { "seekbench", path (file), "--seeks", "5", "--compare", path (loose) });
    EXPECT_EQ (r.exitCode, 0) << r.err;
    EXPECT_TRUE (r.out.contains ("separate files:\nseeks: 5\n"));
    EXPECT_TRUE (hasNumberLine (r.out, "median ms:")) << r.out;
    EXPECT_TRUE (r.out.fromFirstOccurrenceOf ("separate files:", false, false).contains ("max ms:"));

    const auto missing = run (t, { "seekbench", path (file), "--compare", path (t.dir.getChildFile ("nope")) });
    EXPECT_EQ (missing.exitCode, 2);
    EXPECT_FALSE (missing.err.isEmpty());
}

TEST (Cli, HugeFiniteValuesSurviveBuildAndInfo)
{
    TempDir t;
    const auto out = t.dir.getChildFile ("song.a3mkv");
    auto args = buildArgs (t, out);
    args.set (args.indexOf ("--meta") + 1, path (writeMeta (t, R"({"cues":[{"kind":"memory","time":1e300}],
        "events":[{"type":"setpos","stem":1,"beat":2,"azimuth":1e305,"elevation":0}]})")));
    const auto b = run (t, args);
    ASSERT_EQ (b.exitCode, 0) << b.err;
    const auto data = openTrack (out).data;
    ASSERT_EQ (data.cues.size(), 1u);
    EXPECT_EQ (data.cues[0].time, 1e300);
    ASSERT_EQ (data.events.size(), 1u);
    EXPECT_EQ (data.events[0].azimuth, 1e305);
    const auto info = run (t, { "info", path (out) });
    EXPECT_EQ (info.exitCode, 0) << info.err;
    // info prints a3t.json first; read it back rather than matching JUCE's number text.
    const auto shown = parse (info.out.upToFirstOccurrenceOf ("\nstatus:", false, false));
    ASSERT_EQ (shown.status, Status::ok) << info.out;
    ASSERT_EQ (shown.data.cues.size(), 1u);
    EXPECT_EQ (shown.data.cues[0].time, 1e300);
    ASSERT_EQ (shown.data.events.size(), 1u);
    EXPECT_EQ (shown.data.events[0].azimuth, 1e305);
}

namespace
{
    // `count` resetpos events on one stem, beats 0, 1, 2, ... as a JSON array.
    juce::String resetposJson (int count, int stem)
    {
        juce::StringArray items;
        for (int i = 0; i < count; ++i)
            items.add (R"({"type":"resetpos","stem":)" + juce::String (stem) + R"(,"beat":)" + juce::String (i) + "}");
        return "[" + items.joinIntoString (",") + "]";
    }
}

TEST (Cli, BuildRefusesMoreEventsThanTheCap)
{
    TempDir t;
    const auto out = t.dir.getChildFile ("song.a3mkv");
    auto args = buildArgs (t, out);
    args.set (args.indexOf ("--meta") + 1, path (writeMeta (t, R"({"events":)" + resetposJson (maxEvents + 1, 0) + "}")));
    const auto r = run (t, args);
    EXPECT_EQ (r.exitCode, 2);
    EXPECT_TRUE (r.err.contains ("513")) << r.err;
    EXPECT_FALSE (out.exists());
}

// The rig dropped 100 of the laptop's 500 events and added 112; taking only the additions gives 612.
TEST (Cli, MergeThatWouldExceedTheEventCapWritesNothing)
{
    TempDir t;
    const auto out = t.dir.getChildFile ("song.a3mkv");
    auto args = buildArgs (t, out);
    args.set (args.indexOf ("--meta") + 1, path (writeMeta (t, R"({"events":)" + resetposJson (500, 0) + "}")));
    ASSERT_EQ (run (t, args).exitCode, 0);
    const auto side = sidecarFileFor (out);
    side.replaceWithText (R"({"version":2,"based_on":{"uuid":")" + openTrack (out).data.uuid
                          + R"(","revision":1},"events":)" + resetposJson (400, 0).dropLastCharacters (1) + ","
                          + resetposJson (112, 1).substring (1) + "}");
    juce::StringArray added;
    for (const auto& line : juce::StringArray::fromLines (run (t, { "merge", path (out), "--list" }).out))
        if (line.contains ("\tadded\t"))
            added.add (line.upToFirstOccurrenceOf ("\t", false, false));
    ASSERT_EQ (added.size(), 112);

    const auto fileBefore = bytesOf (out);
    const auto sideBefore = bytesOf (side);
    const auto r = run (t, { "merge", path (out), "--take", added.joinIntoString (",") });
    EXPECT_EQ (r.exitCode, 2) << r.err;
    EXPECT_TRUE (r.err.contains ("612 events")) << r.err;
    EXPECT_EQ (bytesOf (out), fileBefore);
    ASSERT_TRUE (side.existsAsFile());
    EXPECT_EQ (bytesOf (side), sideBefore);
}

TEST (Cli, BuildRefusesNestingBehindASingleQuotedString)
{
    TempDir t;
    const auto out = t.dir.getChildFile ("song.a3mkv");
    auto args = buildArgs (t, out);
    args.set (args.indexOf ("--meta") + 1, path (writeMeta (t, R"({"version":1,"x":['"',)"
                                                                     + juce::String::repeatedString ("[", 300000))));
    const auto r = run (t, args);
    EXPECT_EQ (r.exitCode, 2) << r.err;
    EXPECT_TRUE (r.err.contains ("nests too deeply")) << r.err;
    EXPECT_FALSE (out.exists());
}

namespace
{
    // The rig's sidecar with `body` added at the top level (after based_on).
    juce::File rigSidecarWith (const juce::File& file, const juce::String& body)
    {
        const auto side = sidecarFileFor (file);
        side.replaceWithText (R"({"version":2,"based_on":{"uuid":")" + openTrack (file).data.uuid
                              + R"(","revision":1},)" + body + "}");
        return side;
    }
}

TEST (Cli, MergeKeepsASidecarWithUnknownKeys)
{
    TempDir t;
    const auto file = built (t);
    const auto side = rigSidecarWith (file, R"("track":{"rating":5},"future":{"a":1},"analysis":{"later":2})");
    const auto r = run (t, { "merge", path (file), "--take", "all" });
    EXPECT_EQ (r.exitCode, 0) << r.err;
    EXPECT_EQ (openTrack (file).data.track.rating, 5);
    EXPECT_EQ (openTrack (file).data.revision, 2);
    ASSERT_TRUE (side.existsAsFile());
    EXPECT_TRUE (r.err.contains ("merged; sidecar kept: it holds keys this a3t does not know: future, analysis.later"))
        << r.err;
}

TEST (Cli, NothingToMergeKeepsASidecarWithUnknownKeys)
{
    TempDir t;
    const auto file = built (t);
    const auto side = rigSidecarWith (file, R"("track":{"rating":0},"future":1)");
    const auto fileBefore = bytesOf (file);
    const auto sideBefore = bytesOf (side);
    const auto r = run (t, { "merge", path (file), "--take", "all" });
    EXPECT_EQ (r.exitCode, 0) << r.err;
    EXPECT_TRUE (r.err.contains ("nothing to merge; sidecar kept: it holds keys this a3t does not know: future")) << r.err;
    EXPECT_EQ (bytesOf (file), fileBefore);
    ASSERT_TRUE (side.existsAsFile());
    EXPECT_EQ (bytesOf (side), sideBefore);
}

TEST (Cli, MergeTakesTheRigsStems)
{
    TempDir t;
    const auto file = built (t);
    const auto side = rigSidecarWith (file, R"("stems":[{"label":"Drums","colour":"#ff0000"}])");
    const auto list = run (t, { "merge", path (file), "--list" });
    EXPECT_TRUE (list.out.startsWith ("stems\tchanged\t")) << list.out;
    const auto r = run (t, { "merge", path (file), "--take", "stems" });
    EXPECT_EQ (r.exitCode, 0) << r.err;
    EXPECT_EQ (openTrack (file).data.stems[0].label, "Drums");
    EXPECT_FALSE (side.exists());
}
