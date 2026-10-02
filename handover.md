# Session handover — 27 September 2026

Written for: the next Claude Code session continuing this work. Read this
first, then `AGENTS.md` (engineering rules and safety invariants).

**Several sessions work this repo, sometimes at the same time** (a
coordinator workspace without a Qt toolchain did KAN-42–54; another session
merged PR #56). Before starting, run `git fetch origin && git log --oneline
origin/main -20` and check Jira for tickets you assume are still open. Trust
that check over this file.

## Update — 2 October 2026

- Merged since 27 September: KAN-132 tyres widget (#125), KAN-134 hotlap tile
  (#121), KAN-135 lap-time decimals (#122), KAN-136 automatic segments (#123),
  KAN-137 off-track laps (#124), and the KAN-138 user guide (#126–#128).
- An independent read-only audit of `main` at `ca66169` produced the
  stabilisation epic **KAN-144** (KAN-145 to KAN-161). It found recovery loss
  when quitting at the recovery prompt, an auto-sync search range that can
  auto-apply an offset one lap away, two VBO memory blow-ups and a fixed
  30 s final-validation timeout. Finish KAN-144 before new features; it gates M5.
- `main` at `ca66169` was red in Debug on the post-import attach race
  (`TelemetryTests.cpp:8831`): KAN-143 fixes the tests, KAN-150 the product side.
- The M2–M4 epics (KAN-7, KAN-8, KAN-9) are closed.
- **Owner decision, 2 October 2026:** two applications. The desktop overlay
  editor targets macOS and Windows; Flapped Ear Telemetry targets macOS,
  Windows, iOS and Android. `AGENTS.md` and `product-vision.md` record it. Windows builds and CI
  stay paused until the owner resumes them. The other KAN-122 decisions (store
  licence, identities, desktop analysis window, devices) are open.
- **Separation (owner direction, 2 October 2026):** Flapped Ear Overlays is
  finished first as a standalone app in this repository (epic KAN-165, stage 1:
  KAN-122 decisions 3–4, KAN-125, KAN-166). Flapped Ear Telemetry is then built
  in a new repository (stage 2: KAN-167–KAN-169, KAN-126–KAN-130). KAN-121 is
  closed as superseded. Do not add Telemetry or mobile targets here.
- `currentstate.md` and `ROADMAP.md` were archived to `docs/history/` (KAN-159).
  `docs/product-delivery.md` and Jira are the only status records.

## Project

VBOOverlay / "Flapped Ear Telemetry": a Qt 6 / C++20 / QML macOS desktop app
combining a video overlay editor with track-day telemetry analysis (VBO/RCZ,
GoPro). Repo `arekkozuch/VBOOverlay`. Jira project KAN, cloud ID
`315ac5b8-6fd1-4518-8b5f-4433bcc33447`.

## Where M3/M4 stand (end of this session)

Merged to `main` today, all with hosted macOS Debug + Release green:

| Ticket | What | PR |
| --- | --- | --- |
| KAN-56 | Sector theoretical best across a compatible population, donor lap per sector | #57 |
| KAN-57 | Theoretical-best view, donor → Corner Analyzer | #58 |
| KAN-58 | M3 acceptance record `docs/kan58-m3-acceptance.md` (synthetic) | #59 |
| KAN-59 | Non-overlapping time-loss windows (`TimeLoss.h`) | #60 |
| KAN-60 | Ranked time losses (Day results → Time losses…) | #61 |
| KAN-61 | Loss → Corner Analyzer navigation, lap A/B "here" with video | #62 |
| KAN-116 | Connected corners proposed as one corner chain (proposal algorithm v2) | #63 |
| KAN-117 | Corner Analyzer usable on real recordings (side column, speeds, fixes) | #64 |
| KAN-118 | Accelerator pedal, not throttle plate, is the `throttle` alias | #65 |
| KAN-119 | Imported runs named "Session N" in recording order | #66 |
| KAN-120 | "Where your best lap can improve" map; gate-crossing segments timed | #67 |

Merged 26 September 2026:

| Ticket | What | PR |
| --- | --- | --- |
| KAN-62 | Lap and sector timing consistency (median, IQR, minimum 3) | #69 |
| KAN-63 | Braking/apex/exit/pickup and racing-line variability; braking approach stops at the previous corner (`braking-metrics-v2`) | #70 |
| KAN-64 | Progression → **By section** (typical time and spread per session) | #71 |
| KAN-65 | Timed G-G sample pairs (`GgPairs.h`) | #72 |
| KAN-66 | A/B G-G scatter with peaks in the comparison view | #74 |
| KAN-121 plan | `docs/product-split-plan.md` | #73 |

Merged later on 26 September 2026, which completes **M4**:

| Ticket | What | PR |
| --- | --- | --- |
| KAN-123 | Split phase 1: `flappedear_telemetry_core` (Qt Core + zlib) vs `flappedear_overlay_core` | #75 |
| KAN-67 | Recorded temperature summaries (`ChannelSummary.h`) | #76 |
| KAN-69 | Heart-rate summaries per run, section and A/B interval | #77 |
| KAN-68 | Progression → **Car & driver**: temperature trends per session, recorded cooling | #79 |
| KAN-70 | Heart rate in Car & driver and in the Corner Analyzer (A/B, across start/finish) | #80 |
| KAN-71 | Computed day-report model with provenance (`DayReport.h`) | #81 |
| KAN-72 | Day results → **Day report…** with evidence navigation | #82 |
| KAN-73 | "Where to look next": observations apart from hypotheses (`FocusAreas.h`) | #83 |
| KAN-74 | M4 acceptance `docs/kan74-m4-acceptance.md`, plus a fix: Save As no longer discards computed results | #84 |

Merged 27 September 2026 (M5 checks, and M6 import work, while the
remaining M5 items wait for the owner):

| Ticket | What | PR |
| --- | --- | --- |
| KAN-124 | Split phase 2 closed: `AnalysisController`, `DocumentController`, `TelemetryController` (Qt-Core-only `flappedear_telemetry_app`), core tests split | #94–#97 |
| KAN-77 | Full-day budgets on the real day: import to report about 7 s (Release), peak 150 MiB | #98 |
| KAN-76 | Export refuses a hard link to any day source | #99 |
| KAN-75 | Export to a filling destination (disk image) is safe; "the disk ran out of space" message | #100 |
| KAN-78 | Controls reachable at 1180×720 and 760×480; Escape no longer discards typing | #101 |
| KAN-79 | Full-day acceptance record `docs/kan79-full-day-acceptance.md` | #102 |
| KAN-82 | A complete day through move, relink and recovery; the A/B lap returns after a relink | #103 |
| KAN-87 | Folder import (`scanTelemetryFolder`) | #104 |
| KAN-88 | Drag-and-drop import of files and folders | #105 |
| KAN-90 | Attach a recording to a run after reviewing evidence; choose the primary | #106 |
| — | Adding runs to a day is not made stale by analysis bookkeeping | #107 |

Merged later on 27 September 2026 (M6 analysis, source fusion and video):

| Ticket | What | PR |
| --- | --- | --- |
| KAN-91 | Driving states with provenance (`DrivingStates.h`) | #108 |
| KAN-92 | Coasting by episode, segment and lap; on the map | #109 |
| KAN-93 | Trail braking in the Corner Analyzer | #110 |
| KAN-97 | A/B map layers: speed, Δ, G, pedals, recorded temperatures (`MapLayers.h`) | #111 |
| KAN-100 | Temperature against lap time and acceleration, with the time-of-day confound (`TemperatureAssociation.h`) | #112 |
| KAN-101 | Recording clock alignment, declared against measured (`RecordingAlignment.h`); sync engine moved into telemetry core | #113 |
| KAN-102 | Channel fusion policy with per-sample provenance (`ChannelFusion.h`) | #114 |
| KAN-103 | Fusion review, approval and reopen in Run details (`run.fusion`) | #115 |
| KAN-104 | GoPro chapter groups reviewed before loading (`GoProChapters.h`) | #116 |
| KAN-105 | Chapters played as one timeline (`MediaTimeline.h`, `video.chapters`) | #117 |
| KAN-107 | Side-by-side A/B lap video from each run's verified footage | #118 |
| KAN-133 | No compiler warnings in a clean build | #119 |

**KAN-106 (export across chapters) is paused, deliberately.** Exporting a
chaptered video is refused with an explicit message, so the first chapter is
never exported alone. The Jira scoping note lists what a safe version needs:
- exact per-chapter tick counts, passed to the worker instead of re-probing;
- a concat list as a new owned manifest artifact;
- an audio policy at chapter joins;
- export protection of chapters 2..N in single-video projects.

It changes the protected staged export, so it waits for the owner and for
KAN-81.

Fusion (KAN-101–103) and chapters/side-by-side video (KAN-104–107) are
validated on synthetic data only. The private day has one VBO per session
and no video.

**Waiting for the owner (M5):**
- KAN-80 and KAN-81 need the matching private video.
- KAN-83 needs a clean account or machine.
- KAN-84 and KAN-85 depend on Windows work, which is paused.
- KAN-86 closes M5 once those are resolved.
- KAN-79 has questions for the owner in its record: lap times against
  RaceChrono, two laps flagged off-route, and an RCZ to close the blocked
  alternative case.
- KAN-89 (reusable vehicle and track profiles) needs a product decision:
  where shared profiles live, and how they differ from an event's setup
  snapshot.
- KAN-113, KAN-114 and KAN-115 are owner-marked "backlog only".
- KAN-94–96 (realistic potential) and KAN-108–109 (Explain this lap) depend
  on KAN-86. KAN-98–99 (comparable visits) depend on KAN-89.
- KAN-106 (chapter export) is paused; see above.
- KAN-132 (tyre data) is done: the source is RaceChrono's CAN tyre channels in the VBO (merged in #125).

KAN-124 (split phase 2) steps 1–11 are merged (#85, #87–#92, #94–#97):
- **Step 1:** analysis reaches video only through `VideoLink`, and its guards
  use `documentBusy()`.
- **Steps 2–7:** these moved into `flappedear_telemetry_core`:
  - lap derivation (`deriveOutingLaps`);
  - the lap loader (`loadOutingLapDetail`);
  - channel summaries (`summarizeOutingChannels`);
  - the theoretical-best worker and its published results
    (`calculateOutingTheoreticalBest`, `publish*`);
  - the day-report assembly (`buildOutingDayReport`).

  `OutingPipelineTests` runs the pipeline linking only core.
- **Step 8:** `AnalysisController` owns the analysis state and reads the
  project only through `AnalysisDocument`. `AppController` forwards the
  unchanged QML API to it.
- **Step 9:** `DocumentController` owns the project document (saved state,
  recovery, open/save/quit, run selection, import) and implements
  `AnalysisDocument`. The editor state stored in the project comes through
  `DocumentHost`, which `AppController` implements.
- **Step 10:** `TelemetryController` pairs the document and analysis with
  no editor. It lives with them in `flappedear_telemetry_app` (Qt Core
  only; boundary test), and `flappedear_telemetry_app_tests` runs a whole
  day through it headless. This is the controller the Telemetry app
  (KAN-125) builds its UI on.
- **Step 11 (tests):** 42 pure telemetry-core test functions moved from
  `TelemetryTests.cpp` to `TelemetryCoreTests.cpp`
  (`flappedear_telemetry_core_tests`, core only).

To verify a refactor, set `FLAPPEDEAR_REPORT_DUMP` on the private real-day
test and diff the dumps after normalising identities (see
`docs/testing.md`).

KAN-116–120 came from the **owner testing the real Jastrząb day**
(`jastrzab/`, git-ignored). That testing exposed problems synthetic fixtures
never showed, so develop against real data. See "Real data" below.

**Owner track day, 27 September 2026.** Things to ask about afterwards:
- the Day report's "Where to look next", and whether the three areas match
  the driver's own view;
- whether the Day report should open by itself after an import (KAN-72 left
  this to the owner);
- the real-track sections of `docs/kan58-m3-acceptance.md` and
  `docs/kan74-m4-acceptance.md`, which are still to be filled in.

KAN-124 is closed (Gotowe, 27 September 2026). `OverlayController` was not
extracted: `AppController` already holds only the overlay editor plus
`DocumentHost`/`VideoLink` and forwarding. The reasoning is in the Jira
comment; the owner can reopen the ticket.

For any further refactor, run the private real-day check before and after.
Its report key values must stay identical.

**KAN-77 (full-day budgets).** `TelemetryAppTests::measuresAPrivateFullDay`
measures the real day headless and enforces budgets. A day from import to
report takes about 7 s in Release, with a peak of 150 MiB. See
`docs/testing.md`.

## The product split (owner direction, 26 September 2026)

The owner wants **two products**. Since 2 October 2026 they live in
**separate repositories**: Overlays here, Telemetry in a new one (KAN-165):

- **Flapped Ear Telemetry:** a quick iPhone, iPad and Android app to view
  the day's telemetry at the track (nobody carries a laptop to the track),
  also built for macOS and Windows (owner decision, 2 October 2026).
- **Flapped Ear Overlays:** the desktop video-overlay editor. Planned
  additions are tyre pressure and temperature, and a helmet camera without
  GPS, which needs manual sync of multiple video sources.

Read `docs/product-split-plan.md` (epic KAN-165; KAN-121 is closed) before
any structural work. Phase 0 (KAN-122): the platforms were decided on
2 October 2026 (desktop editor on macOS and Windows, Telemetry on macOS,
Windows, iOS and Android) and the tyre data source is settled (KAN-132).
Still open: the Qt store licence, bundle identities, whether the overlay
editor keeps a day-analysis window, and target devices. Stage 1 (Overlays
standalone) needs decisions 3 (identities) and 4 (analysis window).

Split rules already in force after KAN-123:
- `src/telemetry` and `src/project` form `flappedear_telemetry_core`, which
  links **Qt Core and zlib only**.
- `flappedear_telemetry_core_boundary` fails the build if they include
  overlay, app or Gui headers.
- New analysis code goes there, and its pure tests link only that library.
- Keep analysis features independent of video.

## Owner direction and preferences (this session)

- **Merging:** the owner authorised merging for this session ("you can
  merge — keep working until I say stop"). Without that, never merge; post
  green CI and wait. See memory `feedback-merge-and-ship-loop`.
- **The owner reads the app as a driver.** Tables of numbers are not enough.
  Lead with the best lap and where time is, on the track map. Say "Session 3 ·
  LAP 2", not filenames. Times of a minute or more are `m:ss.mmm`
  (`AppController::formatElapsedTime`).
- **Two apps:** decided, in separate repositories. See "The product split"
  above (epic KAN-165).
- One focused PR per ticket; the owner creates or asks for Jira tickets for
  feedback-driven work (KAN-116–120 were created this way).

## Real data

```bash
FLAPPEDEAR_REAL_DAY="$PWD/jastrzab" FLAPPEDEAR_CORNER_REVIEW_DIR=/some/scratch/dir \
  ./build-native/native/tests/flappedear_native_tests analyzesPrivateTrackDayCorners
```

It imports the six recordings, approves every proposal on the best lap's
run (without a manual split, as a driver would), and prints:
- proposals, theoretical best, ranked losses and per-segment metrics;
- temperatures with cooling, and heart rate;
- the day report with its focus areas.

It also saves and reopens the day and requires an identical report. With
the review directory set, it saves screenshots of:
- the Analysis window and the Corner Analyzer;
- G-G and the theoretical best;
- the By section and Car & driver tabs;
- the day report.

With the review directory set, it also saves screenshots of trail braking,
the speed and brake map layers, and the temperature association. It logs
every map layer and association. Latest additions:
- Corners 2–3 trail braking: 1.8 s over 35 m (Session 2 LAP 1) against
  2.9 s over 65 m on the best lap;
- every temperature correlates with quicker laps (coolant ρ −0.84), but all
  of them rise with the order of laps (ρ 0.81–0.86), so every card is
  flagged as confounded.

**Look at the screenshots** before claiming UI work is done. Last result:
- 1:49.898 best against 1:47.905 theoretical, 1.993 s available;
- focus areas: Corners 9–16 (+0.619 s against Session 6 · LAP 3), Corners
  2–3 lost in 5 of 5 session bests, and the braking point for Corners 5–6
  spread over 20.6 m;
- oil up to 128;
- heart rate 119–136 bpm per session.

Facts learned from the owner's car (RaceChrono Pro VBO/RCZ with OBD):
`accelerator_pos-obd` is the pedal (0–100 %); `throttle_pos-obd` is the
plate (13.3 % idle, 80.4 % fully open, fully open from about 70 % pedal,
rev-match blips on downshifts with the pedal at 0). Speed is `velocity`
(km/h), brake is `brake_pos-obd`.

## Known gaps and traps

- **VBO channel units are not recorded.** RaceChrono declares them in
  `[header]`, but `channel.unit` is part of the recording fingerprint
  (`ProjectSourceReference.cpp`). Adding units would make every saved project
  demand a relink, so it needs a fingerprint migration first. Speeds therefore
  show without a unit.
- **Canonical segmentation:** segments are approved per run. The theoretical
  best, the loss ranking and the Corner Analyzer, when opened from them, use
  the lowest-run-ID eligible run with approved segments. The axis is built
  from that run's fastest lap.
- Corner/braking/exit metrics for a gate-crossing segment stay unavailable;
  only its sector time is computed.
- **Stacked PRs:** `gh pr merge --delete-branch` on a base branch
  **auto-closes** PRs stacked on it (GitHub did not retarget them). Point
  every PR at `main` before merging, and delete branches only at the end.
- QML: under `pragma ComponentBehavior: Bound`, qualify child reads through
  an explicit `id`. `Dialog.result` is a FINAL property, so don't name a
  property `result`. `slots` is a Qt macro in C++ tests. ListView delegates
  are not reachable with `findChild`; use `itemAtIndex`. Repeater delegates
  inside Flickables and Popups are not either; search the visual tree
  (`childItems()` from `window->contentItem()`). `focus` is also FINAL (use
  `focusResult`), and so is `Item.data`. An `ItemDelegate`'s `text` is not
  its content; read the property you set. Register every new QML file in
  `native/CMakeLists.txt`'s `QML_FILES`, **and every new `AppController*.cpp`
  in `native/tests/CMakeLists.txt`'s source list**, or startup smoke or the
  test link fails.
- Hosted runners lay out later than this Mac. Measure QML geometry with
  `QTRY_VERIFY`, not `QVERIFY` (a KAN-68 test flaked on CI this way).
- Save As changes the project path and re-derives the laps. Results
  invalidated on `outingLapsChanged` must wait until `!outingLapsLoading()`
  and then compare their input key (KAN-74 fix).
- **Tests after an import:** the analysis records its track inference as an
  edit. Wait for `!outingLapsLoading()` with laps present before attaching a
  recording or selecting a lap, or the review goes stale on the hosted runner
  (this bit KAN-103 twice). After a save under a new path, select laps only
  once they settle.
- **QML media:**
  - `MediaPlayer` can report `LoadedMedia` more than once for one file;
  - a position set while the file loads can be ignored;
  - keep a requested position until a frame at it has shown (KAN-105 in
    `Main.qml`);
  - silent priming (play, then pause on a frame) is not "playing".
- **QML bindings:** inside a property's change handler, other bindings that
  depend on it can still be stale, so read the property itself
  (`ComparisonVideoPane.show()`). `layer` is FINAL on `Item`.
- `Main.qml` can be loaded in a test from its file with `appController` only.
  The branding image is not in the test binary, so ignore "Cannot open:
  qrc:" warnings there.

## Working conventions (still in force)

- Jira: "W toku" (transition `21`) when starting; comment with branch, PR,
  what landed, evidence and gaps; "Gotowe" (`41`) after merge with the final
  SHA and green main CI. All Jira content in English.
- Build/test gate every time: `cmake --build build-native --parallel` and
  `ctest --test-dir build-native --output-on-failure` (38 suites; a clean
  build prints no warnings since KAN-133). Reconfigure
  with `cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt` after adding source files.
- Cloud CI: macOS Debug + Release on every PR push and on push to `main`
  (the main concurrency group cancels superseded runs, so verify the latest
  main SHA). macOS only; no Windows work.
- Documentation ships in the same PR (`docs/testing.md`,
  `docs/telemetry-semantics.md`, the implementation column of
  `docs/product-delivery.md`). `docs/product-vision.md` and delivery forecasts
  are owner-authored: flag staleness rather than rewriting them.
- Never kill an app process you didn't start; the owner may be using it.

## Credentials/access

- Jira via the `plugin:atlassian:atlassian` MCP connector; expect to
  re-authorize with `/mcp` in a new session.
- GitHub: `gh` is authenticated for `arekkozuch/VBOOverlay`.
