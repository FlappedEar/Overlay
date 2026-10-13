# BetterDriver video: a clip series (KAN-260)

Status: plan and step 1 (the parser) in the first PR; later steps are listed at the end.

BetterDriver is the owner's Android recorder. Its "Export VBO + video" writes a `.vbo` and the
session's video next to it. Overlays reads the VBO's GPS since KAN-259 (see
[telemetry-semantics.md](telemetry-semantics.md)). This page is about its video.

## What BetterDriver writes

The format is BetterDriver's own spec (its Confluence page "VBO video format (Circuit Tools)",
confirmed by the owner in Circuit Tools). The parts that matter here:

- A VBO `[avi]` section: line 1 the base name of the clips, line 2 the extension (`MP4`), for example
  `BetterDriver-20261012-103607-3713ff1a_` and `MP4`.
- Clip files next to the VBO named `<base><nnnn>.MP4`: `..._0001.MP4`, `..._0002.MP4`, in start order.
- Two standard columns on every data row: `avifileindex` (1-based clip number, 4 digits) and
  `avisynctime` (whole milliseconds into that clip, 9 characters; `-00000001` where there is no video).
- The phone starts a new clip at a size cap (about 3.8 GB) and on a resolution step-down. Between two
  clips there is no video: in the owner's 2026-10-12 commute export each switch left about 0.6 to 1.5 s,
  and per-clip drift was -2 to -36 ms. `avisynctime` steps vary by about 35 ms because the session clock
  is the phone's arrival time of each GNSS message.
- If a session has phone video only the phone clips are exported; otherwise the clips of the first other
  camera. A clip cut off by a crash (no MP4 index) is left out, so a series can have a missing number.

A real file (owner, session 20261012-103607, 54,797 rows) has five clips (rows with video 9,300, 9,966,
9,855, 20,159 and 5,398 at 25 Hz), with 23 to 34 rows without video at each switch. Its first row is
30.8 s into clip 1, so clip 1's offset is -30.846 s: the offset is the sync, whatever the first row is.

## What Overlays did before

Overlays read the two columns as ordinary channels and ignored `[avi]`. The video was opened by hand
as one file, or as GoPro chapters (KAN-105, KAN-106). Chapters are contiguous by construction (each
starts where the previous stream ended), a missing chapter is a gap that blocks export, and chapters must
match the first in codec, picture size, frame rate, time base and audio. A BetterDriver series breaks
two of those: there are real gaps and the size can change mid-series.

## Design

One principle: the VBO already says where every clip sits on the telemetry timeline, so that is the
sync, not an offset the user aligns by hand.

1. **Parser (step 1, this PR).** `TelemetrySession::videoSeries` holds the `[avi]` base name and
   extension and, per clip that has rows with video: the clip number, the file name, the recording time
   of the clip's media time 0 (`offsetSeconds`: the median over its rows of row time minus
   `avisynctime`), the spread of the per-row offsets (the GNSS arrival jitter), and the first and last
   recording time with video. Row values are read as doubles beside the float channels, so a clip longer
   than a float's exact range of whole milliseconds (4.6 h) is still placed exactly. A base name or
   extension that is not a plain file name (a path separator, `..`, a control character), a clip number
   outside 1 to 9999, more than 64 clips or a missing section or column leaves no series and adds one
   warning; the telemetry still loads. File names are built from the section and the number, never read
   from a data row.
2. **Clip series model (step 2).** The main video keeps its first clip as the ordinary `video` and lists
   the clips as chapters (project format). A chapter may carry `gapBeforeSeconds`: the time without video
   before it, taken from the offsets (`offset(n) - offset(n-1) - duration(n-1)`, since the timeline of
   the video starts at clip 1's media time 0 and the run's sync offset is clip 1's offset). Gaps of a
   series are known and exportable; a gap from a missing file stays a block.
3. **Discovery (step 3).** Opening a VBO with a series looks for `<base><nnnn>.<ext>` in the VBO's
   folder, offers to link the clips it finds, and shows a number that is missing as a gap. No recursive
   search; a name never leaves the VBO's folder.
4. **Preview (step 4).** The chapter playback already shows the clip at a time; a gap with `gapBeforeSeconds`
   shows no picture for its length, and the telemetry keeps running.
5. **Export (step 5).** The concat demuxer cannot express a gap or a size change, so a series with either
   is composed in the filter graph: each clip is scaled to the output size (letterboxed, aspect kept),
   gaps are black of the output size and rate, and audio is silence over a gap. Output size is the
   first clip's. Stage B remains the one place that reads the media.
6. **Editor and guide (step 6).** The video panel says the clips came from the recording, shows gaps,
   and the user guide and Confluence FEO describe it.
7. **Owner check (step 7).** With the owner's real BetterDriver video (not in this repository).

Out of scope until the owner says otherwise: several recording sessions in one project, and a second
camera in the same session (BetterDriver exports one camera's clips; GoPro footage already uses the
additional-video path of KAN-131 and KAN-245).

## Tests

Step 1: `TelemetryCoreTests::readsBetterDriverVideoClips` (a generated two-clip recording with a gap
between and 3 ms of jitter: file names, offsets, spread, first and last time, row counts, the columns kept
as channels, a late millisecond count placed exactly) and `rejectsUnusableVideoClipLists` (missing or short
section, a path or `..` in the base name, a dotted or long extension, fractional, zero and 65 clips).
