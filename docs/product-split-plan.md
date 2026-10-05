# Product split plan: FlappedEar Telemetry and FlappedEar Overlays

Proposed 26 September 2026 on the owner's request. **Status (2 October 2026):
phases 1 and 2 are implemented** (KAN-123 and KAN-124, closed 27 September), and
the tyre widget (KAN-132) is merged. **Owner decision (2 October 2026):** two
applications, the desktop overlay editor for macOS and Windows and FlappedEar
Telemetry for macOS, Windows, iOS and Android. Decisions 3 (identities) and 4
(the desktop analysis window) followed the same day, and decision 5 for mobile
(iOS 15 and Android 8.0) was answered; the store licence (KAN-122) is open. **Owner direction (2 October 2026, later the same
day): separate repositories.** FlappedEar Overlays is finished first as a
standalone app in this repository; FlappedEar Telemetry is then built in a new
repository. Overlays stays as it is, with nothing removed, until Telemetry is
mature enough to replace its analysis ([KAN-166], deferred), and both apps keep
one compatible `.fetproject` format ([KAN-170]). See
[Stages (current plan)](#stages-current-plan). **Owner decision (2 October
2026): FlappedEar Telemetry is a new Flutter app**, started from a blank page in
its own repository and Jira project and sharing no code with this repository
(decision 6, [KAN-167]). Its architect starts from the
[handover](telemetry-handover.md) ([KAN-168]).
The code survey below describes the code before phase 1.
Jira: epic [KAN-165] (current plan). It supersedes [KAN-121], which delivered
phases 1–2 and [KAN-132].
Decisions marked **Owner decision** must be made before the phase that depends
on them.

## Why

At the track nobody carries a laptop. Between sessions a driver needs the
day's analysis on a phone or tablet: where the best lap loses time, which
sections improved, what to change on the next run. RaceChrono already
records on that phone. Video-overlay work (GoPro, helmet camera, FFmpeg
export) is desktop work done after the day. One desktop application serves
neither case well, so the product becomes two. The original plan kept both in
one repository; the owner later chose **separate repositories** (see
[Stages](#stages-current-plan)):

| | **FlappedEar Telemetry** | **FlappedEar Overlays** |
| --- | --- | --- |
| Platforms | iPhone, iPad, Android phones and tablets, and macOS and Windows desktop (owner decision, 2 October 2026) | macOS and Windows desktop (owner decision, 2 October 2026) |
| Job | Import the day's VBO/RCZ files from RaceChrono (share sheet on phones and tablets, file dialog or drop on the desktop), then show laps, best lap vs theoretical best on the map, time losses, section progression and the Corner Analyzer, to prepare the next run | Video + telemetry overlay editing and export, GoPro auto-sync, multiple video sources with manual sync, tyre pressure/temperature widgets |
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

## Target architecture (original one-repository design)

With separate repositories, this repository keeps telemetry-core, app-shared,
overlay-core and the Overlays app. After decision 6 ([KAN-167]) the Telemetry app
is a new Flutter app that consumes none of them; the `apps/telemetry/` line below
is historical.

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
  apps/telemetry/   FlappedEar Telemetry: touch-first QML; iOS, Android, macOS, Windows
  apps/overlays/    FlappedEar Overlays: the current editor; macOS, Windows
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

Owner direction (2 October 2026, with separate repositories): the two apps keep
one format, compatible between them, so that either app opens and re-saves the
other's documents without losing anything. The compatibility rules and the
round-trip tests in both directions are [KAN-170]; they are not written yet.
The [architect handover](telemetry-handover.md#the-shared-contract-fetproject)
summarises what the current code keeps, rejects and rewrites.

## Mobile specifics (Telemetry)

Written for the Qt plan. After decision 6 the requirements below (import,
screens, performance, storage) still describe the product; the Qt
implementation notes (C++ controllers, QML, `QSettings`, `GuiSessionLock`) do
not apply to the Flutter app.

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
   files. This must be verified with Qt before any store release, for the
   App Store and Google Play alike. It does not block development, the
   simulator or TestFlight-style internal testing.
2. **Product direction documents.** Decided 2 October 2026: two applications,
   the desktop editor for macOS and Windows and FlappedEar Telemetry for
   macOS, Windows, iOS and Android. `docs/product-vision.md` and `AGENTS.md`
   record it. Windows code changes are resumed for defects the owner finds
   on Windows (2 October 2026); Windows CI and packaging stay paused until the
   owner resumes them.
3. **Names and identities.** Decided 2 October 2026: the desktop editor is
   FlappedEar Overlays (`com.flappedear.overlays`), with a one-time move of
   preferences, templates, recovery and logs from the old storage identity
   that never deletes or overwrites anything ([KAN-125];
   [application identity](application-identity.md)). FlappedEar Telemetry
   takes `com.flappedear.telemetry`.
4. **Does Overlays keep a day-analysis window?** Decided 2 October 2026:
   eventually no, but not yet. Overlays is meant to become a pure overlay
   editor with data editing, and the Lap Analysis window and analysis
   workflows leave it only once FlappedEar Telemetry is mature enough to
   replace them. On 5 October 2026 the owner approved the removal ([KAN-169]
   done; [KAN-166] in progress, one PR per step). Still open with it: whether
   desktop Telemetry reuses today's desktop analysis windows or the new
   touch-first QML.
5. **Minimum OS versions and target devices** for all four platforms,
   starting with the owner's own phone and tablet. Decided 2 October 2026 for
   mobile: iOS 15 and Android 8.0 (API level 26) as the starting point. Desktop
   minimums are open.
6. **Telemetry technology and core sharing** ([KAN-167]). Decided 2 October
   2026: Flutter, from a blank page in a new repository with its own Jira
   project. No code is shared; this repository is cross-referenced only. Parsers
   and metrics therefore exist twice, and the `.fetproject` format ([KAN-170])
   and matching figures on shared fixtures keep the two apps consistent.

## Stages (current plan)

Owner direction, 2 October 2026: finish FlappedEar Overlays first, then build
FlappedEar Telemetry in its own repository. Jira epic [KAN-165]. Overlays
stays as it is until Telemetry can replace its analysis, and both apps keep one
compatible `.fetproject` format ([KAN-170]).

| Stage | Outcome | Jira |
| --- | --- | --- |
| 1. FlappedEar Overlays standalone (this repository) | Decision 3; "FlappedEar Overlays.app" with the new identity and a one-time move of preferences, templates, recovery and logs; macOS CI builds, tests and packages it. Merged in PR #142 | [KAN-122], [KAN-125] |
| 2. FlappedEar Telemetry, a new Flutter app in its own repository | Decision 6 (Flutter, blank page, own Jira project); the architect handover; then, tracked in the new Jira project: the repository and its CI, the analysis app for desktop and mobile, import, touch screens, memory budget and on-track acceptance; one `.fetproject` format compatible with Overlays | [KAN-167], [KAN-168], [KAN-170], [KAN-126]–[KAN-130] (moving to the new project) |
| 3. Remove the analysis from Overlays (in progress) | The owner approved it on 5 October 2026 ([KAN-169] records the last full-analysis commit, `7eae6cd`): the Lap Analysis window and analysis workflows leave Overlays, and day documents keep every analysis field | [KAN-169], [KAN-166] |

## Phases (original one-repository plan)

Phases 1 and 2 are done. Phases 3–5 are replaced by the stages above; the
table is kept for the record. Each phase ends with every existing test green. Phases 1–3 change no user
behaviour and can proceed before decisions 1 and 5.

| Phase | Outcome | Size |
| --- | --- | --- |
| 0. Decisions ([KAN-122]) | Items 1–5 above answered | owner |
| 1. Core separation ([KAN-123]) | `flappedear_telemetry_core` builds with Qt Core + zlib only. Cuts: `videoFingerprint` out of project code; `scene.widgets` optional; `PreviewPlayback`, `GuiSessionLock`, `AppLog` out of core; overlay code moved to `flappedear_overlay_core`; a CI job enforces the rule | M |
| 2. Controller split ([KAN-124]) | `DocumentController`, `AnalysisController` (no video, `VideoLink` interface) and `OverlayController` extracted from `AppController` behind the same QML-facing API; `TelemetryTests.cpp` split to match | L (largest risk) |
| 3. Two desktop apps ([KAN-125]) | FlappedEar Overlays (the editor; new identity with migration) and FlappedEar Telemetry for the desktop (analysis UI), both built and tested in CI for macOS, and for Windows once resumed | M |
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
- KAN-66 (A/B G-G scatter) was completed as a reusable panel in PR #74.
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
  Addressed by [KAN-125]: a one-time move that never deletes or overwrites.
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
[KAN-165]: https://kozucharkadiusz.atlassian.net/browse/KAN-165
[KAN-166]: https://kozucharkadiusz.atlassian.net/browse/KAN-166
[KAN-167]: https://kozucharkadiusz.atlassian.net/browse/KAN-167
[KAN-168]: https://kozucharkadiusz.atlassian.net/browse/KAN-168
[KAN-169]: https://kozucharkadiusz.atlassian.net/browse/KAN-169
[KAN-170]: https://kozucharkadiusz.atlassian.net/browse/KAN-170
