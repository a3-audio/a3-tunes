# A³ Tunes

Track preparation for the A³ system, the counterpart to Pioneer's rekordbox: it makes
the files StemDeck plays. It runs on the laptop; the rig only plays what it prepared.

Planned: stem separation, audio analysis (tempo, key, grid, phrases), editing of cues and
A³ Motion events, rekordbox library import, streaming-service downloads, and
ambisonic (all-round) and binaural encoding and decoding.

Its file is the **`.a3mkv`**: a Matroska file holding the original stereo track, four
stems and all metadata of a track. The format library `a3t/` (this repo) is the only reader
and writer; a3-tunes and StemDeck both link it. `cli/` holds the `a3t` command-line tool.

## Build and test

Needs JUCE (core only) at `~/local/juce` (or `CMAKE_PREFIX_PATH`), the FFmpeg development
libraries (libavformat, libavcodec, libavutil, libswresample) and GoogleTest.

```sh
JOBS=2 ./test.sh          # builds the tests and the CLI, then runs the tests
build/bin/a3t ...         # the CLI
```

## The `a3t` tool

```sh
a3t build --original o.flac --stem1 s1.flac --stem2 s2.flac --stem3 s3.flac --stem4 s4.flac \
          --out song.a3mkv [--meta meta.json] [--cover c.jpg] [--convert 44100/24]
a3t info song.a3mkv                      # a3t.json (sidecar applied), status, tracks, rate, frames
a3t verify song.a3mkv                    # the writer's checks: names, FLAC stems, exact lengths, a3t.json
a3t merge song.a3mkv --list              # what the rig changed in the sidecar
a3t merge song.a3mkv --take all          # take it into the file (or none / KEY,KEY); removes the sidecar
a3t seekbench song.a3mkv --seeks 100 --compare DIR   # hot-cue seek timing; DIR has stem1..4.flac
```

Exit codes: 0 ok, 1 usage error, 2 failure. Data goes to stdout, reasons to stderr.
`seekbench` draws its random positions from a fixed seed (1), so runs are comparable.
Its "bytes read per second of audio" is the process's `rchar` (from `/proc/self/io`, so page-cache
hits count too) divided by the seconds stem 1 read; the four stems are read together, so it is bytes
per second of music. The loose files given with `--compare` must share sample rate and channel count.

## File layout

Five audio tracks (Original, then Stem 1 to 4, FLAC), the `a3t.json` attachment (metadata,
cues, loops, A³ Motion events), plus tags and chapters derived from it. What the rig edits
goes beside the file in `<name>.a3events.json` (sidecar) until `a3t merge` takes it in.

## Status

The library and the CLI are done. The StemDeck reader and the a3-tunes app are the next plans.
