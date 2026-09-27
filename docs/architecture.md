# Architecture

Flapped Ear Telemetry is one native Qt 6 application for overlay editing/generation and telemetry analysis. C++ owns telemetry, media, project, synchronization, and export behavior; QML presents the editor and the reusable telemetry scene.

The [product vision](product-vision.md) defines the full intended analytical workflow;
the [delivery plan](product-delivery.md) distinguishes implementation from remaining
work and acceptance. This document describes the current module boundaries.

```mermaid
flowchart TD
  A["Video + VBO/RCZ + project"] --> B[AppController]
  B --> C["MediaProbe + TelemetrySource"]
  C --> D["TelemetrySession + TrackGeometry + LapTiming"]
  D --> E["TelemetryRenderContext + WidgetModel"]
  B --> E
  E --> F[TelemetryScene.qml]
  F --> G[Preview]
  F --> H["QRhi → FFV1 overlay → HEVC/AAC export"]
```

## Libraries (KAN-123)

The code builds as two static libraries. This is the first step of the
product split (`docs/product-split-plan.md`, epic KAN-121):

- **`flappedear_telemetry_core`** holds `src/telemetry` and `src/project`:
  parsers, lap timing, compatibility, segments, sector timing, theoretical
  best, time loss, consistency, variability, G-G pairs, channel summaries
  and cooling, the computed day report (`DayReport`, KAN-71), the event
  document and recovery. It links **Qt Core and zlib only**, so it can be built for
  iOS and Android. The `flappedear_telemetry_core_boundary` test fails if a
  file there includes overlay, video, export, app or Gui headers. Video
  fingerprints are built on the overlay side (`export/VideoFingerprint`)
  from the document layer's video-free helpers. An event (v3) document may
  omit the overlay `scene` (an analysis-only day); a scene that is present
  must still be valid.
- **`flappedear_overlay_core`** holds export (FFmpeg, the QRhi frame
  renderer), GoPro, synchronization, widgets and the editor's app support.
  It links the telemetry core plus Qt Gui, Qml, Quick and GuiPrivate.
- `flappedear_core` is an interface target over both, used by the current
  combined application and its app-level tests. Pure test targets link only
  `flappedear_telemetry_core`.

## Application

`AppController` is the QML-facing application boundary. It exposes source state, playback time, synchronization, live values, project dirty state, loading state, export state, analysis state, and the `WidgetModel`. It also owns the preview `TelemetryRenderContext`, so QML reads telemetry through one time transform rather than parsing files itself.

**Split boundary (KAN-124, in progress).** The analysis code (day import,
outing laps, comparison, segment review, Corner Analyzer, theoretical best,
losses, report) reaches video only through `VideoLink` (`src/app/VideoLink.h`):
- "video position for this run's telemetry time";
- "telemetry time for this video position".

Both are empty when the run has no loaded video or the time falls outside
it. `AppController` implements it for the combined app. The Telemetry app
will have no link, and then the KAN-39 lap video is simply unavailable.
Analysis and import guards use `documentBusy()` (today: an export is
running) instead of the overlay's `exporting()`. `loadOutingLapDetail`
(`telemetry/OutingLapLoader`) moved into telemetry core. It loads and
verifies one recorded section for the lap view, the comparison, the
theoretical best and the channel summaries, so both apps share it. The
channel-summary worker (`summarizeOutingChannels`, `channelSummaryMap` in
`telemetry/OutingChannelSummaries`) is also in core. It computes
temperatures, trends, cooling and heart rate per run and section. So is
the theoretical-best worker (`calculateOutingTheoreticalBest` in
`telemetry/OutingTheoreticalBest`). It gives every eligible lap's sector
times on the canonical axis, the theoretical and actual best, and the
per-corner observations. Lap derivation also moved to core
(`deriveOutingLaps`, `outingSourceDependencyKey` in
`telemetry/OutingLapDerivation`). It resolves, bounds and verifies every
run's recording, reuses unchanged runs, and infers and groups tracks. Core
now runs the day analysis without application code:
`OutingPipelineTests` links only `flappedear_telemetry_core`. The day
report is assembled in core too (`buildOutingDayReport` in
`telemetry/OutingDayReport`), from the computed results, the decisions
keys and the focus inputs. `AppController` only gathers them. The
theoretical-best family's published forms are in core as well
(`telemetry/OutingTheoreticalBestResults`):
- `publishTheoreticalBest`;
- `publishTimeLossRanking`;
- `publishSectorProgression`;
- `rankOutingTimeLosses`.

They work from the single committed `OutingTheoreticalBest` result the
controller keeps.

`AnalysisController` (`src/app/AnalysisController.h`) owns the day's
analysis state:
- outing laps and lap detail;
- segment review and the Corner Analyzer;
- the comparison;
- theoretical best, losses and section progression;
- channel summaries and the day report.

It sees the project only through `AnalysisDocument`
(`src/app/AnalysisDocument.h`). That interface lets it:
- read the project;
- commit a validated edit;
- check whether an edit is allowed now (loading, busy, recovery, batch
  import, pending destructive action);
- read the source generation and the active run.

`DocumentController` (`src/app/DocumentController.h`) owns the project
document:
- the saved project, its identity and revisions, and dirty state;
- recovery;
- new, open, save and quit decisions;
- run selection and day import.

It implements `AnalysisDocument`. Whatever the application adds comes
through `DocumentHost` (`src/app/DocumentHost.h`):
- source generations, and the editor state stored in the project (the
  active run's sources, synchronisation, overlay scene and chart
  channels);
- the verified analysis a saved project carries;
- whether the document is busy (an export).

A Telemetry app hosts a document without an editor.

`AppController` implements `DocumentHost` and `VideoLink` and forwards its
QML API unchanged. It still applies lap exclusions to the editor's own lap
navigation when analysis re-applies them. The Overlay controller is
extracted in a later step.

Editor chrome keeps one vertical scroll surface for the complete left sidebar and independent explicit scroll extents for each inspector tab, so no controls are unreachable at the 1180×720 minimum window size. Playback transport is centralized on the primary `MediaPlayer`; the Analysis window forwards the same keyboard seeks and play/pause action to it, and full-screen presentation uses that player and timeline rather than a second transport state. Text, numeric, and focused interactive controls suppress playback shortcuts. Qt decoder failures stop the affected player, enter the application log/status boundary, and remain visible over both the editor preview and Analysis video pane.

## Project

`ProjectDocumentState` tracks the current project path, revision, saved revision, and a pending destructive action. New, open, and quit therefore require a decision when the document is dirty.

`ProjectWriter` saves serialized `.fetproject` data through `QSaveFile` with direct-write fallback disabled. Before reaching it, `AppController` applies the same structural limits used by load and checks the final serialized 4 MiB bound. A project is marked saved only after validation and the atomic write commit.

The saved `.fetproject` is the authoritative clean document. `AppController` retains its complete JSON object as the serialization base and overlays known edits onto nested project objects, so unknown or future fields survive open/edit/save cycles. Current saves persist a document identity and saved revision in `documentState`; Save/Save As updates that base and revision before best-effort recovery cleanup. A failed project write leaves both dirty state and recovery intact.

Persistent unsaved edits are serialized as one complete project object plus recovery v2, document identity, original project path, timestamp, and revision metadata. `ProjectRecoveryStore` atomically replaces `project-recovery.json` under `AppLocalDataLocation` through `QSaveFile`; a restartable 250 ms single-shot debounce coalesces persistent edit bursts. Playback position, Analysis-window visibility, and other transient UI state do not trigger it. Recovery-write failure is logged, exposes `recoveryDegraded` plus an error to QML, presents a non-modal manual-save warning, and retries after a five-second backoff or the next edit. A later success clears the degraded state; it never modifies the saved project. Failure to delete an already superseded snapshot is only logged cleanup debt: it does not set `recoveryDegraded` or show a warning.

QSettings is application preference storage, not document storage. It retains window and analysis-window geometry and the last project path. Legacy QSettings document keys under `editor/widgets`, `sync/*`, `sources/*`, and `analysis/*` are deleted and are never reconstructed as a clean document. Analysis channel configuration remains project content; floating-window visibility does not.

On a clean startup, v2 recovery is compared logically with the remembered or original saved authority. A matching authority at the same or newer revision makes the snapshot stale; startup suppresses the dialog and retries cleanup. A newer matching snapshot is valid and waits for an explicit Recover/Discard choice. Identity-mismatched or malformed v2 recovery is invalid and never applied. Legacy v1 recovery remains conservatively recoverable. The remembered `.fetproject` otherwise opens through the ordinary transactional loader; if it is missing, the path is forgotten and a default new document is used. For a user discard, `ProjectRecoveryStore` first atomically writes a sibling `.discard` tombstone keyed by document identity and discarded-through revision; then Quit, Open, New, or startup Discard may continue even if snapshot deletion fails. Startup suppresses only a matching v2 snapshot at or below that bound, retries cleanup, and removes the tombstone after cleanup succeeds. A failed tombstone write cancels discard only when snapshot deletion also fails; these cleanup paths never change `recoveryDegraded`.

Project open first performs bounded file reading, JSON parsing, and structural validation (version, widget scene, synchronization, analysis settings, and source-reference structure) on the project-load worker. It then commits that complete document atomically before resolving external assets. The validated-result continuation starts video and telemetry only when applying the widget scene succeeds; failure leaves the prior document and its sources untouched. A missing, mismatched, or invalid asset after a successful document commit changes only its source state and never rolls back the document, widgets, synchronization, or settings.

`ProjectSourceReferenceCodec` owns the v2 source-reference migration, project-relative resolution, absolute fallback, serialization, and lightweight fingerprints. The current schema and compatibility rules are in [project-format.md](project-format.md).

Event projects use the development v3 schema and `EventProjectCodec`: one event
contains stable run/source identities, one primary telemetry source per run,
optional alternatives, and run-local video references and synchronization.
The selected run is projected into the existing editor interface; saves fold its
state back into the authoritative event and preserve inactive runs. Ordinary v2
single-recording projects remain supported. See [event project format](event-project-format.md)
for save/recovery/relink behavior and limits. Lap rows carry their owning run;
derived lap numbers are not permanent annotation identities across source or gate changes.

## Source loading

Video metadata probing uses `MediaProbe`/`ffprobe`; `TelemetrySource::load` dispatches VBO/RCZ and parses telemetry and builds `TrackGeometry`. Standalone loads, resolved project sources, and relink candidates run asynchronously. Their independent states are `idle`, `loading`, `ready`, `missing`, `mismatch`, or `error`. A relink candidate is probed/parsed before commit; a fingerprint mismatch is held outside committed state until the user explicitly accepts replacement.

Each source operation begins a new source generation and uses normalized source identities: cleaned absolute paths, or canonical paths when available. Results carry their generation and are ignored if a newer operation has started or identities no longer match. All long-running source work receives the same lightweight `CancellationCheck` callback and throws `OperationCancelled` on cancellation. VBO/RCZ reads/parsing, media probes, GoPro packet indexing/reads/decoding, and both coarse and fine synchronization check it in bounded batches. Starting a replacement generation signals every previous source and sync token; destruction does the same before a bounded two-second convergence wait. Generation checks prevent stale commits while cancellation stops wasted work, so neither replaces the other. At startup, sources are restored only as part of loading the authoritative saved project or an explicitly accepted recovery snapshot.

## Telemetry

`TelemetryImportPlan` supplies the bounded multi-file preparation API used by
`AppControllerImport` and the QML import workflows. It retains independent
immutable recordings and source-derived laps, reports exact duplicates and
per-file errors, and exposes cross-format matching evidence. Advanced review
supports explicit grouping; outing import chooses VBO for unique RCZ/VBO pairs
supported by recording-date/time and GPS evidence. Ambiguous candidates remain
separate. A source group retains alternatives, not fused channels. Final source
verification, cancellation and document/source context checks precede committing
an event. The [import workflow](batch-import.md) and [event delivery plan](event-analysis-plan.md)
describe the remaining boundaries.

`AnalysisControllerOuting` loads primary sources under fingerprint, cancellation and
generation guards to build the event's chronological OUT/LAP/IN rows. Unknown
recording times remain explicit and sort after dated rows in import order.
Opening a row loads an independent, bounded detail session for its telemetry
range; `OutingLapDetailPanel` presents channels and a map with a shared cursor.
This inspection does not replace the editor's active run or its video transform.
Event statistics, independent A/B distance alignment, sectors and time-loss
analysis are not implemented by this list/detail boundary.

`VboParser` performs bounded chunked file reads, reads VBO sections, resolves standard channel aliases, normalizes supported coordinate formats, parses bounded RaceChrono timing-gate metadata, and produces a `TelemetrySession`. It rejects files above 128 MiB, more than 1,000,000 lines or 500,000 data rows, more than 512 columns, lines above 1 MiB, and fields above 64 KiB. [KAN-14](https://kozucharkadiusz.atlassian.net/browse/KAN-14) replaces bulk splitting with cancellable scanning: line views are checked before retention, multiline column headers are scanned without joining, and field buffers never retain more than 64 KiB of characters. Data rows retain only declared columns while counting and validating ignored extra values. Comma mode preserves empty fields and trimmed padding; whitespace mode retains the prior ASCII separator semantics. File growth is checked before appending each read chunk. [KAN-15](https://kozucharkadiusz.atlassian.net/browse/KAN-15) checks absolute times, rollover additions, origin subtraction and duration before publication. Values must remain finite and strictly inside the signed 64-bit microsecond conversion range required by project fingerprints; an unsafe numeric range or collapsed elapsed timestamps reject the parse. Ordinary malformed text, duplicate timestamps and backward rows within that range retain their warning/skip behavior. UTC chronology checks date validity and integer addition. Chart sampling rejects overflowing query spans and bounds bucket positions before integer conversion. The existing limits leave headroom over the validated 32,718-row, 49-channel fixture, with synchronization transform/search bounds addressed by [KAN-17](https://kozucharkadiusz.atlassian.net/browse/KAN-17). [KAN-16](https://kozucharkadiusz.atlassian.net/browse/KAN-16) resolves coordinate units once from the verified RaceChrono Pro 10.2.4 marker or an explicit FlappedEar header declaration. Samples and gates share that conversion; absent/conflicting evidence withholds GPS and gates with a warning while preserving other telemetry. Magnitude does not select units. `TelemetrySession` performs time-based channel lookup and interpolation, with one lazily cached median positive interval per immutable channel for O(1) cadence-gap thresholds after first use. `TrackGeometry` derives an offline normalized track outline from valid latitude/longitude samples and cooperatively checks cancellation in bounded batches.

`LapTiming` consumes the immutable raw session and one unambiguous source Start gate on the existing VBO worker. It derives same-direction gate passages, complete laps, fastest-lap state, and per-lap metric traces under the same cancellation generation. `VboLoadResult` commits the session, track geometry, and `LapSession` atomically. `AppController` exposes bounded lap summaries and owns telemetry-to-video conversion for lap-start seeking; QML never performs synchronization arithmetic. Complete laps remain visible without video coverage. Automated and private-fixture evidence is recorded in [current state](../currentstate.md); it does not replace exact-candidate macOS acceptance.

`TelemetryRenderContext` combines a session, optional track geometry, optional lap session, and the central `SyncTransform`. Its telemetry time is `videoTime * timeScale + offset`; preview and export both use this context. The context converts normalized track points to its cached QML representation exactly when `setTrackGeometry()` is called and publishes a dedicated geometry revision; time updates do not rebuild that cache. It is also the presentation boundary: bounded holding and small channel-specific smoothing windows apply there only. Live lap delta maps current raw GPS position to a bounded time-local region of the best completed lap trace, while current/reference speed share the ordinary presentation policy. `AppController` obtains analysis segments directly from raw `TelemetrySession` samples, retaining the nested segment→point shape across the QML boundary so charts preserve gaps and extrema and never inherit overlay filtering. An individual non-overlapping row explicitly reports no data rather than looking like a paint failure. The full behavioral contract is in [telemetry-semantics.md](telemetry-semantics.md).

## GoPro and synchronization

`GoProTelemetrySource` discovers the MP4 `gpmd` data stream with the authoritative `FfmpegTools::ffprobePath()`, indexes packets, and decodes supported GPS5/GPS9 records into a telemetry session. Its process runner polls without busy-waiting, enforces a 120-second timeout and 64 MiB stdout bound, and terminates/reaps ffprobe on cancellation or limit failure. Packet count is capped at 100,000, aggregate GPMF bytes at 512 MiB, parsed KLV headers at 1,000,000, and container depth at 32. The header counter covers all sensor and structural/container records across the track; limit diagnostics report actual count, configured limit, packet, and parse context. These independent limits remain overflow-safe and cancellable. Packet position/size is checked with subtraction-based file extents before allocation. Final GPS samples are stable-sorted by timestamp and later equal timestamps are discarded, preserving the first sample for each time; channels are then verified finite and strictly increasing before publication.

`TelemetrySyncEngine` compares GoPro GPS speed with VBO speed and returns an offset/time-scale candidate with diagnostics and confidence. Cancellation is checked between offsets and in sampling/correlation batches, without changing the existing numerical algorithm. Async sync results are also generation and path checked before they can affect the controller.


Replacing one source restarts the other pending load with its complete request, including fingerprint, relink intent and dirty-state policy. Source generation and identity still guard completion.

Manual offset/scale changes advance a synchronization revision, cancel the pending match and clear its candidate. A queued result must match that revision before it can apply; changing a value and restoring it also invalidates the older result.

## Widgets and QML

The floating Analysis window is transient UI state. It starts closed for application startup, New, and Open. A QML `Loader` creates `AnalysisWindow` and its secondary `MediaPlayer` only after the user opens Analysis; closing it deactivates the loader and releases the secondary decoder. Channel selection remains persistent project configuration.

`WidgetModel` owns persistent widgets, groups, appearance cues, and templates. It is also the sole semantic normalizer for direct edits, imported scenes, template application, imported templates, and template-store reload: supported numeric settings and geometry remain finite and bounded, invalid known colors fall back to defaults, invalid range pairs are repaired, cues are normalized, and persisted widget IDs must be valid and unique. Width/height/scale limits and position re-clamping keep the unrotated rectangle inside the scene during creation, import, duplication, scaling, and resizing. `TelemetryScene.qml` is the render-only telemetry layer: it has a render context and widget model but no editor-selection or media-player dependency. Its shared frame owns normalized geometry, appearance cues, background, border, title, and formatting helpers; one `Loader` then instantiates only the renderer matching each widget type from `qml/widgets/`. Editor interaction remains in the surrounding QML components, including a rotation-matched selection surface, while preview and export use the same scene definition.

`ComparisonTileWidget.qml` renders six independent Best/Current/Delta widget types for lap time and speed. Each tile has ordinary persistent widget geometry rather than being a composite group. Its content scale includes both canonical scene scale and per-widget scale, so typography, spacing, gauge strokes, border, and corner radius remain proportional in preview and export.

Custom templates are atomically persisted through the template store. Creating a template assigns a user ID; updating one replaces only its `widgets` snapshot after a successful store commit, retaining its ID, metadata, and compatible unknown fields. The editor records an applied template separately from the picker selection, so a passive picker change cannot redirect an in-place update. Picker selection is a QSettings UI preference keyed by template ID (never list index), survives template-list refreshes and application restart, and is deliberately reconciled to the first available template only when its selected ID disappears. Project documents do not store template provenance: loading one clears the applied-template relationship while retaining the independent picker preference.

The scene uses 1920×1080 as its canonical visual canvas. Normalized widget geometry is resolved directly against the target canvas, while pixel-like typography, padding, borders, lines, and markers use one scene scale. Canvases use widget-relative geometry; the retro Grand Prix renderer is the exception and retains its explicit 440×420 design-space transform, so it must not receive a second scene transform. The offscreen renderer has no running GUI event loop, so initialization explicitly dispatches pending component events, completes one scene-graph preparation frame, and dispatches the completion events it produces before export frame zero. This is the readiness boundary that makes Canvas textures available without sleeps or timestamp-bearing throwaway frames.

The modern motorsport broadcast HUD uses `TelemetryPanel.qml` as its dark translucent panel family: `#16232D` at 78% opacity, a restrained `#96A8B8` border, compact 12 px radii, 15 px label / 56 px value / 15 px unit roles, near-white primary text, and muted `#C0CAD2` secondary text. Ten scene pixels of canonical padding keeps the cards dense without crowding their contents. Speed, pedals, Retro Custom values, Heart Rate, and the G-Force bar own this surface directly, so existing layouts immediately use the same visual system rather than a generic wrapper. Speed and Heart Rate are compact portrait cards; pedals and the three-row Retro Custom temperature stack share a short, medium-width module height; and the G-Force bar uses the same substantial panel family. Retro Custom values can opt into top/middle/bottom stack positions, subtle separators, and a small explicit icon while arbitrary standalone usage remains the default. Throttle is green, active brake is red, and G-Force is amber; a zero brake is represented solely by the neutral track. The analog Retro Tachometer remains circular with an integrated rounded dark RPM value plate and high-range accent, while the F1 radar remains an unboxed circular field with labeled 0.25 g rings through 1.50 g. `VisualSmokeScene.qml` is the deterministic compact composition review scene: it mounts these widgets over a bright roadway backdrop with sample-only data (4300 RPM, 86 km/h, 63% throttle, 18% brake, 97 °C oil, 84 °C ATF, 90 °C coolant, 145 bpm, 0.80 g), and the `motorsport-broadcast-smoke` template carries the same nine-widget layout for the editor and export scene. Local `--render-visual-smoke <png>` and `--render-visual-smoke-dark <png>` modes capture the production QML scene against bright and night-like backgrounds.

Optional widget `fontSize` values are canonical canvas pixels: zero or an absent/invalid value uses the renderer's established automatic expression, and a positive finite value is multiplied by the scene scale. This preserves old scenes and keeps explicit value typography proportional between preview, 720p, 1080p, 4K, and larger exports.

Classic G-Force, F1 G-Force Radar, and G-Force Bar renderers share a small presentation object that resolves the two channels, preserves missing-axis no-data behavior, applies per-widget axis inversion only for display, and derives combined magnitude. The F1 radar derives its concentric rings from `floor(maxG / ringStepG)` (defaults: 1.5 g and 0.25 g); it is a padded circular field with its own configurable dark background (`radarBackgroundColor`) and opacity, leaving the surrounding widget rectangle transparent. Its marker and the bar fill clamp visually, while the bar's numeric magnitude remains unbounded by `maxG`.

`TrackWidget.qml` keeps the normalized polyline in a static Qt Quick `Shape`/`PathPolyline` layer. Its pixel path is derived only from a geometry revision, widget dimensions, scene-scaled track padding, or mirroring; line width and color remain direct static shape properties. The current-position marker is a separate QML rectangle bound to `currentTrackPoint`, so playback and export time changes never reconstruct or repaint the full path.

## Export

`MediaProbe` reads explicit source and output raster, rate, codec/profile, bit-depth, orientation, and raw color metadata. `ExportMediaProfile` centrally derives the preservation format and encoder profile. `EncoderDetector` tests usable HEVC encoders and caches an exact raster/rate/pixel-format/profile probe; `TelemetryFrameRenderer` mounts `TelemetryScene.qml` offscreen through `QQuickRenderControl` and QRhi and checks the backend's reported texture limit. QRhi readback is canonical premultiplied RGBA and conditionally normalized from Y-up once before Stage A. Stage B keeps the 8-bit RGB composition contract, while the 10-bit YUV path explicitly unpremultiplies staged BGRA and uses straight-alpha blending before P010 conversion. `ExportEngine` also probes the production composition graph before telemetry sampling. Stage B retains original PTS for seek/trim and preserves audio's delay relative to the selected video range. The two-stage FFmpeg pipeline runs only after capability checks. Raw-frame transport uses bounded byte-oriented backpressure; representative FFV1 sampling counts encoded bytes without retaining payload bytes. All subprocess channels are incrementally drained: ffprobe JSON has a 4 MiB complete-payload limit, stderr keeps a 128 KiB tail, progress lines cap at 16 KiB, and worker messages cap at 128 KiB. `ExportOutputTransaction` creates and owns a same-directory staging output, rejects linked targets, snapshots an approved existing regular file's native identity and modification state, and commits only when that state still matches immediately before replacement. `AppController` consumes worker events and is the sole writer of the corresponding durable export diagnostic file, preserving the same formatted entries shown by Very Verbose without concurrent worker/UI file access. Color policy is documented in [media-color-policy.md](media-color-policy.md).

The detailed pipeline and timing contract are in [export-pipeline.md](export-pipeline.md). Target-file transaction guarantees are in [export-output-safety.md](export-output-safety.md).

## Candidate deployment

Release CI installs the executable, QML modules and Qt runtime via a quoted `qt6_generate_deploy_script` using `qt_deploy_qml_imports` and `qt_deploy_runtime_dependencies`, then checks the installed application with the build SDK hidden. Debug CI remains a separate gate. Archive identity and manual release acceptance are documented in [beta-acceptance.md](beta-acceptance.md).


KAN-17 traces synchronization from project validation to preview, analysis and
export. Central forward/inverse helpers return optional finite times; UI/render
consumers expose no data and worker progress emits JSON null on overflow. Finite
manual transforms retain the existing project schema. Auto-sync validates source
alignment/order before access, uses bounded integer grids and a shared coarse/fine
work budget, rejects insufficient numeric precision, and preserves existing
ambiguity and timing-edit protections. See [telemetry-semantics.md](telemetry-semantics.md#synchronization-transforms-and-numeric-bounds)
for the consumer map and explicit limits.
