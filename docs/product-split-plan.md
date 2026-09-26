# Product split plan: Flapped Ear Telemetry and Flapped Ear Overlays

Proposed 26 September 2026 on the owner's request. **Status: proposal.**
Jira: epic [KAN-121], tickets [KAN-122]–[KAN-132].
Nothing here is implemented yet. Decisions marked **Owner decision** must be
made before the phase that depends on them.

## Why

At the track nobody carries a laptop. Between sessions a driver needs the
day's analysis on a phone or tablet: where the best lap loses time, which
sections improved, what to change on the next run. RaceChrono already
records on that phone. Video-overlay work (GoPro, helmet camera, FFmpeg
export) is desktop work done after the day. One desktop application serves
neither case well, so the product becomes two, kept in **one repository**:

| | **Flapped Ear Telemetry** | **Flapped Ear Overlays** |
| --- | --- | --- |
| Platforms | iPhone, iPad, Android phones and tablets; a macOS build for development and desktop use | macOS desktop |
| Job | Import the day's VBO/RCZ files from RaceChrono via the share sheet, then show laps, best lap vs theoretical best on the map, time losses, section progression and the Corner Analyzer, to prepare the next run | Video + telemetry overlay editing and export, GoPro auto-sync, multiple video sources with manual sync, tyre pressure/temperature widgets |
| Video | None | Required |
| Works offline at the track | Yes | Not needed |

## What the code looks like today

These findings come from a dependency survey of the codebase on
26 September 2026:

- **`native/src/telemetry/` is already clean.** It has no includes from
  export, GoPro, sync, widgets or app code, and no QProcess, Multimedia,
  QRhi or Widgets; it uses only zlib for RCZ. Every parser, lap, segment and
  metric the mobile app needs lives here. The one video-flavoured concept is
  `SyncTransform` (pure maths) in `TelemetrySession.h`.
- **`native/src/project/` has one bad edge.** `ProjectSourceReference.h`
  includes `export/MediaProbe.h` for `videoFingerprint`, so every project
  consumer pulls in export. Separately, `ProjectLimits::validateProject`
  requires `scene.widgets`, so an analysis-only document cannot exist today.
- **`flappedear_core` mixes both products.** Telemetry and project code sit
  next to export (19 files, FFmpeg via `QProcess`, QRhi in
  `TelemetryFrameRenderer`), GoPro, sync, widgets and three app files
  (`PreviewPlayback`, `GuiSessionLock`, `AppLog`). It links `Qt6::Gui`,
  `Quick` and `GuiPrivate`.
- **`AppController` is the knot.** It is about 8,000 lines, has about 130
  properties and is one class for both products:
  - about 1,150 lines of overlay, video and export code;
  - about 1,300 lines of shared document, source and recovery code;
  - the analysis code (Import, Outing, Comparison, SegmentReview,
    CornerAnalyzer, TheoreticalBest).

  Analysis is coupled to video in four places:
  - KAN-39 lap video (`outingLapVideoAvailable`,
    `followOutingLapVideoPosition`);
  - `openComparisonLapAtProgress`;
  - the "active run" model, which loads one run's telemetry *and* video into
    the editor projection;
  - `exporting()` guards spread through the analysis files.
- **QML:** about 12,500 lines. The overlay editor is `Main.qml`,
  `InspectorPanel.qml`, the widgets and the scene. The analysis lives in the
  separate `AnalysisWindow` and about 17 panels and dialogs. Several of them
  rely on hover and tooltips (19 in `OutingLapPanel.qml`), Escape shortcuts
  and minimum desktop sizes, which a touch UI cannot use.
- **Tests:** `TelemetryTests.cpp` (12,500 lines) recompiles all of
  `AppController`; the 20+ pure core test targets already link only the
  core.

## Target architecture (one repo)

```
native/
  telemetry-core/   flappedear_telemetry_core  Qt Core + zlib only
                    parsers, lap timing, compatibility, segments, sector timing,
                    theoretical best, time loss, consistency, variability, G-G,
                    the event document (analysis fields), recovery store
  overlay-core/     flappedear_overlay_core    desktop only
                    export (FFmpeg, QRhi renderer), GoPro, sync engine, widgets,
                    render context, video fingerprints
  app-shared/       DocumentController (project, sources, recovery),
                    AnalysisController (day import, laps, comparison, segments,
                    theoretical best, losses, consistency) - QObject, no video
  apps/telemetry/   Flapped Ear Telemetry: touch-first QML; iOS, Android, macOS
  apps/overlays/    Flapped Ear Overlays: the current editor; macOS
```

Moving directories is optional and can come last. The **CMake targets and
the dependency rule** are what matter: nothing under telemetry-core or
app-shared may include overlay-core, Multimedia, QRhi or QProcess. CI
enforces this by building telemetry-core against Qt Core only.

The analysis-to-video coupling becomes an optional interface: `VideoLink`,
implemented only by Overlays. The Telemetry app never has video. On the
desktop Overlays app, the current "Lap A here…" and KAN-39 video behaviour
keep working through that interface.

### Documents

Keep **one `.fetproject` schema** (v3). The survey found its fields already
fall into analysis-only, overlay-only and shared groups:

- **Analysis-only:** lap exclusions, analysis decisions, track
  configurations, segments, reviews, run notes, conditions and setup.
- **Overlay-only:** scene and widgets, export and map settings, per-run
  video and sync.
- **Shared:** document identity, runs, telemetry source references.

Changes:

- `scene.widgets` becomes optional, so an analysis-only document is valid.
  The Telemetry app writes documents without the overlay fields.
- Overlays keeps writing the full document, and can open a Telemetry day
  document to reuse its approved segments, lap names and exclusions, adding
  video on top. A day analysed on the phone can then be turned into a video
  at home.
- On mobile, imported recordings are copied into the app sandbox and
  referenced relatively. Content fingerprints are unchanged, so a document
  moved between devices still verifies its sources.

## Mobile specifics (Telemetry)

- **Import:** the iOS share sheet / "Open in", and Android `ACTION_SEND` /
  `ACTION_VIEW` intents for `.vbo` and `.rcz` exported from RaceChrono on
  the same phone. Files are copied into the sandbox. There is no file dialog
  and no absolute paths.
- **UI:** touch-first, portrait phone and landscape tablet, no hover or
  tooltips, large targets. First screens:
  1. **Day:** sessions ("Session 1, 2…") and laps.
  2. **Where your best lap can improve:** the KAN-120 map, already
     driver-first.
  3. **Time losses.**
  4. **Sections by session:** the KAN-64 matrix.
  5. **Lap / compare:** Corner Analyzer, charts, G-G.

  The C++ controllers are reused; the QML is new. Desktop windows are not
  ported one-to-one.
- **Performance:** parsing and the theoretical best already run in
  background workers and handle one recording at a time. The 256 MiB
  session-cache budget should become platform-configurable (lower on phones)
  and be measured on the owner's own phone with a full day (6 files, about
  6–7 MB each).
- **Storage:** `AppLocalDataLocation` and `QSettings` work on both
  platforms. The desktop-only `GuiSessionLock` stays out of the mobile app.

## Overlays roadmap (owner requests, 26 September 2026)

- **Multiple video sources with manual sync:** GoPro keeps GPS auto-sync. A
  helmet camera without GPS is aligned manually (offset and scale per
  source, like today's single-video sync), with a visual cue to line up
  (e.g. a flash or a gate crossing). Export composes the sources.
- **Tyre pressure and temperature** as overlay data and widgets. **Owner
  decision:** which device records them (RaceChrono external TPMS channels
  in the VBO/RCZ, or a separate logger file to import and sync). KAN-67 and
  KAN-68 (recorded temperatures) cover the analysis side.

## Owner decisions

1. **Qt licence for app-store distribution.** Qt for iOS links statically,
   and LGPL obligations inside the App Store need care: either a commercial
   or small-business Qt licence, or LGPL compliance with relinkable object
   files. This must be verified with Qt before any store release. It does
   not block development, the simulator or TestFlight-style internal
   testing.
2. **Product direction documents.** `docs/product-vision.md` currently says
   "one macOS-first application", "no second application", and
   `AGENTS.md` says macOS only. The owner updates these, or asks an agent
   to draft the change for approval.
3. **Names and identities.** Today's desktop bundle is named "Flapped Ear
   Telemetry" (`com.flappedear.telemetry`) but *is* the overlay editor. The
   proposal:
   - Overlays becomes `com.flappedear.overlays`, with a one-time migration
     of settings, templates and recovery storage.
   - Telemetry takes `com.flappedear.telemetry`.
4. **Does Overlays keep a day-analysis window?** The recommendation is to
   keep only what export needs (lap and range selection, sync) and move the
   full analysis to Telemetry, which also has a macOS build.
5. **Minimum OS versions and target devices,** starting with the owner's
   own phone and tablet.

## Phases

Each phase ends with every existing test green. Phases 1–3 change no user
behaviour and can proceed before decisions 1 and 5.

| Phase | Outcome | Size |
| --- | --- | --- |
| 0. Decisions ([KAN-122]) | Items 1–5 above answered | owner |
| 1. Core separation ([KAN-123]) | `flappedear_telemetry_core` builds with Qt Core + zlib only. Cuts: `videoFingerprint` out of project code; `scene.widgets` optional; `PreviewPlayback`, `GuiSessionLock`, `AppLog` out of core; overlay code moved to `flappedear_overlay_core`; a CI job enforces the rule | M |
| 2. Controller split ([KAN-124]) | `DocumentController`, `AnalysisController` (no video, `VideoLink` interface) and `OverlayController` extracted from `AppController` behind the same QML-facing API; `TelemetryTests.cpp` split to match | L (largest risk) |
| 3. Two desktop apps ([KAN-125]) | Flapped Ear Overlays (the editor; new identity with migration) and Flapped Ear Telemetry for macOS (analysis UI), both built and tested in CI | M |
| 4. Telemetry on phones and tablets ([KAN-126]–[KAN-129]) | iOS and Android targets, share-sheet/intent import into the sandbox, touch-first screens, CI builds for the iOS simulator and Android | L |
| 5. On-track acceptance ([KAN-130]) | The owner uses the app on their phone at a real track day; internal test distribution | owner |
| Overlays roadmap ([KAN-131], [KAN-132]) | Multiple video sources with manual sync; tyre pressure/temperature widgets | M each, after Phase 3 |

## Effect on the current backlog

- The M4 **core** work (time losses, consistency, variability, G-G pairs,
  thermal and heart-rate summaries, the day-report model) belongs in
  telemetry-core and is unaffected. Continue it.
- M4 **UI** tickets should be designed for the Telemetry app's touch UI
  once Phase 2 lands. Until then, desktop UI stays small and reuses
  Canvas-based QML, which also runs on mobile.
- KAN-66 (A/B G-G scatter) is half done (pairs, peaks, peak-preserving
  decimation). Finish it as a reusable panel.
- M5 "physical Mac acceptance" splits into on-device Telemetry acceptance
  and Mac acceptance for Overlays.

## Risks

- **`AppController` split regressions.** Mitigation: behaviour-preserving
  steps with the existing 400+ native tests and the private real-day check
  run before and after each step.
- **Licensing** for store release (decision 1).
- **Phone memory and performance** with a full day. Measure on the owner's
  device, not only in the simulator.
- **RaceChrono export on the phone.** Confirm which formats the owner's
  RaceChrono version exports on iOS and Android, and that the share sheet
  hands the files over intact.
- **Settings and recovery migration** when the desktop identity changes.
- **Test harness:** the native tests assume `QT_QPA_PLATFORM=cocoa`, so
  mobile needs its own smoke runs (simulator/emulator).

[KAN-121]: https://kozucharkadiusz.atlassian.net/browse/KAN-121
[KAN-122]: https://kozucharkadiusz.atlassian.net/browse/KAN-122
[KAN-123]: https://kozucharkadiusz.atlassian.net/browse/KAN-123
[KAN-124]: https://kozucharkadiusz.atlassian.net/browse/KAN-124
[KAN-125]: https://kozucharkadiusz.atlassian.net/browse/KAN-125
[KAN-126]: https://kozucharkadiusz.atlassian.net/browse/KAN-126
[KAN-127]: https://kozucharkadiusz.atlassian.net/browse/KAN-127
[KAN-128]: https://kozucharkadiusz.atlassian.net/browse/KAN-128
[KAN-129]: https://kozucharkadiusz.atlassian.net/browse/KAN-129
[KAN-130]: https://kozucharkadiusz.atlassian.net/browse/KAN-130
[KAN-131]: https://kozucharkadiusz.atlassian.net/browse/KAN-131
[KAN-132]: https://kozucharkadiusz.atlassian.net/browse/KAN-132
