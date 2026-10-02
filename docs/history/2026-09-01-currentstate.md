# Historical checkpoint — 1 September 2026

Archived on 11 September 2026. The text below is retained as historical evidence and deferred design context; it does not describe the current implementation or CI policy. See the [delivery ledger](../product-delivery.md) and [beta acceptance](../beta-acceptance.md); the later checkpoints are [12 September state](2026-09-12-currentstate.md) and [12 September roadmap](2026-09-12-roadmap.md).

# VBOOverlay / FlappedEar Telemetry — Current Development State

**State captured:** 2026-09-01
**Product-code baseline:** `3be684ee45b8c4b9c074f58ac1abc7bc7af0e8fc` plus this documentation update
**Repository:** `arekkozuch/VBOOverlay`  
**Baseline branch:** `main`  
**Application version in CMake:** `0.2.0`  
**Status:** alpha / active development

**CI policy update (2026-09-11):** the Cloud CI-disabled statements below describe this historical checkpoint. The current [Native CI workflow](../../.github/workflows/build.yml) enables macOS/Windows builds and synthetic tests; [docs/testing.md](../../docs/testing.md#cloud-ci) is authoritative for its scope, explicit hardware skip, and required local acceptance. Other checkpoint claims retain their original date and baseline.

> This document is a development checkpoint and handoff packet. It intentionally captures the project **before any Widget Runtime v2 / `.fewidget` refactor is implemented**. The later widget-system discussion is preserved near the end only as **deferred design context**, not as current behavior.
>
> The file combines three sources of truth: the repository at the baseline above, Git history from the initial commit through that baseline, and project-specific decisions/results retained from the development conversations. Where a conversation decision is not yet implemented, it is explicitly marked **context/deferred**, not presented as code reality.

---

## 1. One-paragraph project state

FlappedEar Telemetry is now a native Qt 6 / C++20 / QML desktop application for synchronizing motorsport telemetry with video, editing telemetry overlays, analyzing synchronized channels, deriving laps, and exporting frame-correct HEVC/AAC video. The established baseline still includes portable project/recovery handling, a shared preview/export scene, bounded inputs, staged exact-CFR export, and the accepted motorsport-broadcast HUD. The September 2026 product slice adds bounded RaceChrono Start gates, raw-GPS passage and lap derivation, fastest-lap state, Analysis lap list/seek, live best-lap comparison, and six independent lap/speed Best/Current/Delta tiles. The same session also hardened project transactionality, persistence validation, widget canvas geometry, rotation-aware editor controls, tile scaling, consistent speed presentation, and decoder-error visibility. Those additions are committed and published but have not yet passed the deferred macOS build/runtime/private-Jastrząb validation gate, so they are implemented rather than accepted as stable runtime behavior.

---

# PART I — PRODUCT AND DEVELOPMENT HISTORY

## 2. Product purpose

The product is intended to take a motorsport recording and recorded telemetry, synchronize them on one time axis, let the user inspect and compose a telemetry HUD, and produce a finished video without manually rebuilding data overlays in a video editor.

The intended real workflow is approximately:

1. Record driving video, typically GoPro MP4/MOV.
2. Record telemetry independently, primarily RaceChrono/VBOX-compatible `.vbo` data.
3. Optionally use GoPro embedded GPMF GPS as an independent synchronization signal.
4. Open video and telemetry in FlappedEar Telemetry.
5. Synchronize video time to telemetry time.
6. Inspect channels and track geometry.
7. Arrange/customize telemetry widgets.
8. Save the work as a `.fetproject`.
9. Export a deterministic HEVC/AAC MP4 where the rendered telemetry corresponds to the same absolute source time as preview.
10. Derive laps/session metrics from real track data and use them in both analysis and overlays; sectors and theoretical best remain a later phase.

The product is not meant to fabricate unavailable telemetry. In particular, missing brake data must remain missing; it must never be replaced by an unrelated channel or interpreted as measured zero.

---

## 3. Repository origin and first implementation — 2026-08-19

### 3.1 Initial repository

The repository began with:

- `d9548bf` — `Initial commit`

The initial commit contained only a minimal `README.md` heading.

### 3.2 First complete editor foundation

The next major commit was:

- `ba38360` — `feat: build telemetry editor foundation`

The first real application was an **Electron + React + TypeScript** prototype. Its architecture included:

- Electron main process for application lifecycle, file dialogs, FFprobe, encoder detection, and persistence;
- a constrained preload/IPC bridge;
- UI-independent telemetry parsing;
- React renderer/editor;
- a shared canvas-based overlay renderer;
- versioned `.fetproject` files;
- MP4/MOV inspection;
- VBO parsing;
- timeline scrubbing;
- manual synchronization;
- draggable/scalable widgets;
- track geometry;
- early synchronization algorithms;
- MapLibre-compatible map-provider abstractions;
- FFmpeg encoder discovery.

The initial Electron dependency stack included Electron, React, Vite, TypeScript, Vitest, ESLint, Prettier and MapLibre GL JS.

Important invariants already existed in the first engineering rules and survived the later rewrite:

- telemetry parsing stays independent from UI;
- media handling stays independent from widget UI;
- synchronization belongs in one central module;
- heart rate comes from the VBO data rather than a second HR subsystem;
- brake telemetry is never invented;
- synchronization is time-based, never frame-index based;
- preview and export should use the same scene definitions;
- parser and synchronization changes require deterministic tests.

### 3.3 Real telemetry validation existed early

A private RaceChrono VBO fixture was already part of the development process. The long-lived validation reference has approximately:

- 32,718 samples;
- 49 numeric channels;
- 3,271.7 seconds of telemetry;
- primary semantic aliases;
- signed arc-minute GPS conversion;
- zero parser warnings in the validated run.

The recording is private and deliberately ignored by Git.

---

## 4. GoPro synchronization and early editor maturation — 2026-08-19

Important early commits include:

- `887de4d` — `feat: add GoPro telemetry auto sync`
- `dc917b0` — `fix: preserve video playback across resize`
- `c2f6c3d` — `fix: constrain preview and support precise seeking`
- `1efda05` — `feat: allow per-widget telemetry channel selection`
- `7ffd478` — `feat: add fullscreen and custom telemetry widgets`

This established several long-term product behaviors:

- video and telemetry are synchronized in seconds;
- user widgets can select actual recorded channels;
- fullscreen preview is part of the editor workflow;
- the application must remain usable while resizing/scrubbing;
- GoPro metadata is useful for synchronization but ordinary video must still work without it.

The GoPro synchronization strategy evolved toward comparing GoPro GPS speed with VBO speed rather than assuming identical clocks or tying telemetry to video FPS.

---

## 5. Native Qt migration — 2026-08-19 to 2026-08-20

The Electron implementation became a prototype/behavior reference while production moved to a native stack:

- Qt 6;
- C++20;
- QML / Qt Quick;
- Qt Multimedia;
- CMake;
- Qt Test.

The native implementation reproduced the product behavior rather than attempting to keep two production architectures alive.

The legacy Electron code was finally removed in:

- `2106d0d` — `chore: remove legacy Electron prototype`

From that point onward, the durable engineering rule became:

> Qt 6 + C++ + QML is the only production application architecture.

The native architecture was chosen to provide a focused desktop application, explicit native ownership/resource behavior, and a smaller/cleaner production runtime than continuing the Electron prototype. During migration there was a preliminary development measurement of roughly 126 MB for a deployed Qt runtime versus roughly 328 MB for the old Electron prototype; this was an engineering observation, not a final release-size guarantee.

---

## 6. Branding, analysis workspace and product identity — 2026-08-20

Important commits:

- `178b705` — `feat: add telemetry app branding`
- `53b7bf5` — `feat: add subtle configurable logo widget`
- `c3a13d8` — `docs: prioritize analysis workspace and video export`
- `6827484` — `feat: add synchronized telemetry analysis panel`
- `80ea7cc` — `fix: refresh and widen telemetry charts`
- `779ce23` — `feat: detach and resize telemetry analysis panes`
- `edf4785` — `fix: restore editor when leaving fullscreen`

The app became **FlappedEar Telemetry** rather than merely a VBO overlay experiment.

The analysis workspace introduced synchronized channel inspection alongside the video. It later became a detached/lazy-loaded window so a second video decoder exists only while Analysis is actually open.

This established the current distinction:

- **overlay presentation** may use short bounded hold/smoothing to look stable;
- **analysis** consumes raw telemetry samples and must preserve gaps/extrema rather than inherit overlay smoothing.

---

# PART II — CURRENT NATIVE ARCHITECTURE

## 7. Technology and build contract

Current required stack:

- CMake 3.24+;
- C++20 compiler;
- Qt 6.8+;
- Qt components: Concurrent, Core, Gui, **GuiPrivate**, Qml, Quick, QuickControls2, Multimedia, Test;
- external FFmpeg and ffprobe at runtime.

`GuiPrivate` is used only internally because the offscreen export renderer uses QRhi/private Qt GUI APIs. This is intentionally treated as version-sensitive implementation detail rather than exposed public API.

Current product version declared in `native/CMakeLists.txt`:

- bundle short version: `0.2.0`;
- bundle version: `1`.

Primary current development platform is macOS. Windows has one known validated runtime/export configuration, but Windows is not the development source of truth and does not yet represent broad hardware coverage.

---

## 8. Top-level module map

Current C++ source areas:

```text
native/src/
├── app/
│   ├── AppController.*
│   └── AppLog.*
├── export/
│   ├── BoundedProcessOutput.*
│   ├── EncoderDetector.*
│   ├── ExportArtifactManifest.*
│   ├── ExportCancellation.*
│   ├── ExportDiagnostics.*
│   ├── ExportEngine.*
│   ├── ExportFormat.*
│   ├── ExportMediaProfile.*
│   ├── ExportOutputTransaction.*
│   ├── ExportProcessSupervisor.*
│   ├── ExportProgress.*
│   ├── ExportStoragePolicy.*
│   ├── ExportTargetIdentity.*
│   ├── FfmpegTools.*
│   ├── MediaProbe.*
│   ├── PersistentExportLog.*
│   ├── RawFrameTransport.*
│   ├── TelemetryFrameRenderer.*
│   └── TemporaryOverlayValidation.*
├── gopro/
│   └── GoProTelemetrySource.*
├── project/
│   ├── BoundedJsonLoader.*
│   ├── ProjectDocumentState.*
│   ├── ProjectLimits.*
│   ├── ProjectRecoveryStore.*
│   ├── ProjectSourceReference.*
│   └── ProjectWriter.*
├── sync/
│   └── TelemetrySyncEngine.*
├── telemetry/
│   ├── SourceOperation.h
│   ├── TelemetryRenderContext.*
│   ├── TelemetrySession.*
│   ├── TrackGeometry.*
│   └── VboParser.*
└── widgets/
    └── WidgetModel.*
```

Current QML is in `native/qml/`, with `Main.qml`, `InspectorPanel.qml`, Analysis UI, custom FlappedEar controls, `TelemetryScene.qml`, visual smoke scenes, and one QML file per renderer under `native/qml/widgets/`.

---

## 9. Architectural data flow

The current architecture is effectively:

```text
video + VBO / .fetproject
        |
        v
   AppController
      |     \
      |      \ async probe/parse
      |       +--> MediaProbe
      |       +--> VboParser --> TelemetrySession
      |       +--> TrackGeometry
      |       +--> GoProTelemetrySource
      |       +--> TelemetrySyncEngine
      |
      +--> WidgetModel
      +--> SyncTransform
                |
                v
      TelemetryRenderContext
          /             \
         v               v
   preview QML     offscreen export context
         \               /
          +-- TelemetryScene.qml --+
                                   |
                           TelemetryFrameRenderer
                                   |
                         QQuickRenderControl / QRhi
                                   |
                            FFV1/BGRA Stage A
                                   |
                            FFmpeg Stage B
                                   |
                             HEVC/AAC MP4
```

The most important architecture invariant is that preview and export mount the same `TelemetryScene.qml`. Export is not allowed to have a separate simplified telemetry implementation.

---

## 10. AppController

`AppController` is the main QML-facing application boundary.

It currently owns/exposes concerns including:

- video source state;
- telemetry source state;
- source generations/cancellation;
- playback time;
- synchronization offset/time scale;
- live telemetry values;
- `WidgetModel`;
- project path/document state;
- dirty state;
- recovery state;
- source loading/relinking;
- analysis data requests;
- export state/progress/diagnostics;
- persistent export-log lifecycle.

It is large (`AppController.cpp` is over 100 KiB at this checkpoint), and decomposition was identified in audit context as a future maintainability opportunity. It is **not** currently considered a reason to destabilize working architecture.

---

# PART III — TELEMETRY

## 11. VBO parser

`VboParser` treats VBO data as untrusted input.

Current hard bounds include:

- maximum VBO size: 128 MiB;
- maximum lines: 1,000,000;
- maximum data rows: 500,000;
- maximum columns: 512;
- maximum line length: 1 MiB;
- maximum individual field length: 64 KiB;
- parser warnings retained: maximum 200 before summary.

Parsing is cooperatively cancellable.

### 11.1 Supported time syntax

The parser understands:

- `HH:MM:SS[.fraction]`;
- compact `HHMMSS[.fraction]`;
- plain finite seconds.

Clock timestamps can cross midnight once under the explicit rollover rule. Duplicate/later-equal timestamps are skipped and non-rollover backwards timestamps are skipped, so emitted telemetry is strictly monotonic.

### 11.2 GPS representation

RaceChrono coordinates represented as signed total arc-minutes are converted to degrees when magnitude identifies that representation.

---

## 12. TelemetrySession and no-data semantics

`TelemetrySession` is the raw telemetry model and lookup boundary.

Supported lookup behavior includes previous, nearest and linear interpolation semantics, but missing values remain missing.

Hard semantic rule:

> Missing telemetry is not numeric zero.

Examples:

- outside channel time range -> no data;
- exact missing sample -> no data;
- linear interpolation requires two finite adjacent samples;
- previous does not search backward through missing gaps;
- nearest does not skip a missing nearest sample in favor of a farther finite sample;
- public lookup never exposes NaN or infinity.

A finite measured `0` remains valid telemetry.

This matters especially for pedals and G-force. A missing brake channel must not turn into a fake zero-brake measurement.

---

## 13. TelemetryRenderContext

`TelemetryRenderContext` is the presentation boundary shared by preview/export.

Central time transform:

```text
telemetryTime = videoTime * timeScale + offset
```

Telemetry is never coupled to FPS.

The render context also applies small presentation-only smoothing/holding while leaving raw `TelemetrySession` unchanged.

Current stale-hold policy:

- ordinary channels: up to 750 ms;
- heart rate: up to 2 s.

Current smoothing windows:

- speed/RPM: 150 ms;
- lateral/longitudinal G: 200 ms;
- throttle/brake: 100 ms;
- heart rate: 250 ms;
- other continuous channels: 150 ms;
- gear: no smoothing.

A channel timestamp jump beyond the cadence-derived tolerance is treated as a real gap rather than bridged indefinitely.

The median positive channel interval is lazily cached so repeated presentation lookups do not rescan the entire channel.

---

## 14. Track geometry

`TrackGeometry` derives an offline normalized track outline from valid latitude/longitude telemetry.

Important behavior:

- invalid/non-finite coordinates are ignored;
- latitude and longitude bounds are checked;
- unavailable coordinates mean unavailable current position;
- track aspect ratio is preserved;
- static geometry is cached;
- playback-time changes move only a separate current-position marker.

`TrackWidget.qml` uses a static Qt Quick `Shape`/`PathPolyline` for the course and a separate marker for current position. Playback/export time must never rebuild the entire static track path.

---

# PART IV — GOPRO AND SYNCHRONIZATION

## 15. GoProTelemetrySource

The app can discover a GoPro `gpmd` metadata stream and decode supported GPS5/GPS9 records.

Important defensive limits include:

- packet count <= 100,000;
- aggregate GPMF bytes <= 512 MiB;
- parsed KLV headers <= 1,000,000;
- container depth <= 32;
- ffprobe process timeout: 120 s;
- bounded ffprobe stdout.

Packet offsets/sizes are validated before allocation.

Decoded GPS samples are sorted stably by time; later duplicates are removed while preserving the first sample at a timestamp. Published channel timestamps are finite and strictly increasing.

Ordinary video import remains valid even if GoPro metadata is absent or malformed.

---

## 16. GPS-speed auto-sync

`TelemetrySyncEngine` compares GoPro GPS speed against VBO speed using normalized correlation and produces an offset/time-scale candidate plus confidence diagnostics.

The numerical algorithm is time-domain based. Async results are protected by source generation and identity checks so a stale result cannot modify a newer source.

A historically validated private source pair produced approximately:

- offset: `+90.217 s`;
- correlation: `0.999575`.

The current validated private GoPro regression also observed:

- 1,536 GPMF packets;
- 259,584 parsed KLV headers;
- 14,796 GPS9 samples.

These figures are development evidence for one private fixture, not broad device compatibility guarantees.

---

# PART V — SOURCE LOADING AND ASYNC SAFETY

## 17. Source lifecycle

Video and telemetry sources have independent states such as:

- idle;
- loading;
- ready;
- missing;
- mismatch;
- error.

Source loads, project-resolved sources and relink candidates run asynchronously.

Each new operation gets a source generation and normalized identity. Results are committed only if generation and identity still match current state.

Long operations are cooperatively cancellable. Generation checks and cancellation solve different problems:

- generation prevents stale results from committing;
- cancellation prevents obsolete work from consuming CPU/process resources.

Both are required.

---

# PART VI — PROJECTS, SAVING, RECOVERY AND RELINKING

## 18. `.fetproject`

Current project schema is version 2.

The saved `.fetproject` is the authoritative **clean** document state.

Unknown top-level and nested JSON fields are retained when the application overlays known edits and saves. This provides forward compatibility without treating unknown fields as application preferences.

Current documents can preserve:

- widget scene;
- groups/cues/settings;
- synchronization;
- analysis channel configuration;
- source references/fingerprints;
- document identity/revision;
- other compatible unknown fields.

Transient window state is not project content.

---

## 19. Project resource limits

Current documented JSON/resource bounds include:

- project/recovery document <= 4 MiB;
- imported template <= 2 MiB;
- template store <= 8 MiB;
- widgets/project <= 256;
- cues/widget <= 256;
- cues total <= 4,096;
- settings entries/widget <= 128;
- custom templates/store <= 128;
- JSON nesting <= 32;
- ordinary strings <= 4,096 chars;
- IDs <= 128 chars;
- template names <= 160 chars;
- descriptions <= 2,048 chars.

Malformed or over-limit documents are rejected rather than truncated silently.

---

## 20. Atomic saving and dirty state

`ProjectWriter` uses `QSaveFile` with direct-write fallback disabled.

A project is marked saved only after the atomic write commits successfully.

New/Open/Quit destructive actions respect dirty state.

The project loader validates the document first and commits the document transactionally before resolving external assets. Missing media does not roll back the valid project scene.

---

## 21. Recovery architecture

Recovery is explicitly separate from the saved project.

A recovery snapshot represents **unsaved** persistent edits. It must never become the clean document merely because the app restarted.

Recovery v2 includes:

- document identity;
- project path/origin context;
- recovery revision;
- saved revision;
- complete project payload.

Startup classifies v2 recovery logically:

- newer matching recovery -> recoverable;
- equal/older matching authority -> stale;
- malformed/identity mismatch -> invalid;
- legacy v1 -> conservatively recoverable.

### 21.1 Important recovery fixes

`4972935` — `fix: invalidate stale recovery after successful save`

Established that a recovery snapshot physically left behind after a successful Save can be proven logically stale. Failure to delete it is cleanup debt, not failed data protection.

`800151f` — `fix: persist recovery discard intent`

Introduced a durable sibling `.discard` tombstone with:

- `discardVersion`;
- `documentId`;
- `discardedThroughRevision`.

This fixed the case where Quit/New/Open/Discard could fail to honor the user's explicit discard if snapshot deletion failed. Once discard intent is durably recorded, a matching residual snapshot at or below the bound stays suppressed. Newer/different recovery remains available.

Recovery cleanup debt must not incorrectly set `recoveryDegraded`.

---

## 22. Portable source references

`6cab87c` — `feat: support portable project media relinking`

Project v2 can store `sources.video` and `sources.telemetry` with:

- project-relative path where useful;
- normalized absolute fallback;
- lightweight source fingerprint.

Relative lookup is anchored only to the project directory, never process CWD/home/app directories.

Fingerprints are deterministic identity metadata, not whole-file cryptographic identity. They sample bounded head/middle/tail bytes and include relevant media/telemetry metadata.

A project can open without one or both external assets. Relinking is explicit, parsed/probed before commit, and fingerprint mismatch requires user confirmation.

---

# PART VII — WIDGET SYSTEM AS IT EXISTS NOW

## 23. Current widget architecture

`WidgetModel` owns persistent widget data, groups, cues and templates.

It is the central semantic normalizer for:

- direct edits;
- scene import;
- template application;
- template import;
- persistent-template reload.

It normalizes finite geometry/numbers, colors, ranges, cue values and IDs while preserving compatible unknown settings.

`TelemetryScene.qml` is render-only. It has no editor-selection or `MediaPlayer` dependency.

The scene maps each widget type to one renderer QML file through a `Loader`.

Current mapped renderer types are:

1. `brandLogo`
2. `speed`
3. `rpm`
4. `heartRate`
5. `pedals`
6. `gForce`
7. `f1GForceRadar`
8. `gForceMagnitudeBar`
9. `arcGauge`
10. `dialGauge`
11. `telemetryOverlay`
12. `retroGrandPrix`
13. `retroTachometer`
14. `retroGear`
15. `retroPedal`
16. `retroSpeedArc`
17. `retroNameplate`
18. `customValue`
19. `retroCustomValue`
20. `track`

Shared QML helpers include at least:

- `TelemetryPanel.qml`;
- `GForceData.qml`.

The current architecture deliberately instantiates one renderer per widget rather than one giant renderer component.

---

## 24. Canonical visual canvas

The visual scene uses a canonical 1920×1080 coordinate system.

Widget position/size are normalized against the actual target canvas. Pixel-like typography, padding, borders, markers and lines scale from the canonical scene scale.

Optional explicit `fontSize` values are canonical-canvas pixels; zero/absent retains renderer-specific automatic sizing.

The retro Grand Prix renderer retains its explicit internal 440×420 design-space transform and is intentionally not double-scaled.

---

## 25. Current accepted HUD direction

The accepted current product style is a compact modern motorsport broadcast HUD with a restrained retro influence — not neon/cyberpunk/glass.

General treatment:

- dark translucent charcoal panels;
- restrained light border;
- near-white primary typography;
- muted secondary labels;
- green throttle;
- red active brake;
- amber G-force;
- circular retro tachometer with red warning/redline treatment.

`TelemetryPanel.qml` is the shared modern panel family for the accepted layout.

Current production layout uses:

- Retro Tachometer;
- Speed;
- Pedals;
- three Retro Custom Value rows for OIL / ATF / COOLANT;
- Heart Rate;
- F1 G-Force Radar;
- G-Force Magnitude Bar.

---

## 26. User-provided canonical working template: `proper.fettemplate`

A real user-created template named `proper.fettemplate` was supplied during development discussion immediately before this checkpoint. It is **not the historical built-in `Motorsport Broadcast HUD` template** and its internal name `propertemplate` is only a temporary personal label used to distinguish it from older built-ins.

It should be treated as the strongest current product-layout reference when future HUD changes are considered.

It contains **9 widget instances / 7 renderer types**:

| Instance | Type | Important source/settings | Normalized geometry |
|---|---|---|---|
| Tachometer | `retroTachometer` | `source=rpm`, max 9000, warning 7500 | x 0.01347545, y 0.72775341, w 0.15279080, h 0.27224659 |
| Speed | `speed` | `source=speed`, km/h | x 0.40588983, y 0.85146415, w 0.07034364, h 0.14037079 |
| Pedals | `pedals` | accelerator `accelerator_pos-obd`, brake `brake_pos-obd` | x 0.48247236, y 0.86116988, w 0.16, h 0.135 |
| Oil | `retroCustomValue` | `engine_oil_temp-obd`, OIL, °C | x 0.64395708, y 0.86316999, w 0.18, h 0.045 |
| ATF | `retroCustomValue` | `gearbox_temp-obd`, ATF, °C | x 0.64395708, y 0.90816999, w 0.18, h 0.045 |
| Coolant | `retroCustomValue` | `coolant_temp-obd`, COOLANT, °C | x 0.64395708, y 0.95316999, w 0.18, h 0.045 |
| HR | `heartRate` | `heartRate`, bpm | x 0.905, y 0.00096638, w 0.095, h 0.15 |
| Radar | `f1GForceRadar` | lat `latacc-calc`, long `longacc-calc`, longitudinal inverted, max 1.5g, 0.25g step | x 0.84744019, y 0.60471385, w 0.145, h 0.265 |
| G bar | `gForceMagnitudeBar` | lat `latacc-calc`, long `longacc-calc`, max 1.5g | x 0.82602456, y 0.88264094, w 0.17, h 0.105 |

The three temperature widgets share one group ID in the template and use top/middle/bottom stack positions.

### 26.1 Known stale-setting discrepancy

`proper.fettemplate` and `widget-templates.json` both contain `showRingLabels: true` for the F1 radar. The current `F1GForceRadarWidget.qml` renderer does **not** render numeric ring labels at all and does not consume that setting.

The current conversation-level visual intent is also six **unlabeled** rings.

Therefore `showRingLabels` is currently a stale/dead setting, and older architecture prose that says the current radar has labeled rings is outdated. Do not silently reintroduce ring labels just because the stale JSON property exists.

---

## 27. Built-in templates

`native/resources/widget-templates.json` currently remains a centralized store for:

- common defaults;
- type-specific widget defaults;
- built-in templates.

Historical built-ins include layouts such as Track Day, Minimal, Broadcast, Motorsport Broadcast HUD, Performance, Circuit Pro, Endurance, Drag Strip, Clean HUD and retro/Grand-Prix-oriented layouts.

Custom templates are stored separately and are atomically persisted. Built-ins are immutable; custom templates support Save current / Save as new semantics.

At this checkpoint **template cleanup has not been implemented**.

---

# PART VIII — ANALYSIS WORKSPACE

## 28. Analysis behavior

Analysis is a secondary workspace for synchronized telemetry inspection.

Important current behavior:

- channel selection is project content;
- Analysis-window visibility is transient UI state;
- Analysis starts closed on startup/New/Open;
- its secondary `MediaPlayer` is created only while the window is open;
- closing Analysis releases that decoder;
- keyboard transport forwards to the same primary playback model;
- raw analysis data preserves gaps.

Raw analysis ranges use actual samples rather than presentation-filtered overlay values.

Display decimation is min/max bucket based so short pedal/RPM/acceleration extrema are retained where practical. Gap segments remain separate paths rather than being connected visually.

Current roadmap work still includes chart zoom, range selection, annotations and configurable axes.

---

# PART IX — EXPORT ARCHITECTURE

## 29. Why export became a major engineering area

The first export implementation evolved through many correctness fixes because a telemetry overlay must be correct simultaneously in:

- time;
- frame cadence;
- source range;
- alpha semantics;
- color depth;
- output-file safety;
- cancellation/process cleanup;
- GUI/offscreen QML behavior.

The current pipeline is intentionally staged rather than a live FFmpeg secondary-overlay pipe.

Do **not** restore the earlier unsafe live-overlay approach without strong evidence.

---

## 30. Export Stage 0 — probe and capability preflight

`MediaProbe` reads source characteristics including:

- coded/display raster;
- exact/average frame rates;
- codec/profile;
- pixel format;
- bit depth when known;
- bitrate;
- sample aspect ratio;
- rotation;
- FFmpeg color range/matrix/transfer/primaries.

`ExportMediaProfile` creates the source-preserving policy.

Current policy:

- 8-bit SDR -> HEVC Main;
- 10-bit SDR -> HEVC Main10;
- HDR/HLG/PQ/Log -> reject at export preflight until a real color-managed compositor exists;
- non-zero rotation -> reject export;
- valid non-square SAR -> reject export;
- import/probe/playback/editing still remain available for those files.

No arbitrary product-level 4K ceiling exists. Native export is a runtime capability decision based on raster, renderer and encoder.

`EncoderDetector` tests the exact requested raster/rate/pixel format/profile and caches capability decisions.

The offscreen renderer checks QRhi texture limits and checked frame-byte arithmetic before rendering.

---

## 31. Exact CFR policy

The whole export uses one authoritative rational frame rate.

For selected interval `[start, end)`:

```text
expectedFrames = ceil((end - start) * rate)
outputDuration = expectedFrames / rate
frame N source time = start + N / rate
```

The rational is preserved exactly; it is not converted to a double and reconstructed later.

VFR input is converted deterministically onto the chosen final CFR cadence.

---

## 32. Export Stage A — render/stage telemetry

`TelemetryFrameRenderer` mounts the same `TelemetryScene.qml` used by preview through `QQuickRenderControl` and QRhi.

For each scheduled source-time frame it obtains an RGBA image and writes raw frames through bounded backpressure to FFmpeg.

Stage A output:

- codec: FFV1;
- pixel storage: BGRA;
- container: temporary Matroska;
- purpose: complete lossless authored telemetry overlay.

The completed overlay is validated before Stage B begins.

This staging exists because a live FFmpeg framesync secondary stream can allow the primary source to advance ahead of the telemetry producer and reuse stale overlay frames.

---

## 33. Raw-frame transport

Raw-frame transport is byte-oriented and bounded.

Current transport behavior includes:

- writes in chunks (at most 1 MiB);
- 8 MiB high-water / 4 MiB low-water queue policy;
- cancellation awareness;
- process-exit detection;
- partial/rejected write handling;
- sustained-stall handling.

A frame counts as submitted only after all bytes are accepted.

`QProcess` must never be assumed to buffer a whole raw 4K frame safely.

---

## 34. Premultiplied-alpha contract

Qt Quick/QRhi Stage A pixels have an explicit premultiplied-alpha contract.

QRhi readback is treated as premultiplied RGBA. Framebuffer Y orientation is normalized once at the boundary when required.

An earlier critical fix:

- `e9228d` — `fix: correct premultiplied overlay composition`

established deterministic alpha semantics and regression fixtures, including antialiased/translucent edges and byte-exact Stage A preservation.

---

## 35. Export Stage B — source composition and final encode

Stage B begins only after Stage A is complete and validated.

For a non-zero range it uses a bounded five-second input preroll:

```text
inputSeek = max(0, requestedStart - 5s)
localStart = requestedStart - inputSeek
localEnd = requestedEnd - inputSeek
```

Source video/audio are trimmed on the local post-seek timeline, rebased to zero, converted to the authoritative CFR rate and composed with the already-zero-origin staged overlay.

The final output is HEVC + optional AAC in MP4.

A dedicated range bug was fixed in:

- `2d15655` — `fix: seek source before ranged export composition`

which prevented late source ranges from unnecessarily decoding the entire source prefix and corrected local range mapping.

---

## 36. Critical Main10 color incident and final fix

### 36.1 Symptom

A real 4K60 HEVC Main10 source could export with nominally correct metadata but visually catastrophic colors:

- orange/yellow sky;
- magenta vegetation;
- cyan road.

Software and VideoToolbox decode both showed the corruption, proving the encoded stream itself was wrong rather than merely a playback/display issue.

### 36.2 Diagnosis

The bad boundary was the 10-bit YUV overlay composition path.

The staged overlay was premultiplied BGRA. Converting a fully transparent premultiplied BGRA overlay into YUVA10 creates neutral chroma offsets even where alpha is zero. Treating that YUV data as if its chroma offsets were already premultiplied by alpha caused transparent overlay pixels to inject false color into the source.

Source-only YUV10 was good. P010 conversion was good. The first corruption appeared at the overlay-composition boundary.

### 36.3 Fix

- `f36dfc2` — `fix: preserve Main10 colors through VideoToolbox export`

The 10-bit Stage B path now conceptually:

1. keeps Stage A premultiplied FFV1/BGRA unchanged;
2. converts overlay to planar RGBA;
3. explicitly **unpremultiplies** it;
4. marks/interprets it as straight alpha;
5. composites in `yuv420p10` using straight alpha;
6. converts to P010 for VideoToolbox Main10 encoding.

The 8-bit path remains on its validated existing contract.

### 36.4 Regression evidence

Before the fix the deterministic color fixture had catastrophic per-channel error (red MAE around 186 in the reported RED state). After the fix, the deterministic regression produced near-zero transparent-path error.

A real short 3840×2160 `60000/1001` full-range BT.709 Main10 export with the production overlay passed with 351 video packets and AAC. A separate documented run measured roughly 44.86 dB PSNR in an overlay-free crop; the later Canvas validation run measured about 45.56 dB on its chosen crop.

### 36.5 FFmpeg development environment context

During diagnosis, Homebrew FFmpeg 8.0.1 did not expose the required `setparams` alpha-mode behavior used in experimentation. The macOS development environment was upgraded to **FFmpeg 9.0.1** at `/opt/homebrew/bin/ffmpeg`, after which full local build/CTest remained green and the needed alpha-mode support was present.

This is a development-environment fact; the release/runtime dependency contract is still not fully packaged/enforced.

---

## 37. Critical Canvas/offscreen export incident and final fix

### 37.1 Symptom

After the Main10 color fix, GUI preview showed the tachometer correctly but application export omitted it. Other widgets remained present.

### 37.2 Diagnosis

Model data and preview/export model state were byte-equivalent. The widget was visible, geometry/RPM were valid and the QML Loader had created the item.

The failure was in raw Stage A QML/QRhi rendering:

- Canvas item existed;
- Canvas was not yet available/painted;
- export worker drove Qt Quick manually without a running GUI event loop;
- readback happened before pending Canvas initialization/paint work had completed.

The deterministic RED regression showed the problem affected **all five Canvas-backed renderer types** tested:

- `retroTachometer`;
- `arcGauge`;
- `dialGauge`;
- `retroGrandPrix`;
- `retroSpeedArc`.

### 37.3 Fix

- `0749eff` — `fix: preserve Canvas widgets in offscreen export`

`TelemetryFrameRenderer` initialization now:

1. dispatches pending component-initialization events;
2. completes one offscreen scene-graph preparation frame **without readback**;
3. dispatches completion events produced by that frame;
4. only then begins requested export frame zero.

No sleeps, arbitrary waits, retries or throwaway timestamp-bearing frames are used.

This is now a durable export invariant in `AGENTS.md`.

### 37.4 Validation

The pixel regression requires Canvas-colored pixels at the first tested non-zero telemetry frame and later frame, and a repeated render at the same requested telemetry time must be pixel-identical.

A real short `GX010082.MP4` export showed the tachometer at decoded timestamps around 0.860 s and 2.002 s while preserving the other eight overlay widgets and correct color.

The user also manually exported from the application and confirmed the export worked.

This commit is the **product-code baseline of this document**.

---

# PART X — OUTPUT SAFETY, PROCESS SAFETY AND DIAGNOSTICS

## 38. Export-output transaction

FFmpeg never writes directly to the final user-selected target.

`ExportOutputTransaction` owns a same-directory staging output and commits it only after final validation.

Existing targets require explicit overwrite consent bound to the target's prepare-time state/identity. Immediately before replacement, the app verifies that the target has not been replaced/modified/disappeared unexpectedly.

Symlink/link targets and unsafe identity changes are rejected where platform facilities allow.

Representative important commits:

- `eec5e2c` — `fix: make export output transaction crash-safe`
- `7afa18b` — `fix: bind export overwrite consent to target state`

---

## 39. Export storage policy

The application estimates temporary Stage A storage from representative telemetry-overlay samples rather than assuming raw-RGBA size for every staged frame.

Preflight checks temporary and destination filesystem capacity separately and retains safety margins/reserve.

During encoding temporary volume capacity is periodically rechecked.

Important commits include:

- `6008d4b` — `feat: harden export artifact lifecycle`
- `c380e7d` — `fix: estimate FFV1 staging from representative samples`

---

## 40. Cancellation and process trees

Export cancellation spans preparation, probes, encoder detection, Stage A and Stage B.

A cancellation marker is used where available. If marker creation fails, the GUI falls back to synchronous supervised worker shutdown rather than falsely declaring cancellation complete while a worker continues.

On macOS/Unix the worker/processes use a dedicated process group for tree termination.

On Windows the top-level worker uses a kill-on-job-close Job Object and reports setup errors explicitly.

Windows process-tree behavior has been validated on the known Windows configuration, but broader Windows failure-mode coverage remains incomplete.

---

## 41. Bounded subprocess outputs

FFmpeg/ffprobe are treated as untrusted external-output sources.

Current important limits include:

- ffprobe JSON complete payload <= 4 MiB;
- retained FFmpeg stderr tail roughly 128 KiB;
- pathological machine-readable progress line <= 16 KiB;
- bounded worker message sizes;
- representative sample output counted/discarded rather than retained indefinitely.

Hardening commits:

- `a871f58` — `fix: bound external document and process inputs`
- `2fdf00c` — `test: cover bounded input hardening`
- `7d59ac9` — `fix: bound encoder discovery output`

---

## 42. Application and export logs

A persistent application log (`flappedear.log`) exists.

Each prepared export also gets a dedicated durable support log under the application-data `exports` directory.

The log includes source/resolved format/range and the same formatted diagnostic stream visible in Very Verbose mode, followed by success/cancel/failure lifecycle state.

Approximate retention keeps the newest ten matching export logs.

Important commits:

- `4d4607a` — `feat: add simple persistent application log`
- `675ff24` — `feat: add live export diagnostics`
- `278f28b` — `feat: persist per-export diagnostic logs`

Very Verbose UI behavior has a specific invariant: if the user scrolls into history, new logs must not move that historical viewport. Only explicit **Jump to latest** resumes tail following.

---

# PART XI — MEDIA/COLOR POLICY

## 43. Source-driven media characteristics

Key commits:

- `8f2330c` — `feat: make export media characteristics source-driven`
- `274ce5b` — `fix: validate real HERO11 media characteristics`

Raster, bit depth and color characteristics are modeled as source data, not arbitrary product presets.

Current media model keeps:

- coded/display dimensions;
- exact rates;
- codec/profile;
- pixel format/bit depth;
- bitrate;
- orientation/rotation;
- SAR;
- color range;
- matrix;
- transfer;
- primaries.

Ten-bit is not treated as synonymous with HDR.

Current supported preservation target is SDR 8-bit/10-bit. HDR/Log still fails preflight rather than being silently converted or mislabeled.

---

## 44. Real HERO11 validation

One private GoPro HERO11 fixture was:

- HEVC Main10;
- 5312×2988;
- `60000/1001`;
- full-range BT.709.

It passed native 5312×2988 and 3840×2160 Main10 exports on the macOS/Metal/VideoToolbox development configuration, with 442 final video packets and AAC for each documented short validation.

This validates one fixture/platform path, not production 8K/HDR or broad GPU support.

---

# PART XII — UI AND EDITOR BEHAVIOR

## 45. Main editor

Current editor includes:

- MP4/MOV playback;
- primary timeline/scrubber;
- source controls;
- widget scene/editor;
- Inspector;
- template controls;
- sync controls;
- export UI;
- detached Analysis window;
- fullscreen preview.

Supported minimum editor size is 1180×720. Sidebar controls must remain reachable through coherent scrolling rather than being trapped below nested fixed scrollers.

Playback shortcuts are centrally suppressed while text/numeric/interactive editor controls are focused.

Keyboard behavior documented in README includes:

- Space: play/pause;
- Left/Right: ±5 s;
- Shift+Left/Right: ±30 s;
- Home/End: timeline bounds;
- Ctrl/Cmd+E: Export;
- Ctrl/Cmd+Shift+A: Analysis;
- F11/Escape: fullscreen enter/leave.

---

## 46. Widget customization

Relevant commits:

- `506e013` — `feat: improve widget and template editing`
- `83e3319` — `feat: expand telemetry widget customization`
- `2617ced` — `fix: refine F1 G-force radar styling`
- `b3976a8` — `Fix template picker selection state`

Widget settings currently cover extensive per-type data binding, formatting, colors, fonts, ranges and specialized behavior.

The present implementation is still partly hard-coded across `WidgetModel`, `Main.qml`, `InspectorPanel.qml`, `TelemetryScene.qml` and `widget-templates.json`. This is technical debt, but it is not the immediate next milestone.

---

## 47. Visual redesign sequence — 2026-08-24/25

The current accepted broadcast HUD emerged through a concentrated visual iteration, including:

- `52c6ed2` — `Implement motorsport broadcast telemetry HUD`
- `9774db8` — `refactor: unify telemetry broadcast styling`
- `ad90855` — `fix: match broadcast HUD acceptance mockup`
- `2646717` — `fix: refine broadcast widget proportions`
- `4401e3d` — `fix: finalize broadcast overlay composition`
- `21f559d` — `fix: polish tachometer and simplify g-force radar`
- `bf9abb2` — `fix: rebuild tachometer dial`
- `0cfe3c9` — `fix: refine rpm readout and temperature icons`
- `a90d8c6` — `fix: restore tachometer scale and temperature symbols`
- `deaad4e` — `fix: use external temperature icons`
- `89258d3` — `fix: set tachometer redline and standard icons`
- `6e6019f` — `fix: embed supplied temperature icons`

Current embedded temperature assets:

- `change-car-oil-svgrepo-com.svg`;
- `temperature-transmission.svg`;
- `engine-coolant-svgrepo-com.svg`.

The current tachometer target is a circular 0–9 dial, large integrated RPM readout and warning/redline beginning at 7500 RPM.

---

# PART XIII — TESTING AND VALIDATION

## 48. Required local gate

Normal local validation gate:

```bash
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
```

`git diff --check` is also part of normal completion hygiene.

**Cloud CI is intentionally disabled.**

`qmllint` must **not** be run in this project workflow; a previous macOS invocation consumed roughly 265 GB of memory. QML correctness is instead covered through builds, startup smoke, render tests and real application checks.

---

## 49. Test architecture

The test target is primarily a large Qt Test suite in:

- `native/tests/TelemetryTests.cpp`

with helper executables such as:

- `ProbeTestHelper.cpp`;
- `RawTransportConsumer.cpp`.

Coverage is broad and includes:

### Telemetry

- VBO parsing;
- malformed inputs;
- size/cardinality limits;
- cancellation;
- timestamp formats/rollover/monotonicity;
- coordinate conversion;
- missing-vs-zero semantics;
- interpolation and presentation hold/smoothing.

### Analysis

- gap segmentation;
- nested segment transport to QML;
- partial overlap;
- constant traces;
- cadence-based gaps;
- min/max decimation;
- cache behavior.

### Synchronization / GoPro

- deterministic and ambiguous sync;
- cancellation;
- GPS9/GPMF decoding;
- malformed packet extents;
- depth/header limits;
- sorting/deduplication;
- optional real-fixture synchronization.

### Widgets / QML

- widget/group/cue/template behavior;
- normalization;
- custom-template persistence/rollback;
- G-force defaults/persistence;
- track geometry cache;
- QML guard against time-driven static-path reconstruction;
- production-RHI pixel regression for Canvas-backed renderers.

### Projects/recovery

- atomic save;
- dirty actions;
- saved/project-less recovery;
- recovery v2 validity/staleness/mismatch;
- Save As;
- deletion failure;
- discard tombstone behavior;
- portable references;
- missing/mismatch/relink flows;
- stale relink rejection;
- shutdown behavior.

### Export

- output transactions;
- target identity changes;
- progress/diagnostics;
- persistent logs;
- media probing;
- exact rational rates;
- encoder detection;
- 8/10-bit media profiles;
- HDR classification;
- rotation/SAR handling;
- raster/bitrate math;
- raw-frame transport;
- artifact manifests;
- process shutdown;
- Stage B range mapping;
- VFR->CFR;
- exact frame/packet counts;
- Stage A FFV1 frame identity;
- premultiplied-alpha fixtures;
- Main10 transparent-chroma/color fidelity;
- Canvas readiness.

---

## 50. Visual smoke testing

The project contains deterministic visual acceptance backgrounds:

- `docs/assets/motorsport-broadcast-acceptance.png`;
- `docs/assets/motorsport-broadcast-acceptance-dark.png`.

The application has render modes that capture the production QML smoke composition on both backgrounds.

A command successfully creating the image is not by itself visual acceptance; the image must still be inspected.

---

## 51. Private real-media validation

Private media remains outside Git and may be enabled with:

```bash
FLAPPEDEAR_REAL_GOPRO=/path/to/video.mp4 \
FLAPPEDEAR_REAL_VBO=/path/to/session.vbo \
  ./build-native/native/tests/flappedear_native_tests
```

Known development results include:

- VBO: 32,718 samples, 49 channels, 3,271.7 s;
- GoPro regression: 1,536 GPMF packets, 259,584 KLV headers, 14,796 GPS9 samples;
- sync: +90.217 s at 0.999575 correlation;
- 3840×2160 `60000/1001` 30→90 export: 3,597 video packets;
- 1920×1080 `60000/1001` 30→33 export: 180 packets;
- 1280×720 `30000/1001` 30→35 export: 150 packets;
- HERO11 5312×2988 Main10 native and 4K outputs: 442 packets each;
- short 5.855850 s 4K60 Main10 production-HUD validation: 351 packets, AAC, corrected color and visible Canvas tachometer.

These are specific development fixtures on specific machines. They are never to be described as universal compatibility guarantees.

---

# PART XIV — AUDIT HISTORY AND HARDENING

## 52. Major technical audit checkpoint

A detailed audit was performed around snapshot `6e6019f`.

Contextual audit result at that time:

- overall assessment roughly 7.3/10;
- no P0 finding;
- production posture: **READY WITH BLOCKERS**, not release-ready.

The important findings and present status were:

| Finding | Original concern | Current status/context |
|---|---|---|
| F01 P1 | unbounded JSON/cardinality | fixed (`a871f58`, `2fdf00c`) |
| F02 P1 | unbounded subprocess outputs | fixed/hardened |
| F03 P1 | nondeterministic dependency/runtime contract | partially addressed in dev; FFmpeg 9.0.1 now used locally, packaging/runtime floor still open |
| F04 P1 | release pipeline | deferred |
| F05 P1 | real-media/GPU validation matrix | deferred / partial single-machine evidence only |
| F06 P1 | recovery persistence failure not visible | fixed; degraded recovery protection is surfaced/retried |
| F07 P2 | rotation/SAR behavior not fail-fast | fixed; import/edit allowed, export explicitly rejects unsupported transforms |
| F08 P2 | Save success + recovery cleanup failure semantics | fixed (`4972935`) |
| F09 P2 | widget settings/cues semantic normalization | fixed (`5e14a26`) |
| F10 P2 | cancellation marker failure fallback | fixed (`5e14a26`) |
| F11/F12 | deeper VBO/GPMF streaming rewrites | intentionally not pursued further for now; current bounded implementation considered sufficient |
| F13 | sync performance | deferred unless real evidence requires it |
| F14 | very high raster memory pressure | deferred/runtime capability concern |
| F15 | AppController decomposition | maintainability debt, deferred |
| F16 | sanitizers/fuzzing | deferred |

This audit shifted the project away from endless infrastructure rewriting and toward product work once the most important correctness/safety issues were closed.

---

## 53. Additional hardening commits after the audit

Important sequence:

- `a871f58` — bound external document/process inputs;
- `2fdf00c` — regression coverage for bounds;
- `7d59ac9` — bound encoder discovery output;
- `c3f66ce` — document recovery-deletion residual behavior;
- `cd61410` — restore green export/project-load baseline;
- `f3f0bf7` — restore editor playback/analysis usability;
- `4972935` — invalidate stale recovery after Save;
- `800151f` — durable recovery discard intent;
- `5e14a26` — harden export and widget semantics;
- `f36dfc2` — Main10 color fix;
- `0749eff` — Canvas/offscreen readiness fix.

---

# PART XV — IMPORTANT ENGINEERING INVARIANTS

## 54. Permanent architecture rules

Future work should preserve these unless evidence justifies a deliberate architecture change:

1. Production architecture is Qt 6 + C++20 + QML.
2. Telemetry parsing stays independent from QML/widget rendering.
3. Video/media code stays independent from widget UI.
4. Synchronization stays centralized and time-based.
5. Heart Rate comes from VBO.
6. Never fabricate brake telemetry.
7. Missing telemetry is not zero.
8. Preview and export share `TelemetryScene.qml` / rendering logic.
9. Saved `.fetproject` is clean authority; recovery is unsaved state.
10. Project document validity is independent from external video/VBO availability.
11. Async source/project results require generation/identity guards.
12. Long source/sync operations are cancellable.
13. Imported files/subprocess outputs are resource-bounded.
14. Static track geometry does not rebuild on playback-time updates.
15. FFmpeg never writes directly to the final user target.
16. Final export cadence uses one authoritative exact rational.
17. QRhi Stage A alpha is explicit premultiplied RGBA/BGRA.
18. Main10 Stage B unpremultiplies the authored overlay and uses straight-alpha composition.
19. Offscreen Qt Quick preparation must complete the deterministic Canvas-readiness frame before export frame zero.
20. Raw encoder input uses bounded backpressure and complete-frame submission semantics.
21. Unsupported HDR/Log/rotation/SAR is rejected rather than silently misrepresented.
22. Source raster is data; there is no arbitrary 4K product ceiling.
23. Cloud CI is currently intentionally disabled.
24. Real-media claims are reported separately from synthetic/unit tests.

---

## 55. Development workflow context

Current project-specific workflow decisions retained from development discussions:

- macOS/current primary local tree is the development source of truth;
- Surface Laptop 4 / Windows is validation-only, not a place from which code/commits become authoritative;
- do not make Windows-side commits/patches the source of truth;
- ChatGPT Work/shared-repository sessions push completed commits under the user's standing authorization; locally run Codex does not push unless explicitly asked;
- every completed fix/implementation gets a focused local commit;
- documentation is part of every iteration;
- review README/ROADMAP/AGENTS/relevant docs when behavior changes;
- do not claim behavior from a build alone when the requirement is visual/real-media/platform behavior;
- do not run `qmllint`;
- normal local build/CTest are the required gate;
- cloud CI is intentionally off.

Documentation-only commits do not change product runtime claims or substitute for the local validation gate.

---

# PART XVI — CURRENT LIMITATIONS / NOT RELEASE-READY ITEMS

## 56. Media/export limitations

Still open:

- broader real-media validation matrix;
- broader Windows GPU/encoder coverage;
- heavy 4K Windows GUI-responsiveness validation;
- HDR/HLG/PQ/Log color-managed composition;
- rotation transform preservation;
- non-square SAR display-transform preservation;
- production 8K validation;
- packaging/signing/release gate;
- external FFmpeg dependency remains runtime-installed rather than bundled/managed.

---

## 57. Product limitations

Still open/current roadmap:

- multi-chapter GoPro timelines;
- chart zoom/range selection/annotations/configurable axes;
- interactive map tiles/offline-safe map export behavior;
- deterministic/macOS/private-fixture acceptance of lap timing and comparison widgets;
- end-of-telemetry gate-cluster finalization and first-pass Current presentation;
- macOS signing/notarization;
- Windows signing;
- self-contained installers/update strategy.

The immediate product priority is completing the full macOS workflow and lap-timing acceptance before returning to these broader roadmap items unless a blocker appears.

---

# PART XVII — DEFERRED WIDGET-RUNTIME DESIGN CONTEXT (NOT IMPLEMENTED)

## 58. Why this section exists

Immediately before deciding to return focus to lap timing, a substantial design discussion explored a future clean widget format and separate editor application.

**No production implementation of this design has been authorized or merged at this checkpoint.**

The discussion is preserved here only so the work is not forgotten later.

---

## 59. Future separate application concept

Possible future repository/application:

- `VBOOverlay-Editor`

It would be a separate executable/repository, not a mode embedded into the main application.

Its purpose would be to create reusable telemetry widget packages visually.

The main VBOOverlay application would remain the consumer/runtime.

---

## 60. Approved conceptual packaging direction

The future package name discussed was:

```text
MyWidget.fewidget
```

Conceptually one ZIP-like package containing, for example:

```text
manifest.json
widget.json
assets/
preview.png
```

Graphics are required first-class content:

- PNG;
- JPEG;
- WebP;
- sanitized SVG.

A future custom needle should be able to use an image/SVG with a configurable pivot and telemetry-driven angle.

---

## 61. Approved trust model

The preferred architecture was hybrid:

- **declarative external widgets**: data/assets only;
- **trusted native widgets**: QML/C++ shipped with VBOOverlay for special cases such as track rendering.

External `.fewidget` packages should not contain:

- QML;
- JavaScript;
- executable code;
- dylibs/DLLs;
- scripts;
- arbitrary shader code;
- external filesystem/network dependencies.

A future executable plugin system, if ever wanted, would be a separate trust model.

---

## 62. Future package safety concept

Conceptual import boundary:

```text
.fewidget
-> bounded archive reader
-> archive safety validation
-> schema validation
-> semantic validation
-> asset validation
-> resource-budget validation
-> immutable canonical definition
-> atomic install
-> catalog refresh
```

Threats explicitly discussed included ZIP bombs, traversal, symlinks, duplicate entries, huge decompressed data, malicious SVG, external references, unreasonable image dimensions/pixel counts and unbounded scene complexity.

Again: this is **deferred design context**, not current implemented security behavior.

---

## 63. Future widget version semantics discussed

If/when this work resumes, the agreed product model was:

- widget identity = `widgetId + widgetVersion`;
- a concrete identity/version is immutable;
- new widget instances automatically select the newest compatible installed version;
- existing projects pin the exact version and never auto-upgrade;
- Inspector may expose explicit version selection for an existing instance;
- changing version must never destroy custom settings;
- each instance should keep a bounded per-version settings state so downgrading restores the exact last state used with that version;
- compatible values migrate automatically (approach A);
- future explicit safe migrations can supplement this (approach C);
- external package migrations must not execute arbitrary scripts.

The user explicitly chose deterministic old projects over automatic upgrades.

---

## 64. Future manifest-driven direction discussed

A future cleanup would ideally make built-in/native widgets and external declarative widgets share one metadata contract, with renderer kind differing (`native` vs `declarative`).

The manifest would eventually own identity/version/category/default geometry/inputs/properties/defaults/editor metadata/assets/compatibility.

The motivation was to reduce current duplicated knowledge across:

- `WidgetModel`;
- `Main.qml`;
- `InspectorPanel.qml`;
- `TelemetryScene.qml`;
- `widget-templates.json`;
- individual renderer files.

This is explicitly **backlog**, because undertaking it now would pull development too far away from the next real product goal.

---

# PART XVIII — CURRENT PRODUCT MILESTONE: LAP TIMING VALIDATION

## 65. Current lap-timing status

The private Jastrząb session supplied the required source evidence: RaceChrono Start metadata, regular raw telemetry time, and repeated GPS passages. Production code now parses the source gate, derives passages/laps/traces, publishes lap state to Analysis and the render context, and provides independent lap/speed comparison widgets.

The implementation is not yet accepted as runtime-complete. The user deferred automated and host-runtime tests until the macOS machine and large source material are available.

---

## 66. Implemented lap-timing sequence

### 66.1 Lap detection foundation

- bounded RaceChrono `[laptiming]` Start-gate parsing;
- shared coordinate normalization and local metric projection;
- finite-gate corridor passage clustering;
- speed, normal-motion, direction, gap, re-arm, refractory, and duration filtering;
- complete raw-time laps between accepted same-direction passages.

### 66.2 Session model

`LapSession` now carries the selected source gate, accepted passages, complete laps, fastest-lap index, lap traces, status, and bounded diagnostics. It is derived state and is not persisted in `.fetproject`. Manual gate correction, explicit outlap/inlap state, and session-level distance remain future work.

### 66.3 Lap comparison

The best completed lap is the current reference. Live comparison projects current GPS into a bounded time-local search of that lap trace and publishes elapsed-time delta, current/reference speed, and speed delta. Distance-normalized charts and pedal/G/other channel comparison remain future work.

### 66.4 Sectors — not implemented

Initial approach should prefer explicit/manual sector points over premature automatic sector discovery.

Needed outputs:

- sector times;
- best sector per session;
- sector deltas;
- theoretical best lap.

### 66.5 Overlay integration

Six independent widgets now exist: lap Best/Current/Delta and speed Best/Current/Delta. They share one comparison-tile renderer and remain individually positionable, scalable, rotatable, visible, and configurable. Sector and previous-lap widgets are not implemented.

### 66.6 Analysis integration

Analysis now shows the lap table, fastest state, deltas, and lap-start seeking through the central inverse synchronization transform. Lap-distance channel charts, explicit reference selection, sector summary, and cross-lap channel comparison remain open.

---

## 67. Immediate correctness and validation work

- finalize an active gate cluster at end of telemetry;
- expose Current after the first accepted Start passage;
- add deterministic parser, detector, inverse-sync, controller, and QML coverage;
- validate the expected four passes and three laps against the private Jastrząb fixture;
- run the full macOS build/test/visual-smoke workflow;
- execute the complete video + VBO + sync + analysis + widget + save/reopen + export workflow.

---

# PART XIX — KEY COMMIT / MILESTONE INDEX

## 68. Historical milestone commits

This is not meant to replace `git log`; it is the high-signal development history a future engineer/reviewer should know.

### 2026-09-01 — lap timing, native close dialog, and static-audit fixes

- `a0ebd2a` — parse RaceChrono timing gates;
- `da5e59a` — derive laps from raw telemetry;
- `263fc3b` — publish derived lap timing;
- `a3105a5` — add lap timing to Analysis;
- `be1b0b8` — use the native unsaved-changes dialog;
- `c54193a`, `5354e66`, `28fc913` — lap overlay, live delta, and independent lap/speed tiles;
- `e105167`, `75d4749` — transactional project apply and pre-write persistence validation;
- `8111794`, `eae4f48`, `9ce989e` — canvas geometry, rotated editor controls, and proportional tile scaling;
- `9b3bf0a`, `3be684e` — consistent comparison speed presentation and visible decoder errors.

These commits are published on `main`. Their code state is statically reviewed, but the deferred macOS Qt/runtime/private-fixture pass has not run.

### 2026-08-19 — project and editor foundation

- `d9548bf` — Initial commit
- `ba38360` — build telemetry editor foundation (Electron/React prototype)
- `887de4d` — GoPro telemetry auto sync
- `dc917b0` — preserve playback across resize
- `c2f6c3d` — constrain preview / precise seeking
- `1efda05` — per-widget telemetry channel selection
- `7ffd478` — fullscreen and custom telemetry widgets

### 2026-08-20 — native product and analysis

- `a202e91` — document local commit workflow
- `2106d0d` — remove legacy Electron prototype
- `178b705` — app branding
- `53b7bf5` — configurable logo widget
- `6827484` — synchronized telemetry analysis
- `80ea7cc` — chart refresh/layout improvements
- `779ce23` — detached/resizable analysis panes
- `edf4785` — fullscreen restore fix

### 2026-08-21 — export architecture/correctness

- `f5ef33c` — introduce TelemetryRenderContext
- `565c68e` — MediaProbe + HEVC capability detection
- `76928b4` — separate telemetry scene from editor interaction
- `99988fd` — first HEVC overlay export
- `8362afa` — user-facing HEVC export workflow
- `1f952e6` — timestamped export ranges
- `7d89f7d` — export progress reporting
- `58cfd3a` — replace `grabWindow` with RHI offscreen renderer
- `ca84d45` — bound FFmpeg overlay buffering
- `74bcf96` — staged frame-correct telemetry overlay export
- `c54f735` — staged range timeline
- `9cbbd4f` — final-media validation hardening
- `675ff24` — live export diagnostics
- `eec5e2c` — crash-safe export target transaction
- `11a7aea` — atomic project saving
- `cd83002` — dirty-project safeguards
- `438f8b9` — transactional project source loading
- `71773fb` — monotonic/rollover-safe VBO timestamps
- `8460f5d` — explicit missing-data semantics
- `fa040c6` — deterministic CFR export timing
- `ac2c177` — staged cadence validation without full decode
- `6008d4b` — export artifact lifecycle hardening
- `c380e7d` — measured FFV1 staging estimates

### 2026-08-22 — product hardening and diagnostics

- `4d4607a` — persistent application log
- `5eac041` — dirty-project dialog fix
- `73cd4a3` — clear startup QML runtime warnings
- `5b54134` — remove obsolete widget-overlay renderer path
- `5786959` — one renderer per widget
- `5919bec` — consistent telemetry scene scale
- `443a1b5` — lazy-load detached analysis video
- `8dd49db` — explicit export format settings
- `278f28b` — persistent per-export diagnostic logs
- `2860dff` — export settings/bitrate policy
- `fb1842b` — cross-platform export reliability hardening
- `465a37c` — separate recovery from saved document state

### 2026-08-23 — project portability and source-driven export

- `9eae1f4` — bounded/cancellable telemetry source loading
- `6a4df5e` — stabilize presentation/analysis semantics
- `7afa18b` — bind overwrite consent to target identity/state
- `76645da` — static track rendering cache
- `6cab87c` — portable media relinking
- `bc76e3c` — UAT regression fixes
- `8f2330c` — source-driven media/export characteristics

### 2026-08-24 — real media, alpha, widgets and HUD

- `274ce5b` — real HERO11 media validation
- `e9228d` — premultiplied overlay composition fix
- `6dd33d4` — telemetry cadence cache
- `2d15655` — ranged-source seek fix
- `506e013` — widget/template editing improvements
- `83e3319` — expanded widget customization
- `2617ced` — F1 radar styling
- `b3976a8` — template picker selection state
- `52c6ed2` — motorsport broadcast telemetry HUD
- `9774db8` — unified broadcast styling
- `ad90855` — HUD acceptance mockup alignment

### 2026-08-25 — HUD polish, audit hardening and recovery

- `2646717` — broadcast proportions
- `4401e3d` — final broadcast composition
- `21f559d` — tach/radar polish
- `bf9abb2` — tachometer dial rebuild
- `0cfe3c9` — RPM/temp icon refinement
- `a90d8c6` — tach scale/temp symbols
- `deaad4e` — external temperature icons
- `89258d3` — tach redline/icons
- `6e6019f` — embed supplied temperature icons
- `a871f58` — bounded external documents/process inputs
- `2fdf00c` — bounded-input regression tests
- `7d59ac9` — encoder discovery output bound
- `c3f66ce` — recovery deletion residual docs
- `cd61410` — restore green export/project-load baseline
- `f3f0bf7` — restore playback/analysis usability
- `4972935` — stale recovery invalidation after Save
- `800151f` — durable recovery discard intent
- `5e14a26` — harden export and widget semantics

### 2026-08-27 — final current export blockers fixed

- `f36dfc2` — preserve Main10 colors through VideoToolbox export
- `0749eff` — preserve Canvas widgets in offscreen export

---

# PART XX — WHAT NOT TO DO NEXT

## 69. Avoid these distractions before the full macOS workflow passes unless a blocker appears

Do not immediately:

- rewrite the widget system;
- build `VBOOverlay-Editor`;
- remove old widgets/templates just because cleanup looks attractive;
- decompose `AppController` without a concrete feature/correctness reason;
- rewrite bounded VBO/GPMF ingestion again without evidence;
- implement a general plugin system;
- chase 8K/HDR before the product needs it;
- reactivate cloud CI;
- change the working two-stage export architecture casually;
- replace deterministic Canvas readiness with sleeps;
- treat Windows validation as authoritative development state;
- run `qmllint`.

The project has reached a point where **product capability should now lead architecture work**.

---

# PART XXI — HANDOFF STATE

## 70. Exact current checkpoint

At product-code baseline `3be684ee45b8c4b9c074f58ac1abc7bc7af0e8fc`:

### Present in the current codebase

- native Qt editor;
- VBO parsing;
- GoPro GPS metadata extraction;
- GPS-speed auto-sync;
- synchronized preview;
- current HUD editing;
- project save/recovery/relinking;
- Analysis workspace;
- exact-CFR staged HEVC/AAC export;
- 8-bit SDR export policy;
- 10-bit SDR Main10 export policy on validated macOS path;
- corrected premultiplied/straight alpha behavior;
- corrected QML Canvas offscreen readiness;
- persistent diagnostics/logging;
- bounded external inputs;
- current manual application export with the accepted HUD;
- source-defined timing-gate and derived `LapSession` implementation;
- Analysis lap table and central lap-start seek;
- live best-lap comparison and six independent lap/speed tiles;
- native macOS unsaved-changes dialog;
- write-side project/recovery validation and guarded project-source startup;
- bounded widget geometry and rotation-matched editor interaction;
- visible main/Analysis decoder failures.

### Explicitly not complete

- production/release packaging;
- broad Windows validation;
- broad real-media/GPU matrix;
- HDR/Log compositor;
- rotation/SAR export transforms;
- multi-chapter GoPro timeline;
- interactive maps;
- advanced analysis tools;
- runtime acceptance of the September lap/session and comparison-widget implementation;
- deterministic lap parser/detector/controller/QML coverage;
- final-passage handling when telemetry ends inside the gate corridor;
- Current presentation before the first completed lap;
- future widget package/editor architecture.

### Immediate next action

On the macOS development machine, add the deferred deterministic coverage, fix the two known lap-state edge cases, then run the private Jastrząb fixture and the complete product workflow. Do not claim the new lap features work at runtime before that gate passes.

---

## 71. Short context block for a future assistant/Codex session

If a future development session starts with only this file, the minimum correct mental model is:

> VBOOverlay is FlappedEar Telemetry, a native Qt 6/C++20/QML motorsport telemetry editor. Preview/export share `TelemetryScene.qml`. Telemetry is time-based, missing != zero, brake is never fabricated, and HR comes from VBO. Projects v2 are atomic and portable; recovery is separate unsaved state. Export is a staged QRhi -> premultiplied FFV1/BGRA -> FFmpeg HEVC/AAC pipeline with exact rational CFR. Main10 requires explicit overlay unpremultiplication/straight-alpha YUV10 composition. The current code baseline is `3be684e`. Source Start gates, derived laps/traces, Analysis lap seeking, live best-lap comparison, and six independent lap/speed tiles are implemented but not yet runtime-validated. Two known lap edge cases remain: telemetry-end cluster finalization and Current before a completed lap. The `.fewidget` runtime remains deferred. macOS is the primary validation environment; Windows coverage is limited; cloud CI is disabled. ChatGPT Work publishes completed commits under the user's standing authorization, while locally run Codex does not push without an explicit request. Never claim validation that did not run.

---

## 72. Relationship to other documentation

This file is intentionally broad and historical. Detailed normative behavior remains documented in:

- `README.md` — current user/developer overview;
- `ROADMAP.md` — remaining work, not changelog;
- `AGENTS.md` — durable engineering invariants;
- `docs/architecture.md` — current module architecture;
- `docs/project-format.md` — project/source/recovery format contract;
- `docs/telemetry-semantics.md` — telemetry/no-data contract;
- `docs/export-pipeline.md` — detailed export/timing/alpha pipeline;
- `docs/media-color-policy.md` — raster/bit-depth/color policy;
- `docs/export-output-safety.md` — destination transaction guarantees;
- `docs/testing.md` — validation and fixture coverage.

If this checkpoint conflicts with code, **code at the named baseline wins**. If it conflicts with a newer commit, update this file rather than treating the old checkpoint as a frozen specification.
