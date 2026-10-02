# Flapped Ear Telemetry

Flapped Ear Telemetry is one native desktop application for video telemetry overlay editing/generation and motorsport telemetry analysis.

## Status

Version 0.2.0, in development; no release is approved. The overlay editor and export pipeline work, and the track-day analysis implements milestones M0–M4: whole-day import, compatible rankings, independent A/B lap comparison, reviewed segments and the Corner Analyzer, sector theoretical best, ranked time losses, consistency, G-G, temperature and heart-rate summaries, and the day report. These are validated by synthetic CI tests and one private real day without video; the owner's real-video acceptance (M5) is still open.

The [product vision](docs/product-vision.md) defines the complete intended scope. The [delivery ledger](docs/product-delivery.md) is the single current status record: what is implemented, what remains, and the acceptance evidence. Distribution still requires the exact candidate's [acceptance evidence](docs/beta-acceptance.md).

## User guide

End-user documentation is published at <https://arekkozuch.github.io/VBOOverlay/>.
Its sources live in [docs/user-guide](docs/user-guide/README.md).

## Current capabilities

Saved events use **Event → Run → Lap**, with independent source references and
video synchronization per run. Analysis imports a whole day (files, folders or
drag and drop) and lists each outing's OUT/LAP/IN laps chronologically when
recording timestamps are available. Opening a lap shows its map and telemetry
with a shared cursor, without requiring video. From there it compares two laps
from compatible runs, reviews automatic straight and corner segments, and links
ranked time losses to the Corner Analyzer and the day report. The
[user guide](https://arekkozuch.github.io/VBOOverlay/) describes every screen.

- MP4/MOV playback with timeline controls and preview overlays.
- RaceChrono and VBOX VBO telemetry import, plus native single-session RaceChrono RCZ import ([supported format](docs/rcz-format.md)).
- Multi-file outing import and advanced import review, duplicate/error reporting, persisted source groups, and event save/reopen. The outing workflow selects VBO for unique RCZ/VBO matches supported by recording-date/time and GPS evidence; ambiguous sources stay separate. Grouping retains alternatives without combining channels.
- Chronological outing laps and interactive single-lap speed/G/channel detail with a synchronized map cursor; missing timestamps and source failures remain visible.
- Independent A/B lap comparison on a shared track-progress axis: delta time, channels, map layers, G-G and optional side-by-side video from each run's own footage.
- Automatic segment proposals with review and editing, Corner Analyzer metrics, sector theoretical best, ranked time losses, consistency, temperature and heart-rate summaries, and a day report with links to its evidence.
- GoPro chapter groups reviewed before loading and played as one continuous timeline. Exporting a chaptered video is not available yet (KAN-106).
- GoPro GPMF GPS extraction and GPS-speed auto synchronization.
- A visual widget editor, projects, built-in layouts, and shareable templates. Both editor sidebars remain fully scrollable at the supported 1180×720 minimum size.
- Lazily loaded synchronized telemetry analysis, including charts and a track view; its secondary decoder exists only while the Analysis window is open.
- Source-defined RaceChrono Start-gate parsing, raw-GPS lap derivation, fastest-lap state, and Analysis navigation for Out lap, each measured lap, and In lap.
- Independent Best, Current, and Delta tiles for lap time and speed comparison against the best completed lap.
- Source-driven CFR HEVC/AAC MP4 export at the effective rational export rate, including runtime raster/profile checks, validated 8-bit and 10-bit SDR preservation, custom SMPTE ranges, and single-lap hotlap ranges with configurable 5–8 second handles, progress, cancellation, and verbose diagnostics retained in a durable per-export log.
- Portable `.fetproject` media references with project-relative lookup, bounded source fingerprints, missing-media recovery, explicit relinking, and stale asynchronous-result rejection.
- Crash-safe export-output handling with state-bound overwrite consent, atomic project saving, and explicit unsaved-change recovery.
- Resource-bounded external JSON documents and subprocess output, with visible recovery-protection warnings when automatic snapshots cannot be persisted.

Lap analysis lets you add up to four recorded channels, change a channel using
its row selector, and remove it with ×. Selections are remembered as analysis
preferences. Channel selectors use a wider label column and a dropdown at least
320 px wide when the window allows; long names wrap instead of being shortened.
Dropdowns stay within the window. Longitudinal-G graphs show braking upward while retaining the
original signed values.

## Keyboard controls

Space plays or pauses. Left/Right seek five seconds; Shift+Left/Right seek thirty seconds; Home/End seek to the first/last actual video frame. These playback shortcuts are disabled while typing or operating a focused editor control. Ctrl/Cmd+E opens Export and Ctrl/Cmd+Shift+A toggles Lap Analysis. The platform full-screen shortcut (⌃⌘F on macOS) enters full screen and Escape leaves it; the F1 help still lists F11 (KAN-140). Full-screen preview provides the same visible transport and scrubber as the editor.

Very Verbose export diagnostics follow the live tail until the user scrolls into history. Historical inspection stays fixed while new lines arrive; **Jump to latest** explicitly resumes following.

## Requirements

- CMake 3.24 or newer.
- A C++20 compiler.
- Qt 6.8 or newer with Concurrent, Core, Gui and matching GuiPrivate headers, Qml, Quick, Quick Controls 2, Multimedia, and Test; include the SVG and Shader Tools modules in binary SDK installations.
- FFmpeg and ffprobe available at runtime. Export depends on an externally installed FFmpeg and a working HEVC encoder; the command-line tools are not bundled. A small production-filter preflight also requires explicit alpha-mode support.

Development targets macOS, with coordinator implementation and local Codex handoffs as requested. Cloud CI was resumed by owner direction on 13 September 2026 for macOS Debug and Release; Windows builds and validation remain paused. See [task delivery](docs/development-workflow.md) for local implementation and validation instructions. Existing [Windows NSIS packaging](docs/windows-installer.md) is retained for later resumption.

For replacing an older named bundle without losing preferences or recovery, see
[application identity and upgrades](docs/application-identity.md).

## Build

For per-task local Codex instructions and the update/build/test
handoff, see [task delivery and local acceptance](docs/development-workflow.md).

On Apple Silicon with Homebrew Qt in `/opt/homebrew/opt/qt`:

```bash
cmake -S . -B build-native \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
open "build-native/native/Flapped Ear Telemetry.app"
```

## Continuous integration

[Native CI](.github/workflows/build.yml) runs on pull requests, pushes to `main`, and manual dispatch. It builds and tests macOS Debug and Release; Release also validates deployed startup and produces an internal candidate. Windows remains paused. Keep local builds and CTest, and report hardware-encoder and private-media acceptance separately. See [CI guidance](docs/testing.md#cloud-ci).

## Architecture

The application keeps telemetry parsing, synchronization, video/media handling, widgets, and QML presentation separate. Preview and export mount the same `TelemetryScene.qml` with independent render contexts. Track geometry is converted and painted as a static layer when its source or appearance changes; playback updates move only the independent position marker.

## Widget editing

G-Force widgets can invert lateral and longitudinal presentation axes independently, without changing imported telemetry. Alongside the classic target, the widget catalog includes an **F1 G-Force Radar** with a dark semi-transparent circular field, 1.5 g range, and six 0.25 g concentric-ring levels by default, plus a compact **G-Force Bar** that shows true combined magnitude while clamping only its fill. Speed, pedals, Heart Rate, Retro Custom, and G-Force Bar use the same translucent charcoal broadcast panel, typography, rounded border, and neutral bar-track treatment; throttle is green, brake is red only when it has fill, and G-Force is amber. The analog retro tachometer keeps its circular identity with a matching rounded dark RPM plate. `Retro Custom` accepts arbitrary channel, adjustment, formatting, unit, fallback, and palette settings. A **Tyres** widget shows each corner's recorded tyre temperature (°C) and pressure (bar or psi), with a dash where a corner has no data. Text-bearing value widgets offer **Font size**: `Auto` retains their responsive legacy sizing, while a positive canonical size is scene-scaled consistently for preview and every export resolution.

Template writes validate structure, count and serialized size before atomic replacement; a rejected store remains protected until successfully reloaded. Templates have two explicit operations. **Save current** updates the custom template that was applied (or just created), preserving its ID, name, and description. Selecting a template alone does not make it editable, so Save current opens **Save as new** until that custom template is applied. Built-in templates are immutable and always use Save as new.

Lap and speed comparison tiles use the same compact translucent panel family. Each Best, Current, and Delta tile is an independent widget with its own position, scale, rotation, visibility, and appearance. Scaling a comparison tile scales its typography, margins, gauge strokes, border, and corner radius together. Editor interaction geometry follows widget rotation, and normalized size/scale changes keep the unrotated widget rectangle inside the canvas.

A single GUI editor owns the shared application-data directory through a process lock; export workers remain independent. A saved `.fetproject` is the authoritative clean document. It can open without its external video or VBO/RCZ assets; missing or mismatched sources remain independently relinkable without losing the scene or settings. Unsaved persistent edits are held separately in an atomic recovery snapshot and are recovered or discarded explicitly at startup only when their logical document state is newer than the saved authority. Before Quit, New, or Open can discard current edits, the editor presents a branded Save / Discard / Cancel confirmation. A leftover snapshot after successful Save is cleanup debt, not degraded recovery protection or a user warning. QSettings stores only application preferences and the last project path. The portable source format is documented in [docs/project-format.md](docs/project-format.md).

External JSON documents are size- and structure-bounded before they can create editor models. FFprobe payloads, FFmpeg diagnostics, progress lines, and export-worker messages are bounded as well; diagnostic tails retain the newest useful output. If automatic recovery storage fails, the editor shows a persistent manual-save warning and retries safely after a backoff. A requested recovery discard is durable before cleanup: a matching residual snapshot is suppressed at the next startup, while newer or different-document recovery remains available.

Module details are in [docs/architecture.md](docs/architecture.md). The project source lives in [`native/src/project`](native/src/project).

## Telemetry semantics

Telemetry has strict no-data semantics: missing is distinct from a measured numeric zero, public raw lookup never returns `NaN` or infinity, and raw analysis does not bridge missing samples or real timestamp gaps. Preview/export overlays use a separate bounded hold and lightweight smoothing policy to remain stable between ordinary samples without changing raw telemetry. See [docs/telemetry-semantics.md](docs/telemetry-semantics.md).

## Export

Export stages a frame-cadenced telemetry overlay, converts the source onto that same CFR cadence before composition, and validates the staged overlay and final MP4 before committing the target file. Scheduling is integer frame-addressed: full-video ranges use the actual source frame domain, and C++ owns inclusive SMPTE NDF IN/OUT parsing. Qt Quick offscreen pixels retain an explicit premultiplied-alpha contract through QRhi readback and FFV1 staging. Before 10-bit YUV composition, Stage B explicitly unpremultiplies that overlay and uses straight-alpha blending so transparent RGB-to-YUV chroma offsets cannot alter the source. Source raster and bit depth remain authoritative rather than being reduced to presets; HDR/Log, non-zero rotation metadata, and non-square sample-aspect-ratio sources are explicitly rejected only at export preflight until their display transforms can be preserved. Import, probe, playback, and editing remain available. Each prepared export writes a separate support log in the app-data `exports` directory; failures and cancellations keep their logs. See [docs/export-pipeline.md](docs/export-pipeline.md), [docs/media-color-policy.md](docs/media-color-policy.md), and [docs/export-output-safety.md](docs/export-output-safety.md).

## Private integration tests

Private recordings are ignored by Git and can be supplied through environment variables:

```bash
FLAPPEDEAR_REAL_GOPRO=/path/to/video.mp4 \
FLAPPEDEAR_REAL_VBO=/path/to/session.vbo \
  ./build-native/native/tests/flappedear_native_tests
```

A private RaceChrono fixture has been validated with 32,718 samples, 49 channels, 3,271.7 seconds, and zero parser warnings. An August 2026 macOS regression run on one private GoPro/VBO pair restored 1,536-packet / 14,796-sample GPS extraction and +90.217 s auto-sync at 0.999575 correlation. The exact 3840×2160, `60000/1001`, 30→90 HEVC/AAC export also passed with 3,597 final video packets, alongside short 1080p59.94 and 720p29.97 checks. These are development results, not cross-platform performance guarantees.

## Current limitations

- Real-media coverage is limited: one private track day without video plus earlier GoPro/VBO checks. The owner's full-day walkthrough with matching video (M5) is open.
- Defects found by the independent audit of 2 October 2026 are tracked in Jira epic KAN-144; open technical items are listed in the [delivery ledger](docs/product-delivery.md#open-technical-items).
- Internal candidate packaging is automated; clean-machine acceptance, distribution notices and Developer ID signing/notarization remain pending.
- Windows builds and validation are paused. Earlier results covered one Windows 11 / Qt 6.11 / MSVC 2022 / Intel Iris Plus / Quick Sync configuration.
- Chaptered GoPro videos play as one timeline, but exporting them is refused until KAN-106; auto-sync reads GPS from the first chapter only.
- Export requires compatible external FFmpeg at runtime, including the production overlay filters; the application fails early when these are absent.
- Identified RaceChrono VBO versions other than Pro 10.2.4 retain telemetry but omit unverified timing gates with a warning.
- VBO channel units are not stored, so VBO values are shown without units; RCZ units are kept.
- GPS-incomplete laps retain their measured timings but are excluded from spatial references and best-lap ranking, with explicit quality/no-delta states. A/B comparison and rankings require compatible runs (layout, direction and timing gate).
- Rotation and sample-aspect-ratio display-transform preservation, HDR/Log color-managed preservation, and production 8K validation remain pending. One real HERO11 5312×2988 10-bit SDR fixture has passed native and 3840×2160 macOS exports; this is not a broader hardware guarantee.
- Interactive map tiles are pending; the local GPS track view works without map tiles.
- Lap timing requires exactly one valid RaceChrono Start gate and usable synchronized GPS; a manual Start/Finish override is not implemented.

The end-user list of limitations is in the [user guide](https://arekkozuch.github.io/VBOOverlay/limitations.html). For remaining work, see the [delivery ledger](docs/product-delivery.md). Developer contribution rules are in [AGENTS.md](AGENTS.md), and local test guidance is in [docs/testing.md](docs/testing.md).

### Editor instance and recovery

Run one Flapped Ear Telemetry editor per user data directory. A second launch asks you to use
or close the existing window, protecting unsaved recovery and custom templates.
Export workers are unaffected. Close older builds before opening this version.
