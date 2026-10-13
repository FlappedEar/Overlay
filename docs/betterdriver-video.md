# BetterDriver video: a clip series (KAN-260)

Status: plan and step 1 (the parser, KAN-261) merged in #261; steps 2 and 3 (the model, KAN-262, and linking, KAN-263) merged in #262 and #263; step 4 (preview, KAN-264) in progress; later steps are listed at the end.

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

The owner's real file (private, not in this repository) has five clips at 25 Hz with 23 to 34 rows
without video at each switch. Its first row is well into clip 1 (about 30 s), so clip 1's offset is
negative: the offset is the sync, whatever the first row is.

Checked on the owner's real BetterDriver clips (13 October 2026, a second session of five clips; the files
are private and not in this repository):

- Every clip is 4K H.264 (High, `yuvj420p`, BT.709) with mono AAC audio, except the last clip, which is
  1080p: the resolution step-down is real.
- Nominal frame rates differ between clips (59.94, 60 and 30 fps) and the streams are variable frame rate
  (average rates a little under nominal). The video stream of a clip starts 0 to 0.13 s after media time 0.
- A clip's offset plus its video duration lands within 0.1 s of its last row with video, and the next
  clip's offset minus that end is 1.3 to 1.5 s: the VBO's offsets and the probed durations agree, so the
  offsets can place the clips and the durations can check them.
- Per-row spread inside a clip was 15 to 190 ms (the GNSS arrival jitter), so the median over a clip is
  the sync, not any single row.

Consequence for the chapter model: clips differ in size, frame rate and time base, which
`ChapterSource::combine` refuses today, so a series cannot ride the concat demuxer unchanged (step 5).

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
2. **Clip series model (step 2, KAN-262: `TimelineChapter::gapBeforeSeconds`, `MediaTimeline`, `VideoChaptersCodec`).** The main video keeps its first clip as the ordinary `video` and lists
   the clips as chapters (project format). A chapter may carry `gapBeforeSeconds`: the time without video
   before it, taken from the offsets (`offset(n) - offset(n-1) - duration(n-1)`, since the timeline of
   the video starts at clip 1's media time 0 and the run's sync offset is clip 1's offset). Gaps of a
   series are known and exportable; a gap from a missing file stays a block.
3. **Discovery and linking (step 3, KAN-263: `BetterDriverClips`, `AppController::betterDriverVideo` and
   `loadBetterDriverVideo`).** For a loaded recording with a series, `betterDriverVideo()` says how many
   of its clips are beside it, and `loadBetterDriverVideo()` opens them. Only the recording's own folder
   is looked in (no recursive search; the names are plain file names the parser has checked). The video
   starts at the first clip that exists; a later clip that is not there stays on the timeline as a gap
   of the length the recording gives it (its last row with video minus its offset), and the saved
   project keeps where the file should be. After probing, each clip's gap is its offset minus the first
   clip's offset, minus where the previous clip ended on the timeline (`applyRecordedGaps`: never
   below 0, never above 3,600 s), so the probed lengths place the clips and the VBO's offsets decide the
   gaps. The sync is set from the first clip's offset (telemetry time = video time + offset, scale 1),
   which is what the owner would otherwise align by hand; it stays editable. Since KAN-268 the clips are linked by themselves when the telemetry is opened (no video yet, or the open video is one of the recording's clips, in the same folder) and when one of the recording's clips is opened with Open video; any other video is left alone. The user can also link with the header button "BetterDriver video (found/total)" (KAN-266). Only an MP4 or MOV series can be opened.
4. **Preview (step 4, KAN-264: `Main.qml`).** A time inside a recorded gap shows no picture: a black
   panel with a short note covers the main video (extra videos stay), the playhead and telemetry time
   run on a clock of the preview's own while the next clip waits at its first frame, and when the clock
   reaches the clip's start the clip plays on (if it was playing). Playing off the end of a clip enters
   the gap by itself, pausing stops the clock, and a seek out of the gap leaves it. A gap before a missing
   clip runs the same way and then stops at the missing clip. In picture-in-picture layouts the main
   video's boxes stay but show no picture over the gap; extra cameras are untouched. A chapter switch now also waits
   for the new file's own events: the old file can report "loaded" and frames after the source property
   has moved on, which used to be taken for the new file's and left the preview at the first frame.
5. **Export (step 5, KAN-265: `ChapterSource::combineSeries` / `seriesGraph`, `ExportSettings::chapterGapSeconds`).** The concat demuxer cannot express a gap or a size change, so a series with recorded gaps
   is composed in Stage B's filter graph: each clip is retimed to the export rate, scaled to the first
   clip's size (letterboxed, aspect kept) and cut to its probed length; gaps are black of that size and
   rate; audio is resampled to stereo at the first sound's rate, and a clip without sound or a gap gets
   silence. The first clip is FFmpeg input 0, the others follow the overlay and additional videos; the
   joined `[seriesV]`/`[seriesA]` replace `[0:v]`/`[0:a]` in the usual trim. The combined source starts
   at zero and lasts every clip and gap, so range, timecodes and telemetry work as for one video. Output
   size defaults to the first clip's. The whole series is read from its start up to the end of the range
   (no input seek), so a range late in a long session takes longer. A missing clip file still blocks export.
6. **Editor and guide (step 6).** The video panel says the clips came from the recording, shows gaps,
   and the user guide and Confluence FEO describe it.
7. **Owner check (step 7).** With the owner's real BetterDriver video (not in this repository).

Out of scope until the owner says otherwise: several recording sessions in one project, and a second
camera in the same session (BetterDriver exports one camera's clips; GoPro footage already uses the
additional-video path of KAN-131 and KAN-245).

## Tests

Step 4: `SourceTests::showsNoPictureOverRecordedGaps` (two generated clips with a 0.6 s gap in the editor window: a seek into the gap shows the black panel with the requested time and the next clip loaded, playing runs the gap's clock into the clip and on, playing off the end of the first clip enters and leaves the gap, a paused gap keeps its time, a seek out of it leaves it, a seek into the gap and at once on into the next clip stands) and `runsRecordedGapsBeforeMissingClips` (the gap before a missing clip runs, then the time stands at the missing clip).

Step 3: `SourceTests::plansBetterDriverClipsBesideTheRecording` (own folder only, a directory is not a
file, clips before the first existing one dropped, later missing ones kept as gaps with expected path and
estimated length, no files and a non-MP4/MOV extension give a problem),
`placesRecordedGapsFromRecordingOffsets` (gap arithmetic, overlap clamps to 0, chapters without an offset
keep theirs, the 3,600 s limit) and `linksBetterDriverClipsAsOneTimeline` (generated MP4 clips for a
generated recording with one clip missing: chapter starts and gaps within 60 ms, the missing clip is
a gap, the sync offset is the first clip's, a time in a gap is flagged, the project saves, validates and
reopens with the same timeline and sync).

Step 1: `TelemetryCoreTests::readsBetterDriverVideoClips` (a generated two-clip recording with a gap
between and 3 ms of jitter: file names, offsets, spread, first and last time, row counts, the columns kept
as channels, a late millisecond count placed exactly) and `rejectsUnusableVideoClipLists` (missing or short
section, a path or `..` in the base name, a dotted or long extension, fractional, zero and 65 clips).
