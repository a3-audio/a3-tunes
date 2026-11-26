// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "a3t/Sidecar.h"

#include <gtest/gtest.h>

using namespace a3t;

namespace
{
    struct TempFolder
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("a3t-sidecar-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { dir.createDirectory(); }
        ~TempFolder() { dir.deleteRecursively(); }
    };

    Event shotAt (double beat, int stem = 0)
    {
        Event e;
        e.beat = beat;
        e.stem = stem;
        return e;
    }

    Sidecar sampleSidecar()
    {
        Sidecar s;
        s.basedOnUuid = "0a1b2c3d-0000-4000-8000-123456789abc";
        s.basedOnRevision = 7;
        s.events = std::vector<Event> { shotAt (4.0), shotAt (8.0, 2) };
        Cue c; c.kind = Cue::Kind::hot; c.slot = 3; c.time = 12.5; c.colour = "Rose"; c.name = "drop";
        s.cues = std::vector<Cue> { c };
        Loop l; l.slot = 1; l.in = 8.0; l.out = 16.0; l.active = true;
        s.loops = std::vector<Loop> { l };
        Phrase p; p.kind = Phrase::Kind::chorus; p.mood = Phrase::Mood::high; p.start = 32.0; p.end = 64.0;
        s.phrases = std::vector<Phrase> { p };
        Analysis a;
        Grid g; g.firstBeat = 0.25; g.bpm = 127.5; g.corrected = true;
        a.grid = g;
        s.analysis = a;
        s.trackFields.set ("rating", 4);
        s.trackFields.set ("playCount", 31);
        return s;
    }

    A3tData sampleData()
    {
        A3tData d;
        d.uuid = "0a1b2c3d-0000-4000-8000-123456789abc";
        d.revision = 7;
        d.track.title = "Title";
        d.track.rating = 2;
        d.track.playCount = 9;
        d.analysis.bpm = 126.0;
        Grid g; g.firstBeat = 0.4; g.bpm = 126.0;
        d.analysis.grid = g;
        Cue c; c.kind = Cue::Kind::memory; c.time = 1.0;
        d.cues = { c };
        d.events = { shotAt (1.0) };
        return d;
    }

    juce::String readAll (const juce::File& f) { return f.loadFileAsString(); }
}

TEST (Sidecar, NameRuleDropsFinalExtension)
{
    const juce::File dir ("/tmp/some");
    EXPECT_EQ (sidecarFileFor (dir.getChildFile ("My Song.a3mkv")).getFileName(), "My Song.a3events.json");
    EXPECT_EQ (sidecarFileFor (dir.getChildFile ("a.b.c.a3mkv")).getFileName(), "a.b.c.a3events.json");
    EXPECT_EQ (sidecarFileFor (dir.getChildFile ("noextension")).getFileName(), "noextension.a3events.json");
    EXPECT_EQ (sidecarFileFor (dir.getChildFile (juce::String (juce::CharPointer_UTF8 ("\xc3\x9c" "ber \xe2\x80\x93 \xc3\x84.a3mkv")))).getFileName(),
               juce::String (juce::CharPointer_UTF8 ("\xc3\x9c" "ber \xe2\x80\x93 \xc3\x84.a3events.json")));
    EXPECT_EQ (sidecarFileFor (dir.getChildFile ("x.flac")).getParentDirectory(), dir);
}

TEST (Sidecar, SaveLoadRoundTrip)
{
    TempFolder tmp;
    const auto file = tmp.dir.getChildFile ("t.a3events.json");
    const auto s = sampleSidecar();
    ASSERT_TRUE (saveSidecar (file, s));

    const auto loaded = loadSidecar (file);
    ASSERT_EQ (loaded.status, Status::ok);
    const auto& r = loaded.sidecar;
    EXPECT_EQ (r.version, sidecarVersion);
    EXPECT_EQ (r.basedOnUuid, s.basedOnUuid);
    EXPECT_EQ (r.basedOnRevision, 7);
    ASSERT_TRUE (r.events.has_value());
    ASSERT_EQ (r.events->size(), 2u);
    EXPECT_TRUE ((*r.events)[1] == (*s.events)[1]);
    ASSERT_TRUE (r.cues.has_value());
    EXPECT_EQ ((*r.cues)[0].slot, 3);
    EXPECT_EQ ((*r.cues)[0].name, "drop");
    ASSERT_TRUE (r.loops.has_value());
    EXPECT_TRUE ((*r.loops)[0].active);
    ASSERT_TRUE (r.phrases.has_value());
    EXPECT_EQ ((*r.phrases)[0].end, 64.0);
    ASSERT_TRUE (r.analysis.has_value());
    ASSERT_TRUE (r.analysis->grid.has_value());
    EXPECT_EQ (r.analysis->grid->bpm, 127.5);
    EXPECT_FALSE (r.analysis->bpm.has_value());
    EXPECT_EQ (static_cast<int> (r.trackFields["rating"]), 4);
    EXPECT_EQ (static_cast<int> (r.trackFields["playCount"]), 31);
    EXPECT_EQ (r.trackFields.size(), 2);
}

TEST (Sidecar, AbsentSectionsStayAbsent)
{
    TempFolder tmp;
    const auto file = tmp.dir.getChildFile ("t.a3events.json");
    Sidecar s;
    s.basedOnUuid = "u";
    s.events = std::vector<Event> { shotAt (1.0) };
    ASSERT_TRUE (saveSidecar (file, s));
    const auto r = loadSidecar (file).sidecar;
    EXPECT_TRUE (r.events.has_value());
    EXPECT_FALSE (r.cues.has_value());
    EXPECT_FALSE (r.loops.has_value());
    EXPECT_FALSE (r.phrases.has_value());
    EXPECT_FALSE (r.analysis.has_value());
    EXPECT_EQ (r.trackFields.size(), 0);
}

TEST (Sidecar, EmptySidecarDeletesTheFile)
{
    TempFolder tmp;
    const auto file = tmp.dir.getChildFile ("t.a3events.json");
    ASSERT_TRUE (saveSidecar (file, sampleSidecar()));
    ASSERT_TRUE (file.existsAsFile());
    Sidecar empty;
    EXPECT_TRUE (empty.empty());
    EXPECT_TRUE (saveSidecar (file, empty));
    EXPECT_FALSE (file.existsAsFile());
    EXPECT_TRUE (saveSidecar (file, empty));   // nothing to delete is fine
}

TEST (Sidecar, AtomicSaveLeavesNoTemporaryFile)
{
    TempFolder tmp;
    const auto file = tmp.dir.getChildFile ("t.a3events.json");
    ASSERT_TRUE (saveSidecar (file, sampleSidecar()));
    ASSERT_TRUE (saveSidecar (file, sampleSidecar()));
    const auto files = tmp.dir.findChildFiles (juce::File::findFiles, false);
    ASSERT_EQ (files.size(), 1);
    EXPECT_EQ (files[0], file);
}

TEST (Sidecar, NewerVersionIsNotOverwritten)
{
    TempFolder tmp;
    const auto file = tmp.dir.getChildFile ("t.a3events.json");
    ASSERT_TRUE (file.replaceWithText ("{\"version\":3}"));
    EXPECT_EQ (loadSidecar (file).status, Status::newer);
    EXPECT_FALSE (saveSidecar (file, sampleSidecar()));
    EXPECT_EQ (readAll (file), "{\"version\":3}");
    EXPECT_FALSE (saveSidecar (file, Sidecar {}));   // an empty one does not delete it either
    EXPECT_TRUE (file.existsAsFile());
}

TEST (Sidecar, DamagedFileIsNotOverwritten)
{
    TempFolder tmp;
    const auto file = tmp.dir.getChildFile ("t.a3events.json");
    ASSERT_TRUE (file.replaceWithText ("{\"version\": 2, \"events\": [ oops"));
    EXPECT_EQ (loadSidecar (file).status, Status::damaged);
    EXPECT_FALSE (saveSidecar (file, sampleSidecar()));
    EXPECT_EQ (readAll (file), "{\"version\": 2, \"events\": [ oops");
}

TEST (Sidecar, MissingFileLoadsAsNone)
{
    TempFolder tmp;
    const auto loaded = loadSidecar (tmp.dir.getChildFile ("nope.a3events.json"));
    EXPECT_EQ (loaded.status, Status::none);
    EXPECT_TRUE (loaded.sidecar.empty());
}

TEST (Sidecar, V1FileLoadsAsEventsOnly)
{
    TempFolder tmp;
    const auto file = tmp.dir.getChildFile ("t.a3events.json");
    ASSERT_TRUE (file.replaceWithText (
        R"({"version":1,"events":[{"stem":1,"beat":16.0,"shape":"orbit","bars":4,"reversed":false,)"
        R"("speed":1,"seed":"42","active":true,"origin":"hand"}]})"));
    const auto loaded = loadSidecar (file);
    ASSERT_EQ (loaded.status, Status::ok);
    EXPECT_TRUE (loaded.sidecar.basedOnUuid.isEmpty());
    ASSERT_TRUE (loaded.sidecar.events.has_value());
    ASSERT_EQ (loaded.sidecar.events->size(), 1u);
    EXPECT_EQ ((*loaded.sidecar.events)[0].seed, 42u);
    EXPECT_FALSE (loaded.sidecar.cues.has_value());
    EXPECT_TRUE (saveSidecar (file, sampleSidecar()));   // a v1 file may be replaced by v2
}

TEST (Sidecar, V1FileWithoutEventsIsDamaged)
{
    TempFolder tmp;
    const auto file = tmp.dir.getChildFile ("t.a3events.json");
    ASSERT_TRUE (file.replaceWithText ("{\"version\":1}"));
    EXPECT_EQ (loadSidecar (file).status, Status::damaged);
}

TEST (Sidecar, OverlayReplacesASectionWholeAndLeavesTheRest)
{
    Sidecar s;
    s.events = std::vector<Event> { shotAt (9.0), shotAt (10.0) };
    const auto base = sampleData();
    const auto merged = overlay (base, s);
    ASSERT_EQ (merged.events.size(), 2u);
    EXPECT_EQ (merged.events[0].beat, 9.0);
    EXPECT_EQ (merged.cues.size(), 1u);
    EXPECT_TRUE (merged.cues[0] .time == base.cues[0].time);
    EXPECT_EQ (merged.track.rating, 2);
    EXPECT_EQ (merged.uuid, base.uuid);
}

TEST (Sidecar, OverlayWithAnEmptySectionClearsIt)
{
    Sidecar s;
    s.events = std::vector<Event> {};
    EXPECT_TRUE (overlay (sampleData(), s).events.empty());
}

TEST (Sidecar, OverlayPatchesTrackFieldsByName)
{
    Sidecar s;
    s.trackFields.set ("rating", 4);
    const auto merged = overlay (sampleData(), s);
    EXPECT_EQ (merged.track.rating, 4);
    EXPECT_EQ (merged.track.playCount, 9);
    EXPECT_EQ (merged.track.title, "Title");
}

TEST (Sidecar, OverlayKeepsUnknownTrackFieldsOfTheSidecar)
{
    Sidecar s;
    s.trackFields.set ("futureField", 5);
    const auto merged = overlay (sampleData(), s);
    EXPECT_EQ (static_cast<int> (merged.track.trackExtra["futureField"]), 5);
}

TEST (Sidecar, OverlayPatchesAnalysisMembers)
{
    Sidecar s;
    Analysis a;
    Grid g; g.firstBeat = 0.5; g.bpm = 128.0; g.corrected = true;
    a.grid = g;
    s.analysis = a;
    const auto merged = overlay (sampleData(), s);
    ASSERT_TRUE (merged.analysis.bpm.has_value());
    EXPECT_EQ (*merged.analysis.bpm, 126.0);
    ASSERT_TRUE (merged.analysis.grid.has_value());
    EXPECT_EQ (merged.analysis.grid->bpm, 128.0);
    EXPECT_TRUE (merged.analysis.grid->corrected);
}

TEST (Sidecar, ForeignSidecarIsNotApplied)
{
    const auto data = sampleData();
    Sidecar mine; mine.basedOnUuid = data.uuid;
    Sidecar foreign; foreign.basedOnUuid = "ffffffff-0000-4000-8000-000000000000";
    Sidecar v1;   // no uuid: found by name, so it belongs
    EXPECT_TRUE (belongsTo (mine, data));
    EXPECT_FALSE (belongsTo (foreign, data));
    EXPECT_TRUE (belongsTo (v1, data));
}

TEST (Sidecar, FindsTheSidecarByUuidAfterARename)
{
    TempFolder tmp;
    Sidecar a = sampleSidecar();
    Sidecar b = sampleSidecar();
    b.basedOnUuid = "bbbbbbbb-0000-4000-8000-000000000000";
    ASSERT_TRUE (saveSidecar (tmp.dir.getChildFile ("zzz-unrelated-name.a3events.json"), a));
    ASSERT_TRUE (saveSidecar (tmp.dir.getChildFile ("aaa.a3events.json"), b));
    ASSERT_TRUE (tmp.dir.getChildFile ("note.json").replaceWithText ("{}"));

    const auto found = findSidecarByUuid (tmp.dir, a.basedOnUuid);
    ASSERT_TRUE (found.has_value());
    EXPECT_EQ (found->getFileName(), "zzz-unrelated-name.a3events.json");
    EXPECT_FALSE (findSidecarByUuid (tmp.dir, "00000000-0000-4000-8000-000000000000").has_value());
    EXPECT_FALSE (findSidecarByUuid (tmp.dir, "").has_value());
}

TEST (Sidecar, NestingBehindASingleQuotedStringIsDamaged)
{
    TempFolder tmp;
    const auto file = tmp.dir.getChildFile ("t.a3events.json");
    ASSERT_TRUE (file.replaceWithText (R"({"version":2,"x":['"',)" + juce::String::repeatedString ("[", 100000)
                                       + juce::String::repeatedString ("]", 100000) + "]}"));
    EXPECT_TRUE (nestsTooDeep (file.loadFileAsString()));
    EXPECT_EQ (loadSidecar (file).status, Status::damaged);
}
