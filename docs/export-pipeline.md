# Export pipeline

Export uses two FFmpeg stages so telemetry frames are generated from the same QML scene as preview without relying on a live secondary video stream.

## Stage 0: probe and capability detection

`MediaProbe` reads explicit raster, codec/profile, pixel-format/bit-depth, orientation, bitrate, and raw color metadata with `ffprobe`. `ExportMediaProfile` then derives one source-preserving output policy. This one preflight boundary rejects non-zero rotation metadata and valid non-square SAR because the current pipeline does not preserve display transforms; it never silently adds a transform. `EncoderDetector` discovers usable HEVC encoders and caches a one-frame probe of the exact requested raster, rate, pixel format, bit depth, and profile. The QRhi renderer separately validates the requested raster against its reported texture limit with checked pixel/frame-byte arithmetic. Before representative telemetry sampling or Stage A, a cancellable, bounded three-frame probe runs the production Stage B graph at the selected bit depth. Missing alpha-mode/filter support fails with an actionable error before telemetry rendering. The effective export rate is one exact `MediaRational`: an explicitly supplied export rate when valid, otherwise the source `avg_frame_rate`, falling back to `r_frame_rate`. This exact rational is retained throughout export; it is not reconstructed from a rounded decimal.

Likely variable-frame-rate input is detected from a meaningful difference between nominal and average frame rate and is surfaced as a warning.

## Export format recommendations

The export dialog resolves its displayed dimensions, exact rational rate, video bitrate, and size estimate from one shared format model. Native source dimensions are first; standard smaller widths preserve source aspect ratio and never upscale. Recommended bitrate follows the continuous pixel-rate curve documented in [media-color-policy.md](media-color-policy.md), with a depth and quality adjustment and no 4K plateau. Custom is the only editable bitrate mode and is bounded to 0.5–500 Mbps. Estimated output size uses checked arithmetic over the resolved video bitrate plus enabled 192 kbps AAC audio, including its existing 3% container allowance, and is displayed in MiB below 1024 MiB or GiB otherwise.

Before Stage A, export renders 24 evenly distributed telemetry frames (or fewer for short ranges) with the production QRhi scene and encodes them to an in-memory FFV1/BGRA Matroska sample. The measured encoded bytes/frame is projected across the exact scheduled frame count with a 1.50 safety margin. Sampling does not decode source video or create a disk artifact. If that tiny sample cannot complete, the fallback remains 512 KiB/frame with a 1.75 margin: intentionally far above the observed roughly 71 KiB/frame 4K telemetry overlay, without treating a mostly transparent scene as raw RGBA. The selected final bitrate receives a 25% margin and a 2 GiB reserve is retained. Temporary and destination filesystems are inspected independently; a future artifact is resolved against its nearest existing filesystem ancestor while diagnostics retain the intended path. If both resolve to one volume, concurrent requirements are combined. An export fails before Stage A when the requirement cannot fit. During encoding the temporary volume is sampled about once per second and the process is stopped before the reserve is exhausted.

## Stage A: render and stage telemetry

For the selected source range, `ExportEngine` calculates an explicit rational frame cadence and asks `TelemetryFrameRenderer` for each source-time frame. The renderer mounts `TelemetryScene.qml` through `QQuickRenderControl` and QRhi. Because the worker directly drives Qt Quick without entering the GUI event loop, renderer initialization dispatches pending component events, completes one scene-graph preparation frame without readback, and dispatches its completion events before export frame zero. This makes asynchronous QML Canvas textures ready deterministically; no sleep or arbitrary retry is part of the contract. The renderer then reads back each full-resolution RGBA export frame and sends it through a bounded `QProcess` pipe to FFmpeg.

The offscreen alpha contract is explicit. QRhi readback is wrapped as `QImage::Format_RGBA8888_Premultiplied`; if `QRhi::isYUpInFramebuffer()` reports bottom-to-top rows, the image is vertically flipped exactly once at this boundary. Stage A preserves those premultiplied channel values while changing raw RGBA transport to FFV1/BGRA storage. The 8-bit RGB path continues to mark and composite those values as premultiplied. Before 10-bit YUV composition, Stage B converts the staged overlay to planar RGBA, explicitly unpremultiplies it, marks it straight, and declares `overlay=...:alpha=straight:format=yuv420p10`. This is required because an ordinary premultiplied BGRA-to-YUVA conversion gives fully transparent pixels neutral chroma offsets; treating those offsets as already alpha-scaled injects severe magenta/cyan false color into the source.

FFmpeg writes the completed overlay as FFV1/BGRA in a temporary Matroska file. The shared transport writes at most 1 MiB at a time, applies an 8 MiB high-water and 4 MiB low-water policy independent of frame resolution, and treats cancellation, process exit, rejected/partial writes, and sustained lack of byte progress as explicit outcomes. A frame is counted as submitted only after all its bytes have been accepted. The same transport is used for the representative sample and Stage A.

Stage A intentionally remains 8-bit RGBA/BGRA because it carries the authored overlay, not decoded source precision. For 10-bit SDR input, Stage B unpremultiplies only that 8-bit authored overlay, composites it as straight alpha into a 10-bit filter output, converts the result to the encoder's P010 input surface, and selects HEVC Main10. The decoded output must pass pixel-level color fidelity in addition to final pixel-format/profile and known color-field validation. HLG, PQ, Log/extended, and possible-HDR (HDR side data, or BT.2020 without a known transfer) sources fail preflight because copying their tags would not make SDR-authored overlay compositing colorimetrically correct.

The normal staged-overlay validation uses a metadata-only `ffprobe` call plus
independent producer/encoder invariants. It checks:

- FFV1 codec;
- expected dimensions;
- `generatedFrames == submittedFrames == FFmpeg progress frame count == expectedFrames`;
- zero-origin stream start when reported; and
- duration and reported `r_frame_rate` / `avg_frame_rate` within the tolerance
  implied by the Matroska stream time base and exact scheduled duration.

The rawvideo command receives the authoritative exact `MediaRational` (for
example `60000/1001`). Matroska commonly stores this FFV1 stream with a 1 ms
time base, so FFmpeg may report a nearby rational such as `19001/317` for both
reported rates after timestamp quantization. That metadata is not required to
be rationally identical to the schedule; it must be explainable by the
stream-time-base duration bound. A meaningful cadence error such as `30/1`
remains outside that bound and fails validation.

Normal export deliberately does not pass `-count_frames` for the complete
temporary FFV1 file. Full decoded frame counts remain deterministic/deep test
evidence, while the production path uses the already-known producer count,
FFmpeg's final progress count, and a quick metadata probe. Only if FFmpeg's
final progress record is incomplete does it fall back to `-count_packets`; the
generated FFV1 Matroska integration test verifies the one-packet-per-frame
property used by that exceptional path.

The staging step exists because FFmpeg framesync can select the latest secondary frame at or before a primary timestamp. With a live secondary pipe, the primary decoder can advance while telemetry repeats a stale frame. Stage B starts only after a completed, validated overlay exists on disk.

## Stage B: compose the final MP4

Stage B uses a bounded five-second input preroll: `-copyts -seek_timestamp 1 -ss max(0, requestedStart - 5)` retains original source timestamps. `trim` and `atrim` use the absolute requested start/end in that same source PTS domain. Timestamp strings are emitted from exact source-frame/rational arithmetic and never determine frame count. The completed overlay remains at export-relative zero and is not sought.

Source video is trimmed on its original PTS timeline, including positive stream start timestamps. Stage B then normalizes video timestamps to zero and applies FFmpeg's `fps` filter at the effective exact rational rate before framesync. The pre-`fps` stream intentionally retains end-boundary lookahead: at fractional rates an exact range endpoint can fall between source frames, and removing the following frame prevents `fps=round=near` from selecting the last scheduled pre-end frame. The authoritative `end_frame=expectedFrames` trim after cadence conversion enforces the `[start, end)` schedule without timeline drift. The CFR source and completed CFR overlay therefore enter framesync on the same deterministic cadence.

The output also uses the per-video-stream `-fps_mode:v cfr` control. The `fps` filter establishes the cadence before framesync; `-fps_mode:v cfr` makes the final output policy explicit without relying on deprecated global `-vsync`. Stage B then overlays telemetry, encodes HEVC, optionally trims/re-encodes source audio to AAC, and writes an MP4 staging target.

Telemetry rendering still uses absolute source time: exporting source seconds 120–140 renders its first overlay at source time 120. Output video starts at zero; audio retains its actual delay relative to the selected video range.

Progress and Very Verbose diagnostics report stage activity, Stage B source range,
input-seek and absolute-trim values, time to the first Stage B output frame, FFmpeg progress,
temporary-overlay size, frame-count source, reported rates/time base, metadata
validation elapsed time, validation checks, and bounded diagnostic output.
The Very Verbose view keeps the latest 1,500 entries (`BoundedDiagnosticLog`) and is
notified at most every 200 ms (`ExportController::diagnosticLogChanged`). While detached from the
tail it keeps the same history in view as the head trims, using the log's
`droppedCharacters` count (KAN-141 review, 6 October 2026).
When an export is prepared, the controller also creates one flushed text log in
`QStandardPaths::AppLocalDataLocation/exports/`, named
`export-YYYYMMDD-hhmmss-<export-uuid>.log`. Its header records the source,
requested resolved dimensions/rational rate/video bitrate/audio policy, and range;
the controller writes the same formatted worker diagnostics plus lifecycle events
and a final success, cancellation, or failure footer. Logs are support artifacts,
so inability to create one does not prevent an export. Retention removes only
older files matching that export-log naming convention in this dedicated directory,
keeping approximately the ten newest logs.
One cancellation file is consulted from the start of preparation: VBO/RCZ file parsing, track construction, input/temporary/final `ffprobe` calls, encoder discovery/capability checks, and both FFmpeg stages. Cancellation follows cooperative request, a short graceful wait, process-tree termination, then force kill. If the GUI cannot create the cancellation file, it logs that failure and immediately calls `ExportProcessSupervisor::stopAndWait`; a terminal cancellation failure is published only after the supervised worker has stopped, while a still-running worker remains in `cancelling` until its finished callback owns cleanup. On macOS/Unix the GUI worker starts in a dedicated process group; FFmpeg and ffprobe inherit it, so forced worker shutdown reaches the complete export tree. The current Unix behavior is runtime-tested. Windows assigns the top-level worker to a `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` job and reports the exact failed Job Object API if that cannot be established; export then fails before worker release. The worker waits for an explicit parent readiness file before starting FFmpeg/ffprobe work, closing the assignment-before-descendant race in the current design. Windows runtime/export has been validated on one Windows 11 / Qt 6.11 / MSVC 2022 / Intel Iris Plus / Quick Sync configuration; heavy 4K GUI responsiveness, a wider GPU/encoder matrix, packaging/signing, and multi-instance export-log safety remain open.
Final validation checks for a nonempty result, HEVC codec, dimensions, exact
nominal and average rate, authoritative video packet count, zero video start PTS,
and exact rational consistency of packet count, `duration_ts`, stream time base,
and export rate. When FFmpeg's Stage-B progress frame count is available, it must
equal the final ffprobe count. A real packet deficit or
surplus fails with direction-specific diagnostics.

KAN-148 adds three rules:

- **Validation timeout.** Final validation reads every packet, so its timeout grows with the output: 30 s plus 1 s per 15 MB, at most 2 hours. A 13 GB export to a 100 MB/s disk is no longer failed after the whole encode.
- **Start PTS.** `start_pts` is read as a signed value. The output's start must be reported and be zero; an absent start fails validation instead of being taken as zero.
- **Negative-start sources.** A source whose video starts before zero is refused with a clear message, because frame-correct trimming assumes a non-negative start.
After validation, an existing destination is replaced only if its prepare-time native regular-file
identity, size, and modification state still match; otherwise encoding is reported as finished but the
changed destination is preserved and commit fails.

Audio is included only when its source interval overlaps the selected video interval. `atrim` uses absolute source timestamps and `asetpts` subtracts the selected video origin, preserving delayed audio rather than resetting each track independently. Validation compares start against that expected delay and duration against the audio/video-range intersection. The tolerance is one AAC access unit (1024 samples at the reported sample rate, or the audio time base when larger), with floating-point epsilon. Ranges before or after the audio track export without audio.

## Chaptered sources (KAN-106)

A GoPro recording split into chapter files exports as one source on the chapter timeline of
KAN-105: each chapter starts where the previous chapter's video stream ended.

- **One source.** `ChapterSource::combine` joins the chapters' probes into one `MediaInfo` that
  starts at zero, with the summed `duration_ts` and frame count, so the frame domain, timecodes,
  range and telemetry mapping cover the whole timeline. Chapters must match the first in codec
  and profile, size, frame rate and time base, pixel format and bit depth, colour description,
  orientation and pixel shape, and audio track (present or not, codec, sample rate); each needs an
  exact `duration_ts` and a known video start. Otherwise export is refused and names the chapter
  and what differs. A missing or changed chapter (a gap) is refused too.
- **Reading.** Stage B reads the chapters through FFmpeg's concat demuxer, given as a
  `data:` URL, so no list file is written and nothing new needs cleanup. Each entry is an
  absolute `file:` URL cut to its video stream with `inpoint`, `outpoint` and `duration` in exact
  ticks. FFmpeg's seek inside a concat input skips the first chapter's first keyframe when its
  decode timestamp is negative (every B-frame stream), so frames would be lost. The input is
  therefore never sought: the script starts at the chapter holding the preroll seek point, with an
  `inpoint` there, and `-copyts -itsoffset <seek>` puts its timestamps back on the chapter
  timeline. `trim`, `atrim` and everything after them are unchanged.
- **Audio at joins.** Each chapter's audio keeps its own timestamps on the chapter timeline. For
  chaptered sources only, `aresample=async=1:min_hard_comp=0.001` runs before `atrim`: a chapter
  whose audio ends before its video leaves silence instead of pulling every later chapter's audio
  early, and overlaps at a join are dropped. Chapters must all have audio or all have none.
- **Checks.** The editor passes every chapter's path and `duration_ts` to the worker. The worker
  and the engine probe each chapter again and refuse one whose `duration_ts` changed after loading.
  Every chapter is a protected source ([export-output-safety.md](export-output-safety.md)).

Tests: `MediaTimelineTests` (combining, refusals, exact scripts and seek arguments);
`ExportTests::exportsChapteredSourceThroughWorker` (three 29.97 fps chapters, a range across both
joins: 80 frames, each from the right chapter, the third chapter's tone at its first frame even
though the second chapter's audio is 0.5 s short, a changed chapter refused, chapter targets
refused); `SourceTests::keepsVideoChaptersAsOneTimeline` (the editor exports all three chapters,
210 frames, and refuses a gap). Real GoPro chapters are still to be checked with the owner's
footage (KAN-81).

## Additional videos (KAN-131)

A run's additional videos (see [project-format.md](project-format.md)) are composed in Stage B.

- **Checks.** The editor exports only when every additional video is ready: one still loading,
  missing, changed since it was saved, or unreadable refuses the export and names it. The worker
  probes each again and refuses one without a video stream, or with HDR or Log colour.
  Every additional video is a protected source ([export-output-safety.md](export-output-safety.md)).
- **Placement.** `VideoComposition::layout` gives every video its rectangle, the same for preview
  and export. Picture in picture: the main video fills the frame as before; each additional video
  keeps its aspect ratio inside a box of 28 % of the output's width and height, stacked down the
  right edge from the top with 3 % margins. Side by side: the main video keeps its aspect ratio
  in the left half (scaled, then padded black); the additional videos share the right half, one
  above the other. Every position and size is even. A video keeps its place even when it has no
  frame in the export, so the others do not move.
- **Timing.** Both syncs map video time to the same telemetry time, so an additional video's
  time is linear in the main video's: `rate = mainScale / videoScale`,
  `shift = (mainOffset - videoOffset) / videoScale`. `VideoComposition::timing` turns that into
  an input seek one second before the first frame needed (`-seek_timestamp 1 -ss`, on the file's
  own timestamps, which the global `-copyts` keeps) and a `setpts=(T*factor+shift)/TB` that puts
  each frame on the output timeline. The video is scaled into its rectangle and overlaid with
  `eof_action=pass:repeatlast=0`: before its first frame and after its last the main video shows
  through, never a frozen frame. Overlay takes, for each output frame, the last additional frame
  whose time rounded to the output rate is not after it. A video with no frame inside the export
  is left out and logged.
- **Order and rate.** Additional videos are overlaid on the main video before the telemetry
  overlay, so widgets stay on top. The main video's `fps` filter sets the output cadence, so the
  output is CFR at one rate whatever the additional videos' rates. Audio is the main video's only.
- **Preflight.** The composition preflight runs the same graph shape with a stand-in input per
  additional video.

Tests: `ExportTests::placesAdditionalVideosByLayout`, `timesAdditionalVideosAgainstTheMainVideo`,
`composesAnAdditionalVideoOnItsOwnTimeline` (a 25 fps video with a 0.5 s start time beside a 30 fps
main video: each output frame shows the expected numbered frame, then the main video once it
ends); `SourceTests::exportsWithAnAdditionalVideo` (the editor through the worker: side by side
pixels, the additional video refused as the output, export refused while it is loading or missing).

### Picture-in-picture options and camera switching (KAN-245)

In picture-in-picture mode the project's `videoLayout.pip` and `videoLayout.program`
([design](pip-camera-switching.md)) decide what Stage B draws. `VideoComposition::plan` turns them
into layers, the same list the preview draws: full-frame layers for the camera on air (letterboxed
in black) and boxes for the cameras off air (with the border inside the box). Each layer has windows
on the main video's timeline.

- **Graph.** Each camera is retimed once (`setpts`), then `split` into one copy per layer. A layer is
  scaled to its content rectangle, padded for border or letterbox, and overlaid on the previous
  result with `enable='gte(t,a)*lt(t,b)+...'` (export time = main video time minus the export's
  start; windows are half-open, so a cut is exact to the frame). The main video is the base under
  everything, so a camera with no footage at its time leaves the main video. A window that covers
  the whole export has no `enable`.
- **Crossfade.** A switch gets its own layer: `format=yuva420p`, then `fade=t=in:st=...:d=...:alpha=1`
  at the window's start; the previous layer stays enabled for the fade. 10-bit output uses
  `yuva420p10le`.
- **Inputs.** Only cameras with a layer and footage in the export become FFmpeg inputs.
- **Worker configuration.** The job carries the whole `videoLayout` object and each additional
  video's `id`; the worker validates it with `AdditionalVideosCodec::validLayout`.
- Side by side keeps the fixed layout above and ignores `pip` and `program`.

Tests: `ExportTests::plansPictureInPictureAndCameraSwitching` (the layers), and
`switchesCamerasAndDrawsPictureInPicture` (red main, green and blue cameras: hard cut exact at the
frame, boxes swap, picture-in-picture off, crossfade mid-point is a mix, corner and border).

## CFR and VFR status

Every final export is CFR at the effective exact rational export rate. Authoritative scheduling uses the inclusive integer range `[firstFrame, lastFrame]`, so `expectedFrames = lastFrame - firstFrame + 1`. Full video comes from `nb_frames` when available, with exact `duration_ts/time_base` as fallback; container/decimal duration never chooses the count. When `nb_frames` is more than the stream's `duration_ts` can hold at the nominal rate, as in an MP4 trimmed without re-encoding whose edit list hides the frames before the cut, `MediaProbe::probe` counts the video packets that FFmpeg does not flag as discarded and uses that as the frame count ([KAN-175](https://kozucharkadiusz.atlassian.net/browse/KAN-175)). Other sources pay nothing extra. Custom IN and OUT are C++-parsed `HH:MM:SS:FF` values and both are included; QML carries strings only. Presentation seconds are derived only after this decision for rendering and FFmpeg diagnostics.

The normal validation path records FFmpeg's final progress frame count and uses the independent final packet count as authoritative. Exact count is success. If every codec/raster/rate/bit-depth/profile/color/audio/staging/transaction check passes, Stage B progress agrees when available, and the exact contiguous-CFR timestamp relation holds, only 1 through 10 terminal missing frames are committed as **SuccessWithWarning**; a larger deficit, surplus, disagreement, or any other validation error is failure. Full decoded frame counts remain part of deterministic integration tests rather than every production export, avoiding an unnecessary full decode of long media.

For target-file transaction guarantees, see [export-output-safety.md](export-output-safety.md).
# External process boundaries

Every ffprobe/FFmpeg channel is treated as untrusted external input. FFprobe JSON is drained while the process runs and must fit within 4 MiB; the reader wakes on every read from the pipe rather than on a timer, so a flooding probe is stopped within about one pipe buffer of the limit (KAN-201); overflow terminates the probe and reports the operation, limit, and observed bytes. FFmpeg stderr is retained as a 128 KiB newest-first diagnostic tail with truncation semantics.

Text fields in a worker message are cut to their last 32 KiB, marked "[… earlier output omitted]". That keeps each message well below the GUI's 128 KiB limit, and the end of an FFmpeg tail, which explains the error, still reaches the export log. The GUI applies the limit to each message line rather than to everything one read delivered (KAN-148). Machine-readable progress drops a pathological pending line above 16 KiB instead of growing indefinitely. The representative FFV1 storage sample counts and discards encoded stdout bytes, so sample output is not retained in RAM.

## Worker lifecycle and stale artifacts (KAN-156)

- **Parent exit.** An export worker stops when the application that started it is gone, for example after a crash. It notices that it has been reparented, cancels cooperatively and removes its temporary files. It does not encode on into a hidden staging file.
- **Stale artifacts.** Stale export artifacts, meaning the temporary overlay and the staging file named in an export manifest whose worker has exited, are cleaned only by the application at startup, while it holds its single-instance session lock. Command-line modes never clean them, because another export could own them before its worker PID is recorded.
- **No command-line export.** There is no command-line export mode; the former `--export-test` hard-coded overwrite consent.
- **Manifest location (KAN-164).** Manifests are written to `export-manifests/` in the application's local data folder, not the system temporary folder, which macOS can purge before the next start. The startup janitor also reads manifests that older versions left in the temporary folder. The application passes the manifest path to the worker, and both sides accept it by its file name and contents rather than by recomputing the folder, because the two processes may resolve the data folder differently (for example under `QStandardPaths` test mode).
- **Worker paths follow the manifest (KAN-198).** The export worker refuses a configuration without a manifest path before it starts the export, and the engine refuses one whose output or temporary-overlay path differs from the paths recorded in that manifest, before probing or writing anything. The worker can therefore only write the files the startup janitor knows how to clean.
