# A³ Tunes

Track preparation for the A³ system, the counterpart to Pioneer's rekordbox: it makes
the files StemDeck plays. It runs on the laptop; the rig only plays what it prepared.

Planned: stem separation, audio analysis (tempo, key, grid, phrases), editing of cues and
A³ Motion events, rekordbox library import, streaming-service downloads, and
ambisonic (all-round) and binaural encoding and decoding.

Its file is the **`.a3mkv`**: a Matroska file holding the original stereo track, four
stems and all metadata of a track. The format library `a3t/` is the only reader and
writer; a3-tunes and StemDeck both link it.

Status: just started, nothing built yet.
